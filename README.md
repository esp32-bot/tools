# esp32-bot/tools

Shared ESP-IDF components, CLI tools, WebSerial browser flasher, and one-liner developer setup for [@esp32-bot](https://github.com/esp32-bot) robots (`balax`, `joyx`, etc.).

## 1. Zero-Install Browser Flasher (For Family & Cousins)

No software install needed! Plug the robot into USB, open Chrome/Edge, and visit:
- **BalaX Web Flasher & Live Tuner:** `https://esp32-bot.github.io/balax/`

## 2. One-Liner Developer Setup (Zed + ESP-IDF + Local Studio GUI)

```bash
curl -fsSL https://esp32-bot.github.io/tools/install.sh | bash
```

Clones `tools`, `balax`, `joyx`, and `balax-remote` into `~/gh/esp32-bot/` and launches the local **BalaX Studio** GUI dashboard.

## 3. Shared ESP-IDF Components (`components/`)

Automatically included by `~/gh/esp32-bot/balax/CMakeLists.txt` via `EXTRA_COMPONENT_DIRS`:
- `m5stickc_plus` — AXP192 power management, MPU6886 IMU, ST7789 LCD, LED, buttons, speaker, I2C
- `gfx` — RGB565 2D canvas & bitmap font renderer (compiles on both ESP32 and host laptop for PNG previews)
- `settings` — NVS-backed runtime settings with ranges and `get`/`set`/`save`/`defaults` console commands
- `console_commands` — USB/UART REPL command registration

## 4. Shared CLI & Packaging Tools (`bin/`)

- `bx` — Build, flash, monitor, run host tests, render LCD previews, package OTA firmware, and launch the local GUI
- `console.py` — Send console commands over USB without triggering the M5StickC DTR/RTS reset circuit
- `gui.py` — Zero-dependency local web studio for building, flashing, and live PID gain tuning
- `dist.py` — Package `stable` and `dev` firmware builds (`manifest.json` + binaries) for GitHub Pages WebSerial & Wi-Fi OTA
- `wifi.py` — Manage gitignored Wi-Fi credentials (`secrets/wifi.json`)
