#!/usr/bin/env python3
"""Cryptographically authenticated LAN OTA flasher, live log monitor, and remote console over Wi-Fi IP.

Security Model:
  1. Key Derivation: A 256-bit secret key K is derived via PBKDF2-HMAC-SHA256 (100,000 iterations)
     from the robot's secret password and salt in `secrets/wifi.json`. The raw password is never stored
     on the robot; only the 256-bit derived key K is embedded in the firmware.
  2. Challenge-Response (Anti-Replay): Before every operation, the client requests a fresh 256-bit
     hardware-TRNG one-time nonce from `GET http://<ip>/api/challenge`.
  3. Cryptographic Signing & Firmware Authentication:
     - For LAN OTA (`POST /api/ota`), the client computes `HMAC-SHA256(K, nonce || firmware_bytes)`
       and sends `X-Nonce` and `X-Signature`. The robot streams the firmware into the inactive OTA slot
       while computing the HMAC and refuses to activate the partition unless the signature matches in
       constant time.
     - For LAN Console (`POST /api/cmd`) and LAN Monitor (`GET /api/logs`), every request is signed
       with `HMAC-SHA256(K, nonce || payload)`.

Usage:
  tools/bx lan-flash [ip]               build (or use build/balax.bin) and flash over LAN
  tools/bx lan-monitor [ip]             stream live logs and send interactive console commands over LAN
  tools/bx lan-cmd <ip> <command>...    run console commands over LAN (e.g. status, arm, get, set ...)
"""

import argparse
import hashlib
import hmac
import json
import os
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SECRETS_FILE = REPO / "secrets" / "wifi.json"
DEFAULT_BIN = REPO / "build" / "balax.bin"
PBKDF2_ITERATIONS = 100_000


def load_lan_key(password_override: str | None = None) -> bytes:
    if not SECRETS_FILE.exists():
        sys.exit(f"Missing {SECRETS_FILE}. Run `tools/wifi.py secret` first.")
    cfg = json.loads(SECRETS_FILE.read_text())
    lan = cfg.get("lan_auth")
    if not lan:
        sys.exit(f"No `lan_auth` in {SECRETS_FILE}. Run `tools/wifi.py secret` and flash once over USB.")
    if password_override:
        return hashlib.pbkdf2_hmac(
            "sha256",
            password_override.encode("utf-8"),
            bytes.fromhex(lan["salt_hex"]),
            lan.get("iterations", PBKDF2_ITERATIONS),
            dklen=32,
        )
    return bytes.fromhex(lan["key_hex"])


def load_saved_ip() -> str | None:
    if SECRETS_FILE.exists():
        cfg = json.loads(SECRETS_FILE.read_text())
        return cfg.get("last_ip")
    return None


def save_last_ip(ip: str) -> None:
    if SECRETS_FILE.exists():
        cfg = json.loads(SECRETS_FILE.read_text())
        if cfg.get("last_ip") != ip:
            cfg["last_ip"] = ip
            SECRETS_FILE.write_text(json.dumps(cfg, indent=2) + "\n")


def resolve_ip(ip_arg: str | None) -> str:
    ip = ip_arg or os.environ.get("BX_IP") or load_saved_ip() or "192.168.4.1"
    return ip


def get_challenge(ip: str, timeout: float = 5.0) -> dict:
    url = f"http://{ip}/api/challenge"
    with urllib.request.urlopen(url, timeout=timeout) as resp:
        return json.loads(resp.read().decode())


def sign_request(key: bytes, nonce_hex: str, payload: bytes = b"") -> str:
    nonce = bytes.fromhex(nonce_hex)
    h = hmac.new(key, nonce, hashlib.sha256)
    if payload:
        h.update(payload)
    return h.hexdigest()


def lan_flash(ip: str, key: bytes, bin_path: Path) -> int:
    if not bin_path.exists():
        sys.exit(f"Firmware binary not found at {bin_path}. Run `tools/bx build` first.")
    firmware = bin_path.read_bytes()
    chal = get_challenge(ip)
    nonce_hex = chal["nonce"]
    sig_hex = sign_request(key, nonce_hex, firmware)

    print(
        f"Connected to {ip} (currently running {chal.get('partition')} @ {chal.get('version')}).\n"
        f"Authenticated challenge nonce: {nonce_hex[:16]}...\n"
        f"HMAC-SHA256 firmware signature: {sig_hex[:16]}...\n"
        f"Uploading {len(firmware):,} bytes over LAN..."
    )
    t0 = time.monotonic()
    req = urllib.request.Request(
        f"http://{ip}/api/ota",
        data=firmware,
        headers={
            "Content-Type": "application/octet-stream",
            "Content-Length": str(len(firmware)),
            "X-Nonce": nonce_hex,
            "X-Signature": sig_hex,
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            result = json.loads(resp.read().decode())
    except urllib.error.HTTPError as e:
        body = e.read().decode(errors="replace")
        sys.exit(f"LAN OTA rejected (HTTP {e.code}): {body}")

    dt = time.monotonic() - t0
    kbps = (len(firmware) / 1024) / max(dt, 0.01)
    print(
        f"✓ LAN OTA verified & flashed to partition '{result.get('partition')}' "
        f"in {dt:.1f}s ({kbps:.0f} KB/s). Robot is rebooting!"
    )
    save_last_ip(ip)
    return 0


def lan_cmd(ip: str, key: bytes, commands: list[str]) -> int:
    for cmd in commands:
        chal = get_challenge(ip)
        nonce_hex = chal["nonce"]
        payload = cmd.encode("utf-8")
        sig_hex = sign_request(key, nonce_hex, payload)
        req = urllib.request.Request(
            f"http://{ip}/api/cmd",
            data=payload,
            headers={
                "Content-Type": "text/plain; charset=utf-8",
                "X-Nonce": nonce_hex,
                "X-Signature": sig_hex,
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=10) as resp:
                out = resp.read().decode(errors="replace")
                print(out, end="" if not out or out.endswith("\n") else "\n")
        except urllib.error.HTTPError as e:
            body = e.read().decode(errors="replace")
            sys.exit(f"LAN command '{cmd}' rejected (HTTP {e.code}): {body}")
    save_last_ip(ip)
    return 0


def lan_monitor(ip: str, key: bytes, duration: float | None = None) -> int:
    chal = get_challenge(ip)
    print(
        f"--- LAN Monitor connected to {ip} ({chal.get('partition')} @ {chal.get('version')}) ---\n"
        f"--- Press Ctrl+C to quit ---"
    )
    save_last_ip(ip)
    since = 0
    start = time.monotonic()
    try:
        while True:
            if duration is not None and (time.monotonic() - start) >= duration:
                break
            try:
                c = get_challenge(ip, timeout=3.0)
                nonce_hex = c["nonce"]
                sig_hex = sign_request(key, nonce_hex, b"")
                req = urllib.request.Request(
                    f"http://{ip}/api/logs?since={since}",
                    headers={"X-Nonce": nonce_hex, "X-Signature": sig_hex},
                    method="GET",
                )
                with urllib.request.urlopen(req, timeout=3.0) as resp:
                    seq_hdr = resp.headers.get("X-Log-Seq")
                    if seq_hdr:
                        since = int(seq_hdr)
                    text = resp.read().decode(errors="replace")
                    if text:
                        print(text, end="", flush=True)
            except KeyboardInterrupt:
                raise
            except Exception:
                time.sleep(0.5)
            time.sleep(0.25)
    except KeyboardInterrupt:
        print("\n--- LAN Monitor disconnected ---")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--password", help="override secret password (otherwise uses derived key in secrets/wifi.json)")
    sub = parser.add_subparsers(dest="subcmd", required=True)

    p_flash = sub.add_parser("flash", help="flash firmware over LAN with HMAC-SHA256 challenge-response")
    p_flash.add_argument("ip", nargs="?", help="robot IP address (defaults to BX_IP or last used IP)")
    p_flash.add_argument("--bin", type=Path, default=DEFAULT_BIN)

    p_cmd = sub.add_parser("cmd", help="run console commands over LAN")
    p_cmd.add_argument("ip", help="robot IP address")
    p_cmd.add_argument("commands", nargs="+", help="console commands to run")

    p_mon = sub.add_parser("monitor", help="stream live logs over LAN")
    p_mon.add_argument("ip", nargs="?", help="robot IP address")
    p_mon.add_argument("--duration", type=float, help="stop after N seconds")

    args = parser.parse_args()
    key = load_lan_key(args.password)

    if args.subcmd == "flash":
        return lan_flash(resolve_ip(args.ip), key, args.bin)
    if args.subcmd == "cmd":
        return lan_cmd(args.ip, key, args.commands)
    if args.subcmd == "monitor":
        return lan_monitor(resolve_ip(args.ip), key, args.duration)
    return 1


if __name__ == "__main__":
    sys.exit(main())
