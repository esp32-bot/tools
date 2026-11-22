#!/usr/bin/env python3
"""Local web GUI dashboard for BalaX (tools/bx gui).

Zero external dependencies beyond Python's standard library.
Provides one-click build, flash, test, screen preview, robot actions (arm/disarm/takeoff/beep/reboot),
live status reading, and live PID gain / setting sliders over USB serial.
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys
import threading
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

REPO = Path(__file__).resolve().parent.parent
BX = REPO / "tools" / "bx"
PREVIEW_PNG = REPO / "host" / "build" / "preview.png"

SETTING_LINE_RE = re.compile(
    r"^(\S+)\s+(\S+)\s+(-?\d+(?:\.\d+)?)\.\.(-?\d+(?:\.\d+)?)\s+(.*)$"
)

lock = threading.Lock()


def list_serial_ports() -> list[str]:
    ports = sorted(
        glob.glob("/dev/cu.usbserial-*") + glob.glob("/dev/cu.wchusbserial*")
    )
    return ports


def run_bx(args: list[str], port: str | None = None, timeout: int = 180) -> dict:
    env = os.environ.copy()
    if port:
        env["BX_PORT"] = port
    try:
        proc = subprocess.run(
            [str(BX), *args],
            cwd=str(REPO),
            env=env,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        return {
            "ok": proc.returncode == 0,
            "code": proc.returncode,
            "stdout": proc.stdout,
            "stderr": proc.stderr,
        }
    except subprocess.TimeoutExpired as exc:
        return {
            "ok": False,
            "code": -1,
            "stdout": exc.stdout or "",
            "stderr": f"Command timed out after {timeout}s",
        }


def parse_settings(output: str) -> list[dict]:
    items = []
    for raw_line in output.splitlines():
        line = raw_line.strip()
        m = SETTING_LINE_RE.match(line)
        if m:
            name, val, lo, hi, help_text = m.groups()
            items.append(
                {
                    "name": name,
                    "value": float(val) if "." in val else int(val),
                    "min": float(lo) if "." in lo else int(lo),
                    "max": float(hi) if "." in hi else int(hi),
                    "help": help_text,
                }
            )
    return items


HTML_PAGE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>BalaX Studio — tools/bx</title>
<style>
  :root {
    --bg: #0d1117;
    --panel: #161b22;
    --border: #30363d;
    --text: #e6edf3;
    --muted: #8b949e;
    --accent: #58a6ff;
    --success: #238636;
    --danger: #da3633;
    --warn: #d29922;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    background: var(--bg);
    color: var(--text);
    padding: 20px;
  }
  header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding-bottom: 16px;
    border-bottom: 1px solid var(--border);
    margin-bottom: 20px;
    flex-wrap: wrap;
    gap: 12px;
  }
  h1 { margin: 0; font-size: 1.4rem; display: flex; align-items: center; gap: 10px; }
  .badge {
    font-size: 0.75rem;
    padding: 3px 8px;
    border-radius: 999px;
    background: #1f2937;
    border: 1px solid var(--border);
    color: var(--accent);
  }
  .grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 20px;
  }
  @media (max-width: 960px) {
    .grid { grid-template-columns: 1fr; }
  }
  .card {
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 10px;
    padding: 16px;
  }
  .card h2 {
    margin: 0 0 12px 0;
    font-size: 1.05rem;
    color: var(--text);
    display: flex;
    justify-content: space-between;
    align-items: center;
  }
  .btn-row {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-bottom: 12px;
  }
  button, select, input[type="text"], input[type="number"] {
    background: #21262d;
    color: var(--text);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 8px 12px;
    font-size: 0.88rem;
    cursor: pointer;
    transition: background 0.15s, border-color 0.15s;
  }
  button:hover:not(:disabled) {
    background: #30363d;
    border-color: #8b949e;
  }
  button:disabled {
    opacity: 0.5;
    cursor: wait;
  }
  button.primary { background: #1f6feb; border-color: #388bfd; }
  button.primary:hover:not(:disabled) { background: #388bfd; }
  button.success { background: var(--success); border-color: #2ea043; }
  button.danger { background: var(--danger); border-color: #f85149; }
  pre {
    background: #010409;
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 12px;
    overflow: auto;
    max-height: 340px;
    font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
    font-size: 0.8rem;
    line-height: 1.45;
    margin: 0;
    white-space: pre-wrap;
  }
  .settings-list {
    display: flex;
    flex-direction: column;
    gap: 10px;
    max-height: 520px;
    overflow-y: auto;
    padding-right: 4px;
  }
  .setting-item {
    display: grid;
    grid-template-columns: 140px 1fr 80px auto;
    align-items: center;
    gap: 10px;
    padding: 8px;
    border: 1px solid var(--border);
    border-radius: 6px;
    background: #0d1117;
  }
  .setting-name { font-weight: 600; font-size: 0.85rem; }
  .setting-help { font-size: 0.75rem; color: var(--muted); grid-column: 1 / -1; }
  input[type="range"] { width: 100%; accent-color: var(--accent); }
  .cmd-bar {
    display: flex;
    gap: 8px;
    margin-top: 10px;
  }
  .cmd-bar input { flex: 1; cursor: text; }
  .preview-box {
    display: flex;
    align-items: center;
    gap: 16px;
    margin-top: 10px;
  }
  .preview-box img {
    border: 1px solid var(--border);
    border-radius: 4px;
    image-rendering: pixelated;
    height: 160px;
  }
</style>
</head>
<body>
<header>
  <h1>BalaX Studio <span class="badge">ESP-IDF v6.1.1</span> <span class="badge">tools/bx</span></h1>
  <div style="display:flex; align-items:center; gap:8px;">
    <label for="portSelect" style="font-size:0.85rem; color:var(--muted);">USB Port:</label>
    <select id="portSelect"></select>
    <button onclick="refreshPorts()">Refresh Ports</button>
  </div>
</header>

<div class="grid">
  <div class="card">
    <h2>Build, Flash & Test</h2>
    <div class="btn-row">
      <button class="primary" onclick="runTask('build')">Build Firmware</button>
      <button class="success" onclick="runTask('flash')">Build & Flash Robot</button>
      <button onclick="runTask('test')">Run Laptop Tests</button>
      <button onclick="runPreview()">Render Screen Preview</button>
    </div>

    <h2>Robot Console Actions (USB)</h2>
    <div class="btn-row">
      <button onclick="runCmd(['status'])">Read Status</button>
      <button class="success" onclick="runCmd(['arm'])">ARM</button>
      <button class="danger" onclick="runCmd(['disarm'])">DISARM</button>
      <button onclick="runCmd(['takeoff'])">Takeoff</button>
      <button onclick="runCmd(['beep'])">Beep</button>
      <button onclick="runCmd(['play', 'startup'])">Play Melody</button>
      <button onclick="runCmd(['reboot'])">Reboot</button>
    </div>

    <div class="cmd-bar">
      <input type="text" id="customCmd" placeholder="Custom console command (e.g. set volume 50)" onkeydown="if(event.key==='Enter') sendCustomCmd()">
      <button onclick="sendCustomCmd()">Send</button>
    </div>

    <div class="preview-box" id="previewBox" style="display:none;">
      <img id="previewImg" src="/api/preview.png" alt="LCD Preview">
      <div style="font-size:0.8rem; color:var(--muted);">Host-rendered M5StickC Plus LCD status screen (`host/build/preview.png`)</div>
    </div>

    <h2 style="margin-top:16px;">Output Log <button style="font-size:0.75rem; padding:3px 8px;" onclick="document.getElementById('log').textContent=''">Clear</button></h2>
    <pre id="log">Ready. Pick an action above.</pre>
  </div>

  <div class="card">
    <h2>
      <span>Live Settings & Balance Gains</span>
      <div style="display:flex; gap:6px;">
        <button onclick="loadSettings()">Load from Robot</button>
        <button class="success" onclick="runCmd(['save'])">Save to Flash (NVS)</button>
        <button class="danger" onclick="resetDefaults()">Reset Defaults</button>
      </div>
    </h2>
    <div id="settingsContainer" class="settings-list">
      <div style="color:var(--muted); font-size:0.88rem;">
        Plug in the robot over USB and click <b>Load from Robot</b> to view and tune all 16 balance gains, volume, and brightness live.
      </div>
    </div>
  </div>
</div>

<script>
const logEl = document.getElementById('log');
const portSelect = document.getElementById('portSelect');

function appendLog(title, text, ok = true) {
  const stamp = new Date().toLocaleTimeString();
  const prefix = ok ? '✓' : '✗';
  logEl.textContent = `[${stamp}] ${prefix} ${title}\\n${text.trim()}\\n\\n` + logEl.textContent;
}

function selectedPort() {
  return portSelect.value || '';
}

async function refreshPorts() {
  const res = await fetch('/api/ports');
  const data = await res.json();
  portSelect.innerHTML = '';
  if (data.ports.length === 0) {
    const opt = document.createElement('option');
    opt.value = '';
    opt.textContent = 'No USB robot detected';
    portSelect.appendChild(opt);
  } else {
    for (const p of data.ports) {
      const opt = document.createElement('option');
      opt.value = p;
      opt.textContent = p;
      portSelect.appendChild(opt);
    }
  }
}

async function setButtonsDisabled(disabled) {
  document.querySelectorAll('button').forEach(b => b.disabled = disabled);
}

async function runTask(task) {
  setButtonsDisabled(true);
  appendLog(`Running tools/bx ${task}...`, 'Please wait...');
  try {
    const res = await fetch('/api/task', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({task, port: selectedPort()})
    });
    const data = await res.json();
    appendLog(`tools/bx ${task}`, (data.stdout || '') + '\\n' + (data.stderr || ''), data.ok);
  } catch (e) {
    appendLog(`tools/bx ${task} error`, String(e), false);
  } finally {
    setButtonsDisabled(false);
  }
}

async function runPreview() {
  await runTask('preview');
  const box = document.getElementById('previewBox');
  const img = document.getElementById('previewImg');
  img.src = '/api/preview.png?t=' + Date.now();
  box.style.display = 'flex';
}

async function runCmd(commands) {
  setButtonsDisabled(true);
  try {
    const res = await fetch('/api/cmd', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({commands, port: selectedPort()})
    });
    const data = await res.json();
    appendLog(`cmd: ${commands.join(' ')}`, (data.stdout || '') + '\\n' + (data.stderr || ''), data.ok);
    return data;
  } catch (e) {
    appendLog(`cmd error`, String(e), false);
  } finally {
    setButtonsDisabled(false);
  }
}

function sendCustomCmd() {
  const input = document.getElementById('customCmd');
  const cmd = input.value.trim();
  if (!cmd) return;
  runCmd([cmd]);
  input.value = '';
}

async function loadSettings() {
  setButtonsDisabled(true);
  try {
    const res = await fetch('/api/settings?port=' + encodeURIComponent(selectedPort()));
    const data = await res.json();
    if (!data.ok) {
      appendLog('Load settings failed', (data.stdout || '') + (data.stderr || ''), false);
      return;
    }
    renderSettings(data.settings);
    appendLog('Loaded settings from robot', `${data.settings.length} settings loaded.`, true);
  } finally {
    setButtonsDisabled(false);
  }
}

async function resetDefaults() {
  await runCmd(['defaults']);
  await loadSettings();
}

function renderSettings(settings) {
  const container = document.getElementById('settingsContainer');
  container.innerHTML = '';
  for (const s of settings) {
    const item = document.createElement('div');
    item.className = 'setting-item';
    const step = (s.max - s.min) <= 2 ? 0.01 : ((s.max - s.min) <= 100 ? 0.5 : 1);
    item.innerHTML = `
      <div class="setting-name">${s.name}</div>
      <input type="range" min="${s.min}" max="${s.max}" step="${step}" value="${s.value}"
        oninput="this.nextElementSibling.value = this.value"
        onchange="updateSetting('${s.name}', this.value)">
      <input type="number" min="${s.min}" max="${s.max}" step="any" value="${s.value}"
        onchange="this.previousElementSibling.value = this.value; updateSetting('${s.name}', this.value)">
      <span style="font-size:0.75rem; color:var(--muted);">${s.min}..${s.max}</span>
      <div class="setting-help">${s.help}</div>
    `;
    container.appendChild(item);
  }
}

async function updateSetting(name, value) {
  await runCmd([`set ${name} ${value}`]);
}

refreshPorts();
</script>
</body>
</html>
"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def _send_json(self, payload: dict, status: int = HTTPStatus.OK):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/":
            body = HTML_PAGE.encode()
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif parsed.path == "/api/ports":
            self._send_json({"ports": list_serial_ports()})
        elif parsed.path == "/api/settings":
            qs = parse_qs(parsed.query)
            port = qs.get("port", [""])[0] or None
            with lock:
                res = run_bx(["cmd", "get"], port=port, timeout=15)
            res["settings"] = parse_settings(res["stdout"]) if res["ok"] else []
            self._send_json(res)
        elif parsed.path == "/api/preview.png":
            if PREVIEW_PNG.exists():
                data = PREVIEW_PNG.read_bytes()
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "image/png")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
            else:
                self.send_error(HTTPStatus.NOT_FOUND, "Preview not generated yet")
        else:
            self.send_error(HTTPStatus.NOT_FOUND)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length) or b"{}")
        port = body.get("port") or None

        if self.path == "/api/task":
            task = body.get("task", "")
            if task not in ("build", "flash", "test", "preview", "erase"):
                self._send_json({"ok": False, "stderr": f"Unknown task: {task}"}, HTTPStatus.BAD_REQUEST)
                return
            with lock:
                res = run_bx([task], port=port, timeout=300)
            self._send_json(res)
        elif self.path == "/api/cmd":
            commands = body.get("commands", [])
            if not commands or not isinstance(commands, list):
                self._send_json({"ok": False, "stderr": "No commands given"}, HTTPStatus.BAD_REQUEST)
                return
            with lock:
                res = run_bx(["cmd", *[str(c) for c in commands]], port=port, timeout=20)
            self._send_json(res)
        else:
            self.send_error(HTTPStatus.NOT_FOUND)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8420)
    parser.add_argument("--no-open", action="store_true", help="Don't open a browser tab automatically")
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    url = f"http://{args.host}:{args.port}"
    print(f"BalaX Studio running at {url} (Ctrl+C to stop)")
    if not args.no_open:
        threading.Timer(0.2, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
