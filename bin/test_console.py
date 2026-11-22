#!/usr/bin/env python3
"""Tests console.py against a fake robot on a pseudo-terminal, no hardware needed.

    tools/bx test
"""

import os
import pty
import threading
import unittest

from console import Console

PROMPT = b"balax> "
COLOR_PROMPT = b"\x1b[0;32m" + PROMPT + b"\x1b[0m"
REPLIES = {b"get volume": b"50\r\n", b"reboot": b"ets Jun  8 2016\r\n", b"": b""}
UNKNOWN = b"Unrecognized command\r\n"
# Another task can log after the prompt is up, as the main task does at the end of boot.
LOGGED_AFTER_PROMPT = {b"reboot": b"I (917) main_task: Returned from app_main()\r\n"}


class FakeRobot(threading.Thread):
    """The robot's end of the console. In `terminal` mode it behaves like linenoise after a
    terminal answered its probe: it waits for a cursor-position answer before each prompt.
    `waiting` starts it blocked on such a question, as if a terminal had just disconnected."""

    def __init__(self, fd: int, terminal: bool, waiting: bool = False):
        super().__init__(daemon=True)
        self.fd = fd
        self.terminal = terminal
        self.waiting = waiting

    def run(self):
        try:
            if self.waiting:
                self.prompt()
            line = b""
            while (key := os.read(self.fd, 1)) != b"":
                if key != b"\r":
                    line += key
                    os.write(self.fd, b"\r" + COLOR_PROMPT + line + b"\x1b[0K" if self.terminal else key)
                    continue
                os.write(self.fd, b"\r\n")
                os.write(self.fd, REPLIES.get(line, UNKNOWN))
                self.prompt()
                os.write(self.fd, LOGGED_AFTER_PROMPT.get(line, b""))
                line = b""
        except OSError:
            pass  # the test closed its end

    def prompt(self):
        if self.terminal:
            for question in (b"\x1b[6n", b"\x1b[999C\x1b[6n"):
                os.write(self.fd, question)
                while os.read(self.fd, 1) != b"R":
                    pass
        os.write(self.fd, COLOR_PROMPT if self.terminal else PROMPT)


class ConsoleTest(unittest.TestCase):
    def check(self, terminal: bool, waiting: bool = False):
        robot_end, tool_end = pty.openpty()
        tty = os.ttyname(tool_end)
        FakeRobot(robot_end, terminal, waiting).start()
        try:
            with Console(tty, 115200, PROMPT) as console:
                self.assertTrue(console.sync())
                self.assertEqual(console.run("get volume"), b"50\r\n")
                self.assertEqual(console.run("bogus"), UNKNOWN)
                self.assertEqual(console.run("reboot"), REPLIES[b"reboot"] + LOGGED_AFTER_PROMPT[b"reboot"])
                self.assertEqual(console.run("get volume"), b"50\r\n")
        finally:
            os.close(tool_end)
            os.close(robot_end)

    def test_plain_console(self):
        self.check(terminal=False)

    def test_terminal_console(self):
        self.check(terminal=True)

    def test_console_waiting_for_cursor(self):
        self.check(terminal=True, waiting=True)


if __name__ == "__main__":
    unittest.main()
