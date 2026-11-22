#!/usr/bin/env python3
"""Manage the robot's Wi-Fi and LAN OTA/Monitor cryptographic secret, kept in gitignored secrets/wifi.json.

The firmware build turns that file into a header (see `header`), so the next build and
flash carries it to the robot. The robot joins the first of its networks that's in range,
and always runs its own access point too: that's how JoyX remotes find it, and how to
reach it when no network is in range.

    tools/wifi.py add <ssid>       add a network; prompts for the password (not echoed)
    tools/wifi.py add-current      add a network this Mac knows, password from the Keychain
    tools/wifi.py remove <ssid>
    tools/wifi.py ap <ssid>        name the robot's access point; prompts for a password
    tools/wifi.py secret           set or generate the LAN OTA/Monitor password (PBKDF2-HMAC-SHA256 derived)
    tools/wifi.py list
    tools/wifi.py header <out.h>   used by the build

The robot's radio only reaches 2.4 GHz networks.
"""

import argparse
import getpass
import hashlib
import json
import re
import secrets
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_FILE = REPO / "secrets" / "wifi.json"

SSID_MAX_BYTES = 32
PASSWORD_MIN_CHARS = 8   # WPA2 passphrase; empty means an open network
PASSWORD_MAX_CHARS = 63
# Without `ap`: an open access point with this name.
DEFAULT_ACCESS_POINT = {"ssid": "BalaX", "password": ""}
# How many of this Mac's networks `add-current` offers.
KNOWN_SHOWN = 15
KEYCHAIN_KIND = "AirPort network password"
PBKDF2_ITERATIONS = 100_000


def load(path: Path) -> dict:
    return json.loads(path.read_text()) if path.exists() else {}


def save(path: Path, config: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(config, indent=2) + "\n")
    path.chmod(0o600)


def derive_lan_key(password: str, salt_hex: str) -> bytes:
    return hashlib.pbkdf2_hmac(
        "sha256",
        password.encode("utf-8"),
        bytes.fromhex(salt_hex),
        PBKDF2_ITERATIONS,
        dklen=32,
    )


def ensure_lan_auth(config: dict, path: Path) -> tuple[bytes, str]:
    """Ensure a cryptographic LAN key exists in secrets/wifi.json; auto-generate if missing."""
    lan = config.get("lan_auth")
    if not lan or "salt_hex" not in lan or ("key_hex" not in lan and "password" not in lan):
        pw = secrets.token_urlsafe(18)
        salt_hex = secrets.token_hex(16)
        key = derive_lan_key(pw, salt_hex)
        config["lan_auth"] = {
            "password": pw,
            "salt_hex": salt_hex,
            "key_hex": key.hex(),
            "iterations": PBKDF2_ITERATIONS,
        }
        save(path, config)
        return key, salt_hex
    salt_hex = lan["salt_hex"]
    if "key_hex" in lan:
        return bytes.fromhex(lan["key_hex"]), salt_hex
    key = derive_lan_key(lan["password"], salt_hex)
    lan["key_hex"] = key.hex()
    save(path, config)
    return key, salt_hex


def read_password(from_stdin: bool, prompt: str = "Password (empty for an open network): ") -> str:
    if from_stdin:
        return sys.stdin.readline().rstrip("\n")
    return getpass.getpass(prompt)


def validate(ssid: str, password: str) -> None:
    if not 0 < len(ssid.encode()) <= SSID_MAX_BYTES:
        sys.exit(f"SSID must be 1-{SSID_MAX_BYTES} bytes")
    if password and not PASSWORD_MIN_CHARS <= len(password) <= PASSWORD_MAX_CHARS:
        sys.exit(f"Password must be {PASSWORD_MIN_CHARS}-{PASSWORD_MAX_CHARS} characters, or empty")


def add_network(config: dict, ssid: str, password: str) -> None:
    validate(ssid, password)
    networks = config.setdefault("networks", [])
    entry = {"ssid": ssid, "password": password}
    existing = [i for i, n in enumerate(networks) if n["ssid"] == ssid]
    if existing:
        networks[existing[0]] = entry  # keep its place in the try order
    else:
        networks.append(entry)


def run(*command: str) -> subprocess.CompletedProcess:
    return subprocess.run(command, capture_output=True, text=True)


def wifi_device() -> str:
    ports = run("networksetup", "-listallhardwareports").stdout
    match = re.search(r"Hardware Port: Wi-Fi\nDevice: (\S+)", ports)
    if not match:
        sys.exit("This Mac has no Wi-Fi")
    return match.group(1)


def current_ssid(device: str) -> str | None:
    """The network this Mac is on, unless macOS hides it (it does from apps without
    Location Services)."""
    summary = run("ipconfig", "getsummary", device).stdout
    match = re.search(r"^\s+SSID : (.+)$", summary, re.MULTILINE)
    ssid = match.group(1) if match else None
    return None if ssid in (None, "<redacted>") else ssid


def pick_known(device: str) -> str:
    listed = run("networksetup", "-listpreferredwirelessnetworks", device).stdout.splitlines()[1:]
    known = [line.strip() for line in listed if line.strip()][:KNOWN_SHOWN]
    if not known:
        sys.exit("This Mac knows no Wi-Fi networks; use `add <ssid>`")
    print("macOS doesn't tell apps which network it's on. Networks this Mac knows, most preferred first:")
    for number, ssid in enumerate(known, 1):
        print(f"  {number:2}  {ssid}")
    choice = input("Which one? [1] ").strip() or "1"
    if not choice.isdigit() or not 1 <= int(choice) <= len(known):
        sys.exit("No such number")
    return known[int(choice) - 1]


def keychain_password(ssid: str) -> str | None:
    print(f"Reading the password for '{ssid}' from the Keychain; macOS may ask you to allow it.")
    found = run("security", "find-generic-password", "-D", KEYCHAIN_KIND, "-a", ssid, "-w")
    return found.stdout.rstrip("\n") if found.returncode == 0 else None


def c_string(text: str) -> str:
    # Every byte as a 3-digit octal escape: safe for any character, and unlike \x
    # escapes it can't swallow the characters that follow.
    return '"' + "".join(f"\\{b:03o}" for b in text.encode()) + '"'


def c_credentials(entry: dict) -> str:
    return f"{{{c_string(entry['ssid'])}, {c_string(entry['password'])}}}"


def header(config: dict, path: Path) -> str:
    networks = config.get("networks", [])
    access_point = config.get("access_point", DEFAULT_ACCESS_POINT)
    lan_key, _ = ensure_lan_auth(config, path)
    entries = "".join(f"    {c_credentials(n)},\n" for n in networks)
    key_bytes = ", ".join(f"0x{b:02x}" for b in lan_key)
    return (
        "// Generated by tools/wifi.py from secrets/wifi.json. Do not edit or commit.\n"
        "#pragma once\n"
        "#include <array>\n"
        "#include <cstdint>\n"
        "#include <string_view>\n\n"
        "struct WifiCredentials {\n"
        "    std::string_view ssid;\n"
        "    std::string_view password;  // empty for an open network\n"
        "};\n\n"
        "// Networks to join, in order of preference.\n"
        f"inline constexpr std::array<WifiCredentials, {len(networks)}> WIFI_NETWORKS{{{{\n"
        f"{entries}"
        "}};\n\n"
        "// The robot's own access point.\n"
        f"inline constexpr WifiCredentials WIFI_ACCESS_POINT = {c_credentials(access_point)};\n\n"
        "// 256-bit PBKDF2-HMAC-SHA256 derived secret key for LAN OTA & LAN Monitor challenge-response.\n"
        f"inline constexpr std::array<uint8_t, 32> LAN_AUTH_KEY = {{{key_bytes}}};\n"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--file", type=Path, default=DEFAULT_FILE, help=argparse.SUPPRESS)
    commands = parser.add_subparsers(dest="command", required=True)
    add = commands.add_parser("add", help="add a network, or update its password")
    add.add_argument("ssid")
    add.add_argument("--password-stdin", action="store_true", help="read the password from stdin")
    commands.add_parser("add-current", help="add a network this Mac knows, with its Keychain password")
    commands.add_parser("remove", help="remove a network").add_argument("ssid")
    ap = commands.add_parser("ap", help="name the robot's access point and set its password")
    ap.add_argument("ssid")
    ap.add_argument("--password-stdin", action="store_true", help="read the password from stdin")
    sec = commands.add_parser("secret", help="set or auto-generate the LAN OTA/Monitor secret password")
    sec.add_argument("--password-stdin", action="store_true", help="read the secret password from stdin")
    sec.add_argument("--generate", action="store_true", help="generate a new random secret password")
    commands.add_parser("list", help="list networks, in the order the robot tries them, and the access point")
    commands.add_parser("header", help="write the C++ header the firmware includes").add_argument("out", type=Path)
    args = parser.parse_args()

    config = load(args.file)
    saved = "The next build and flash puts it on the robot."

    if args.command == "add":
        add_network(config, args.ssid, read_password(args.password_stdin))
        save(args.file, config)
        print(f"Saved '{args.ssid}'. {saved}")
    elif args.command == "add-current":
        device = wifi_device()
        ssid = current_ssid(device) or pick_known(device)
        password = keychain_password(ssid)
        if password is None:
            print("Not in the Keychain.")
            password = read_password(False)
        add_network(config, ssid, password)
        save(args.file, config)
        print(f"Saved '{ssid}'. {saved}")
    elif args.command == "remove":
        networks = config.get("networks", [])
        kept = [n for n in networks if n["ssid"] != args.ssid]
        if len(kept) == len(networks):
            sys.exit(f"No network named '{args.ssid}'")
        config["networks"] = kept
        save(args.file, config)
        print(f"Removed '{args.ssid}'.")
    elif args.command == "ap":
        password = read_password(args.password_stdin)
        validate(args.ssid, password)
        config["access_point"] = {"ssid": args.ssid, "password": password}
        save(args.file, config)
        print(f"The robot's access point is '{args.ssid}'. {saved}")
    elif args.command == "secret":
        if args.generate:
            pw = secrets.token_urlsafe(18)
        else:
            pw = read_password(args.password_stdin, "LAN OTA/Monitor secret password (empty to generate random): ")
            if not pw:
                pw = secrets.token_urlsafe(18)
        salt_hex = secrets.token_hex(16)
        key = derive_lan_key(pw, salt_hex)
        config["lan_auth"] = {
            "password": pw,
            "salt_hex": salt_hex,
            "key_hex": key.hex(),
            "iterations": PBKDF2_ITERATIONS,
        }
        save(args.file, config)
        print(f"Saved LAN cryptographic key (PBKDF2-HMAC-SHA256, {PBKDF2_ITERATIONS} iterations). {saved}")
    elif args.command == "list":
        for n in config.get("networks", []):
            print(f"{n['ssid']}{'' if n['password'] else '  (open)'}")
        access_point = config.get("access_point", DEFAULT_ACCESS_POINT)
        print(f"Access point: {access_point['ssid']}{'' if access_point['password'] else '  (open)'}")
        has_lan = "lan_auth" in config
        print(f"LAN OTA/Monitor key: {'configured (PBKDF2-HMAC-SHA256)' if has_lan else 'will auto-generate on build'}")
    elif args.command == "header":
        text = header(config, args.file)
        # Rewrite only on change, so an unchanged file doesn't trigger a rebuild.
        if not args.out.exists() or args.out.read_text() != text:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(text)


if __name__ == "__main__":
    main()
