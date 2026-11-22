/**
 * Shared esp32-bot WebSerial Flasher & Live USB Console/Tuner widget.
 * Hosted at https://esp32-bot.github.io/tools/flasher.js
 *
 * Usage on any robot's GitHub Pages (balax, joyx, etc.):
 *   <div id="app"></div>
 *   <script type="module">
 *     import { mountEsp32BotFlasher } from "https://esp32-bot.github.io/tools/flasher.js";
 *     mountEsp32BotFlasher({ name: "BalaX", repo: "esp32-bot/BalaX", ... });
 *   </script>
 */

const ESP_WEB_TOOLS_URL = "https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module";

const STYLES = `
  :root {
    --bg: #0d1117;
    --panel: #161b22;
    --border: #30363d;
    --text: #e6edf3;
    --muted: #8b949e;
    --accent: #58a6ff;
    --success: #238636;
    --danger: #da3633;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    background: var(--bg);
    color: var(--text);
    padding: 24px;
    max-width: 1180px;
    margin-inline: auto;
  }
  .eb-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 12px;
    padding-bottom: 16px;
    border-bottom: 1px solid var(--border);
    margin-bottom: 24px;
  }
  .eb-header h1 { margin: 0; font-size: 1.5rem; display: flex; align-items: center; gap: 10px; }
  .eb-pill {
    font-size: 0.75rem;
    padding: 4px 10px;
    border-radius: 999px;
    background: #1f2937;
    border: 1px solid var(--border);
    color: var(--accent);
    text-decoration: none;
  }
  .eb-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
  @media (max-width: 920px) { .eb-grid { grid-template-columns: 1fr; } }
  .eb-card {
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 18px;
  }
  .eb-card h2 {
    margin: 0 0 12px 0;
    font-size: 1.1rem;
    display: flex;
    justify-content: space-between;
    align-items: center;
  }
  .eb-btn-row { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 12px; align-items: center; }
  .eb-root button, .eb-root select, .eb-root input[type="text"], .eb-root input[type="number"] {
    background: #21262d;
    color: var(--text);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 8px 14px;
    font-size: 0.9rem;
    cursor: pointer;
  }
  .eb-root button:hover:not(:disabled) { background: #30363d; border-color: var(--muted); }
  .eb-root button.primary { background: #1f6feb; border-color: #388bfd; font-weight: 600; }
  .eb-root button.success { background: var(--success); border-color: #2ea043; font-weight: 600; }
  .eb-root button.danger { background: var(--danger); border-color: #f85149; }
  .eb-pre {
    background: #010409;
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 12px;
    overflow: auto;
    max-height: 320px;
    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
    font-size: 0.8rem;
    line-height: 1.45;
    margin: 0;
    white-space: pre-wrap;
  }
  .eb-settings-list {
    display: flex;
    flex-direction: column;
    gap: 10px;
    max-height: 520px;
    overflow-y: auto;
  }
  .eb-setting-item {
    display: grid;
    grid-template-columns: 140px 1fr 80px auto;
    align-items: center;
    gap: 10px;
    padding: 8px;
    border: 1px solid var(--border);
    border-radius: 6px;
    background: #0d1117;
  }
  .eb-setting-name { font-weight: 600; font-size: 0.85rem; }
  .eb-setting-help { font-size: 0.75rem; color: var(--muted); grid-column: 1 / -1; }
  .eb-root input[type="range"] { width: 100%; accent-color: var(--accent); }
`;

export function mountEsp32BotFlasher(config = {}) {
  const {
    target = "#app",
    name = "ESP32-Bot",
    description = "Plug your robot into USB and click Flash. No software or drivers required.",
    repo = "esp32-bot/tools",
    remoteUrl = null,
    manifestBase = "fw",
    baudRate = 115200,
    actions = [
      { label: "Status", cmd: "status" },
      { label: "Beep", cmd: "beep" },
      { label: "Reboot", cmd: "reboot" },
    ],
  } = config;

  if (!document.querySelector(`script[src="${ESP_WEB_TOOLS_URL}"]`)) {
    const s = document.createElement("script");
    s.type = "module";
    s.src = ESP_WEB_TOOLS_URL;
    document.head.appendChild(s);
  }

  if (!document.getElementById("esp32-bot-flasher-styles")) {
    const style = document.createElement("style");
    style.id = "esp32-bot-flasher-styles";
    style.textContent = STYLES;
    document.head.appendChild(style);
  }

  const root = typeof target === "string" ? document.querySelector(target) : target;
  if (!root) throw new Error(`mountEsp32BotFlasher: target ${target} not found`);
  root.classList.add("eb-root");

  root.innerHTML = `
    <header class="eb-header">
      <h1>${name} Web Flasher & USB Tuner <span class="eb-pill">esp32-bot/tools · Zero Install</span></h1>
      <div style="display:flex; gap:8px;">
        ${remoteUrl ? `<a class="eb-pill" href="${remoteUrl}">Open Remote Control PWA →</a>` : ""}
        <a class="eb-pill" href="https://github.com/${repo}">GitHub</a>
      </div>
    </header>

    <div class="eb-grid">
      <div class="eb-card">
        <h2>1. Flash Firmware in Browser</h2>
        <p style="color:var(--muted); font-size:0.9rem; margin-top:0;">${description}</p>
        <div class="eb-btn-row">
          <label for="ebChannelSelect" style="font-size:0.88rem; color:var(--muted);">Channel:</label>
          <select id="ebChannelSelect">
            <option value="stable" selected>Stable (Recommended)</option>
            <option value="dev">Dev (Latest Main Branch)</option>
          </select>
          <esp-web-install-button id="ebInstallBtn" manifest="${manifestBase}/stable/manifest.json">
            <button slot="activate" class="primary">Flash ${name} Firmware Over USB</button>
          </esp-web-install-button>
        </div>

        <h2 style="margin-top:20px;">2. Live USB Console & Actions (WebSerial)</h2>
        <div class="eb-btn-row" id="ebActionRow">
          <button class="primary" id="ebConnectBtn">Connect USB Console</button>
        </div>
        <div style="display:flex; gap:8px; margin-bottom:12px;">
          <input type="text" id="ebCustomInput" style="flex:1;" placeholder="Type console command (e.g. set volume 50)">
          <button id="ebSendBtn">Send</button>
        </div>
        <pre class="eb-pre" id="ebSerialLog">Click "Connect USB Console" to talk to a flashed device without resetting it.</pre>
      </div>

      <div class="eb-card">
        <h2>
          <span>3. Live Settings & Gains Tuner</span>
          <div style="display:flex; gap:6px;">
            <button id="ebLoadBtn">Load Settings</button>
            <button class="success" id="ebSaveBtn">Save to Flash</button>
            <button class="danger" id="ebDefaultsBtn">Defaults</button>
          </div>
        </h2>
        <div id="ebSettingsContainer" class="eb-settings-list">
          <div style="color:var(--muted); font-size:0.9rem;">
            Connect USB Console and click <b>Load Settings</b> to tune all runtime settings live from your browser.
          </div>
        </div>
      </div>
    </div>
  `;

  const channelSelect = root.querySelector("#ebChannelSelect");
  const installBtn = root.querySelector("#ebInstallBtn");
  const connectBtn = root.querySelector("#ebConnectBtn");
  const actionRow = root.querySelector("#ebActionRow");
  const customInput = root.querySelector("#ebCustomInput");
  const sendBtn = root.querySelector("#ebSendBtn");
  const logEl = root.querySelector("#ebSerialLog");
  const settingsContainer = root.querySelector("#ebSettingsContainer");

  channelSelect.addEventListener("change", () => {
    installBtn.setAttribute("manifest", `${manifestBase}/${channelSelect.value}/manifest.json`);
  });

  for (const act of actions) {
    const btn = document.createElement("button");
    if (act.variant) btn.className = act.variant;
    btn.textContent = act.label;
    btn.addEventListener("click", () => sendCmd(act.cmd));
    actionRow.appendChild(btn);
  }

  let port = null;
  let reader = null;
  let rxBuffer = "";

  function log(msg) {
    logEl.textContent += msg;
    logEl.scrollTop = logEl.scrollHeight;
  }

  async function writeRaw(str) {
    if (!port || !port.writable) return;
    const writer = port.writable.getWriter();
    await writer.write(new TextEncoder().encode(str));
    writer.releaseLock();
  }

  async function toggleSerial() {
    if (port) {
      if (reader) await reader.cancel();
      await port.close();
      port = null;
      connectBtn.textContent = "Connect USB Console";
      log("\n[Disconnected]\n");
      return;
    }
    try {
      port = await navigator.serial.requestPort();
      await port.open({ baudRate });
      // Prevent M5StickC DTR/RTS reset circuit from rebooting the chip on connect
      await port.setSignals({ dataTerminalReady: false, requestToSend: false });
      connectBtn.textContent = "Disconnect USB";
      logEl.textContent = `[Connected to ${name} over WebSerial @ ${baudRate} baud]\n`;
      readLoop();
      await writeRaw("\x1b[1;1R\r");
      setTimeout(loadSettings, 400);
    } catch (err) {
      log(`\n[Connection error: ${err.message}]\n`);
    }
  }

  async function readLoop() {
    const decoder = new TextDecoder();
    while (port && port.readable) {
      reader = port.readable.getReader();
      try {
        while (true) {
          const { value, done } = await reader.read();
          if (done) break;
          const chunk = decoder.decode(value);
          if (chunk.includes("\x1b[6n")) {
            await writeRaw(chunk.includes("\x1b[999C") ? "\x1b[1;80R" : "\x1b[1;1R");
          }
          const clean = chunk.replace(/\x1b\[[0-9;?]*[A-Za-z]/g, "");
          rxBuffer += clean;
          log(clean);
        }
      } catch {
        break;
      } finally {
        reader.releaseLock();
      }
    }
  }

  async function sendCmd(cmd) {
    if (!port) {
      await toggleSerial();
      if (!port) return;
    }
    await writeRaw(cmd + "\r");
  }

  async function loadSettings() {
    rxBuffer = "";
    await sendCmd("get");
    setTimeout(() => {
      const re = /^(\S+)\s+(-?\d+(?:\.\d+)?)\s+(-?\d+(?:\.\d+)?)\.\.(-?\d+(?:\.\d+)?)\s+(.*)$/gm;
      const items = [];
      let m;
      while ((m = re.exec(rxBuffer)) !== null) {
        items.push({
          name: m[1],
          value: Number(m[2]),
          min: Number(m[3]),
          max: Number(m[4]),
          help: m[5],
        });
      }
      if (items.length > 0) renderSettings(items);
    }, 350);
  }

  function renderSettings(settings) {
    settingsContainer.innerHTML = "";
    for (const s of settings) {
      const step = s.max - s.min <= 2 ? 0.01 : s.max - s.min <= 100 ? 0.5 : 1;
      const div = document.createElement("div");
      div.className = "eb-setting-item";
      div.innerHTML = `
        <div class="eb-setting-name">${s.name}</div>
        <input type="range" min="${s.min}" max="${s.max}" step="${step}" value="${s.value}">
        <input type="number" min="${s.min}" max="${s.max}" step="any" value="${s.value}">
        <span style="font-size:0.75rem; color:var(--muted);">${s.min}..${s.max}</span>
        <div class="eb-setting-help">${s.help}</div>
      `;
      const rangeInput = div.querySelector('input[type="range"]');
      const numInput = div.querySelector('input[type="number"]');
      rangeInput.addEventListener("input", () => {
        numInput.value = rangeInput.value;
      });
      rangeInput.addEventListener("change", () => {
        sendCmd(`set ${s.name} ${rangeInput.value}`);
      });
      numInput.addEventListener("change", () => {
        rangeInput.value = numInput.value;
        sendCmd(`set ${s.name} ${numInput.value}`);
      });
      settingsContainer.appendChild(div);
    }
  }

  connectBtn.addEventListener("click", toggleSerial);
  sendBtn.addEventListener("click", () => {
    if (!customInput.value.trim()) return;
    sendCmd(customInput.value.trim());
    customInput.value = "";
  });
  customInput.addEventListener("keydown", (e) => {
    if (e.key === "Enter" && customInput.value.trim()) {
      sendCmd(customInput.value.trim());
      customInput.value = "";
    }
  });
  root.querySelector("#ebLoadBtn").addEventListener("click", loadSettings);
  root.querySelector("#ebSaveBtn").addEventListener("click", () => sendCmd("save"));
  root.querySelector("#ebDefaultsBtn").addEventListener("click", () => {
    sendCmd("defaults");
    setTimeout(loadSettings, 300);
  });
}
