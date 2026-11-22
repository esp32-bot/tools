#!/usr/bin/env python3
"""Run console commands on the robot over USB serial, without resetting it.

    tools/console.py --port /dev/cu.usbserial-XXXX get "set volume 40" save

Prints what each command prints. Exits non-zero if a command fails, is unknown, or the
console doesn't answer. `tools/bx cmd ...` calls this with the port filled in.
"""

import argparse
import errno
import re
import sys
import time
from pathlib import Path

import serial

REPO = Path(__file__).resolve().parent.parent
CONFIG_H = REPO / "main" / "config.h"
SDKCONFIG = REPO / "sdkconfig"

TIMEOUT_S = 5  # long enough for `reboot` to come back with a prompt
QUIET_S = 0.2
ANSI_ESCAPE = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]")
# What the console prints when a command fails (esp_console_common.c).
FAILURE_MARKERS = (b"Unrecognized command", b"Command returned non-zero error code")

# When a terminal answered its probe at boot, the console's line editor (linenoise) asks for
# the cursor position before every prompt, and waits until it gets an answer. It measures the
# width by asking once, jumping to the right edge, and asking again.
CURSOR_QUERY = b"\x1b[6n"
JUMP_TO_RIGHT_EDGE = b"\x1b[999C"
CURSOR_AT_START = b"\x1b[1;1R"
CURSOR_AT_EDGE = b"\x1b[1;80R"


def firmware_value(path: Path, pattern: str) -> str:
    match = re.search(pattern, path.read_text(), re.MULTILINE)
    if not match:
        sys.exit(f"Can't find {pattern!r} in {path}")
    return match.group(1)


class Console:
    def __init__(self, port: str, baud: int, prompt: bytes):
        self.prompt = prompt
        self.link = serial.Serial()
        self.link.port = port
        self.link.baudrate = baud
        self.link.timeout = 0.05
        # On the M5StickC, RTS drives reset and DTR the boot pin, through a circuit that keeps
        # the chip running while both are on or both off. Opening the port turns both on, and
        # pyserial would then turn DTR off first, resetting the robot. So it opens with both
        # on, and __enter__ turns RTS off first.
        self.link.dtr = True
        self.link.rts = True
        self.received = b""
        self.answered = 0  # how far into `received` cursor questions have been answered

    def __enter__(self):
        self.link.open()
        try:
            self.link.rts = False
            self.link.dtr = False
        except OSError as error:
            # A port without modem lines, like a pseudo-terminal; pyserial's open() ignores these too.
            if error.errno not in (errno.EINVAL, errno.ENOTTY):
                self.link.close()
                raise
        return self

    def __exit__(self, *exc):
        self.link.close()

    def sync(self) -> bool:
        """Gets the console to a fresh prompt, whatever state it was left in."""
        self.link.reset_input_buffer()
        # Frees a line editor stuck waiting for a cursor answer. Anywhere else it's typed
        # into the line, which then fails harmlessly as a command.
        self.link.write(CURSOR_AT_START)
        self.wait_until_quiet()
        self.link.write(b"\r")
        return self.read_until_prompt() is not None

    def run(self, command: str) -> bytes | None:
        """The command's output, or None if the prompt didn't come back."""
        self.link.write(command.encode() + b"\r")
        output = self.read_until_prompt()
        # The console echoes the command line first.
        return None if output is None else output.partition(b"\n")[2]

    def read_until_prompt(self) -> bytes | None:
        """Everything up to the next prompt on a new line, escape codes removed. Log lines
        that other tasks print after the prompt (as at boot) are kept, after the rest, so it
        returns once the line has been quiet for a read."""
        deadline = time.monotonic() + TIMEOUT_S
        while time.monotonic() < deadline:
            arrived = self.receive()
            clean = ANSI_ESCAPE.sub(b"", self.received)
            before, prompt, after = clean.rpartition(b"\n" + self.prompt)
            if prompt and not arrived and (not after or after.endswith(b"\n")):
                self.received, self.answered = b"", 0
                return before + b"\n" + after
        return None

    def wait_until_quiet(self):
        """Receives, and drops, until nothing has arrived for QUIET_S."""
        quiet_since = time.monotonic()
        while time.monotonic() - quiet_since < QUIET_S:
            if self.receive():
                quiet_since = time.monotonic()
        self.received, self.answered = b"", 0

    def receive(self) -> bool:
        """Reads what has arrived, answering cursor questions like a terminal. False if nothing."""
        data = self.link.read(self.link.in_waiting or 1)
        self.received += data
        while (question := self.received.find(CURSOR_QUERY, self.answered)) != -1:
            at_edge = self.received.rfind(JUMP_TO_RIGHT_EDGE, self.answered, question) != -1
            self.link.write(CURSOR_AT_EDGE if at_edge else CURSOR_AT_START)
            self.answered = question + len(CURSOR_QUERY)
        return bool(data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", required=True)
    parser.add_argument("commands", nargs="+", help="one console command per argument")
    args = parser.parse_args()

    prompt = firmware_value(CONFIG_H, r'CONSOLE_PROMPT\[\] = "([^"]*)"').encode() + b" "
    baud = int(firmware_value(SDKCONFIG, r"^CONFIG_ESP_CONSOLE_UART_BAUDRATE=(\d+)$"))

    failed = False
    with Console(args.port, baud, prompt) as console:
        if not console.sync():
            sys.exit("The robot's console didn't answer. Is the BalaX firmware running?")
        for command in args.commands:
            output = console.run(command)
            if output is None:
                sys.exit(f"No prompt after '{command}'")
            text = output.replace(b"\r\n", b"\n").decode(errors="replace")
            print(text, end="" if not text or text.endswith("\n") else "\n")
            failed |= any(marker in output for marker in FAILURE_MARKERS)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
