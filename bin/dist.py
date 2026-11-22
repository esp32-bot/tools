#!/usr/bin/env python3
"""Package ESP-IDF build output into a static GitHub Pages firmware distribution (`fw/<channel>/`).

Creates:
  <out_dir>/fw/<channel>/manifest.json      # ESP Web Tools & Wi-Fi OTA metadata
  <out_dir>/fw/<channel>/balax.bin          # App binary (0x20000) for Wi-Fi OTA
  <out_dir>/fw/<channel>/balax-merged.bin   # Merged full image (0x1000..) for 1-click WebSerial flashing
  <out_dir>/fw/<channel>/bootloader.bin
  <out_dir>/fw/<channel>/partition-table.bin
  <out_dir>/fw/<channel>/ota_data_initial.bin
"""

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BUILD = REPO / "build"


def git_version() -> str:
    try:
        return (
            subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=REPO, text=True)
            .strip()
        )
    except Exception:
        return "dev"


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--channel", choices=("stable", "dev"), default="dev")
    parser.add_argument("--out", type=Path, default=REPO / "web")
    parser.add_argument("--name", default="BalaX")
    args = parser.parse_args()

    flasher_json = BUILD / "flasher_args.json"
    if not flasher_json.exists():
        sys.exit(f"{flasher_json} not found. Run `tools/bx build` first.")

    meta = json.loads(flasher_json.read_text())
    fw_dir = args.out / "fw" / args.channel
    fw_dir.mkdir(parents=True, exist_ok=True)

    parts = []
    for offset_hex, rel_path in meta["flash_files"].items():
        src = BUILD / rel_path
        if not src.exists():
            sys.exit(f"Missing build artifact: {src}")
        dst = fw_dir / Path(rel_path).name
        shutil.copy2(src, dst)
        parts.append({"path": dst.name, "offset": int(offset_hex, 16)})

    app_bin = fw_dir / "balax.bin"
    version = git_version()
    manifest = {
        "name": f"{args.name} ({args.channel})",
        "version": version,
        "channel": args.channel,
        "built_at": datetime.now(timezone.utc).isoformat(),
        "ota_url": f"fw/{args.channel}/balax.bin",
        "ota_sha256": sha256_file(app_bin) if app_bin.exists() else "",
        "new_install_prompt_erase": False,
        "builds": [
            {
                "chipFamily": "ESP32",
                "parts": sorted(parts, key=lambda p: p["offset"]),
            }
        ],
    }

    manifest_path = fw_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Packaged {args.name} [{args.channel}] ({version}) -> {manifest_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
