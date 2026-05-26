/*
 * WebPortal.cpp v6  (Shelly + ESP32)
 * ====================================
 *
 * Changes vs. v5 (PZEM/ESP32):
 *
 *  1. WiFi.mode(WIFI_AP_STA)  — Option B: ESP32 is AP anchor for both the
 *     Shelly STA client and the user's phone.  No external router needed.
 *
 *  2. POST /api/shelly_push   — new endpoint; calls ShellyClient::ingest().
 *     Returns HTTP 200 on success, 400 on parse error.
 *
 *  3. GET /api/live           — JSON now includes "shelly_ok" boolean field.
 *
 *  4. POST /api/settings      — poll_ms whitelist updated:
 *     { 1000, 2000, 5000, 10000, 30000 } ms.
 *     200 ms and 500 ms removed (Shelly meter updates at ~1 Hz, Req 21).
 *
 *  5. PAGE_INDEX, PAGE_SETTINGS  — label "cos φ" → "pf_apparent".
 *     Settings rate buttons updated to match new whitelist.
 *
 *  6. PAGE_README  — updated to describe Shelly architecture.
 *
 *  Everything else — HTML structure, captive portal, download, reset,
 *  live plot, mDNS, DNS catch-all, all other routes — is UNCHANGED.
 */

#include "WebPortal.h"

// ─────────────────────────────────────────────────────────────────────────────
// PAGE_INDEX  — identical structure to v5; only label change
// ─────────────────────────────────────────────────────────────────────────────
static const char PAGE_INDEX[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="de"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PZEM Logger</title>
<style>
  body { font-family: sans-serif; max-width: 600px; margin: 1em auto;
         padding: 1em; background: #f5f5f5; color: #222; }
  h1 { color: #000; }
  .card { background: white; border-radius: 8px; padding: 1em;
          margin-bottom: 1em; box-shadow: 0 1px 3px rgba(0,0,0,.1); }
  .live { font-size: 2.5em; font-weight: bold; color: #007acc;
          text-align: center; margin: .3em 0; }
  .sub-live { font-size: 1.2em; font-weight: normal; color: #444;
              text-align: center; margin-bottom: 0.5em; }
  .label { text-align: center; color: #666; font-weight: bold; }
  .btn-row { display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: .6em; }
  button { padding: .9em; font-size: 1em; border: none; border-radius: 6px;
           background: #007acc; color: white; cursor: pointer; }
  button:hover { background: #005f99; }
  button.danger { background: #d33; }
  button.danger:hover { background: #a22; }
  button.muted { background: #888; }
  .status { font-size: .9em; color: #666; margin-top: .8em; text-align: center; }
  .status span { display: inline-block; padding: .1em .5em;
                 border-radius: 3px; margin-right: .3em; }
  .ok  { background: #cfc; color: #060; }
  .err { background: #fcc; color: #800; }
</style>
</head><body>

<h1>BRAUN Shelly Power Logger</h1>

<div class="card">
  <div class="label">Aktuelle Werte</div>
  <div class="live"><span id="power">—</span> W</div>
  <div class="sub-live">
    <span id="voltage">—</span> V &nbsp;|&nbsp; pf<sub>apparent</sub>: <span id="pf">—</span>
  </div>
  <hr style="border: 0; border-top: 1px solid #eee; margin: 1em 0;">
  <div class="status">
    Shelly: <span id="shelly-status" class="ok">?</span>
    SD:     <span id="sd-status"     class="ok">?</span><br><br>
    Puffer: <span id="buf">0</span>
    | Verworfen: <span id="drop">0</span>
    | Uptime: <span id="uptime">0</span> s
  </div>
</div>

<div class="card">
  <div class="btn-row">
    <button onclick="location.href='/liveplot'" style="background: #28a745;">📈 Live Plot</button>
    <button onclick="location.href='/download'">Download Log Files</button>
    <button class="danger" onclick="confirmReset()">Reset &amp; Delete SD</button>
    <button class="muted" onclick="location.href='/settings'">Settings</button>
    <button class="muted" onclick="location.href='/readme'">Read Me</button>
  </div>
</div>

<script>
function confirmReset() {
  if (!confirm('Wirklich alle Log-Daten auf der SD-Karte löschen?')) return;
  fetch('/reset', { method: 'POST' })
    .then(r => r.text())
    .then(t => alert(t))
    .catch(e => alert('Fehler: ' + e));
}

async function refresh() {
  try {
    const r = await fetch('/api/live');
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const d = await r.json();
    document.getElementById('power').textContent   = d.power   === null ? '—' : d.power.toFixed(1);
    document.getElementById('voltage').textContent = d.voltage === null ? '—' : d.voltage.toFixed(1);
    document.getElementById('pf').textContent      = d.pf      === null ? '—' : d.pf.toFixed(2);
    document.getElementById('buf').textContent     = d.buffer;
    document.getElementById('drop').textContent    = d.dropped;
    document.getElementById('uptime').textContent  = d.uptime;
    const se = document.getElementById('shelly-status');
    se.textContent = d.shelly_ok ? 'OK' : 'FEHLER';
    se.className   = d.shelly_ok ? 'ok' : 'err';
    const sd = document.getElementById('sd-status');
    sd.textContent = d.sd_ok ? 'OK' : 'FEHLER';
    sd.className   = d.sd_ok   ? 'ok' : 'err';
  } catch (e) {
    document.getElementById('power').textContent   = '—';
    document.getElementById('voltage').textContent = '—';
    document.getElementById('pf').textContent      = '—';
  }
}
refresh();
setInterval(refresh, 1000);
</script>
</body></html>
)HTML";

// ─────────────────────────────────────────────────────────────────────────────
// PAGE_SETTINGS  — rate buttons updated; pf label updated
// ─────────────────────────────────────────────────────────────────────────────
static const char PAGE_SETTINGS[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Settings – PZEM Logger</title>
<style>
  body { font-family: sans-serif; max-width: 600px; margin: 1em auto;
         padding: 1em; background: #f5f5f5; color: #222; }
  h1   { color: #000; }
  .card { background: white; border-radius: 8px; padding: 1.2em;
          margin-bottom: 1em; box-shadow: 0 1px 3px rgba(0,0,0,.1); }
  h2   { margin: 0 0 1em; font-size: 1.1em; color: #444; }
  .grid-container {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(100px, 1fr));
    gap: .5em;
    margin-bottom: 1em;
  }
  .setting-btn {
    padding: .7em .3em; font-size: .95em; border: 2px solid #ccc;
    border-radius: 6px; background: white; color: #333; cursor: pointer;
    text-align: center; transition: border-color .15s, background .15s;
  }
  .setting-btn:hover  { border-color: #007acc; }
  .setting-btn.active { border-color: #007acc; background: #e8f4ff;
                        color: #007acc; font-weight: bold; }
  .current { font-size: .9em; color: #666; margin-bottom: 1.2em; }
  .current span { font-weight: bold; color: #007acc; }
  .save-row { display: flex; gap: .6em; align-items: center; margin-top: 1.5em; }
  button.primary { padding: .8em 1.6em; font-size: 1em; border: none;
                   border-radius: 6px; background: #007acc; color: white; cursor: pointer; }
  button.primary:hover    { background: #005f99; }
  button.primary:disabled { background: #aaa; cursor: default; }
  .msg { font-size: .9em; padding: .4em .8em; border-radius: 4px; display: none; }
  .msg.ok  { background: #cfc; color: #060; display: inline-block; }
  .msg.err { background: #fcc; color: #800; display: inline-block; }
  a.back   { display: inline-block; margin-top: .5em; color: #007acc; text-decoration: none; }
  a.back:hover { text-decoration: underline; }
  .note { font-size: .85em; color: #888; margin-top: .5em; }
</style>
</head><body>

<h1>Settings</h1>

<div class="card">
  <h2>Sampling Rate</h2>
  <p class="current">Current rate: <span id="cur-rate">…</span></p>
  <!-- 200 ms / 500 ms removed: Shelly meter updates at ~1 Hz (Req 21) -->
  <div class="grid-container" id="rate-grid">
    <div class="setting-btn rate-btn" data-ms="1000" >1 / s</div>
    <div class="setting-btn rate-btn" data-ms="2000" >0.5 / s</div>
    <div class="setting-btn rate-btn" data-ms="5000" >0.2 / s</div>
    <div class="setting-btn rate-btn" data-ms="10000">1 / 10 s</div>
    <div class="setting-btn rate-btn" data-ms="30000">1 / 30 s</div>
  </div>

  <hr style="border: 0; border-top: 1px solid #eee; margin: 1.5em 0;">

  <h2>Logging Power Threshold</h2>
  <p class="current">Log data if power exceeds: <span id="cur-thresh">…</span></p>
  <div class="grid-container" id="thresh-grid">
    <div class="setting-btn thresh-btn" data-w="0">0 W (No Limit)</div>
    <div class="setting-btn thresh-btn" data-w="1">1 W</div>
    <div class="setting-btn thresh-btn" data-w="2">2 W</div>
    <div class="setting-btn thresh-btn" data-w="5">5 W</div>
    <div class="setting-btn thresh-btn" data-w="10">10 W</div>
    <div class="setting-btn thresh-btn" data-w="20">20 W</div>
    <div class="setting-btn thresh-btn" data-w="50">50 W</div>
  </div>

  <div class="save-row">
    <button class="primary" id="save-btn" onclick="saveSettings()" disabled>Apply Settings</button>
    <span class="msg" id="msg"></span>
  </div>
  <p class="note">Changes take effect immediately and are kept until the device is restarted.</p>
</div>

<a class="back" href="/">← Back</a>

<script>
let selectedMs = null;
let selectedThresh = null;

async function loadSettings() {
  try {
    const r = await fetch('/api/settings');
    const d = await r.json();
    selectedMs    = d.poll_ms;
    selectedThresh = d.power_threshold;
    updateUI();
  } catch(e) {
    document.getElementById('cur-rate').textContent   = 'unknown';
    document.getElementById('cur-thresh').textContent = 'unknown';
  }
}

function msToLabel(ms) {
  const map = {1000:'1 / s', 2000:'0.5 / s', 5000:'0.2 / s',
               10000:'1 / 10 s', 30000:'1 / 30 s'};
  return map[ms] || (ms + ' ms');
}

function updateUI() {
  document.getElementById('cur-rate').textContent =
    msToLabel(selectedMs);
  document.getElementById('cur-thresh').textContent =
    selectedThresh === 0 ? '0 W (No Limit)' : selectedThresh + ' W';
  document.querySelectorAll('.rate-btn').forEach(b => {
    b.classList.toggle('active', parseInt(b.dataset.ms) === selectedMs);
  });
  document.querySelectorAll('.thresh-btn').forEach(b => {
    b.classList.toggle('active', parseInt(b.dataset.w) === selectedThresh);
  });
}

document.getElementById('rate-grid').addEventListener('click', e => {
  const btn = e.target.closest('.rate-btn');
  if (!btn) return;
  selectedMs = parseInt(btn.dataset.ms);
  updateUI();
  document.getElementById('save-btn').disabled = false;
  setMsg('', '');
});

document.getElementById('thresh-grid').addEventListener('click', e => {
  const btn = e.target.closest('.thresh-btn');
  if (!btn) return;
  selectedThresh = parseInt(btn.dataset.w);
  updateUI();
  document.getElementById('save-btn').disabled = false;
  setMsg('', '');
});

async function saveSettings() {
  if (selectedMs === null || selectedThresh === null) return;
  document.getElementById('save-btn').disabled = true;
  try {
    const r = await fetch('/api/settings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'poll_ms=' + selectedMs + '&power_threshold=' + selectedThresh
    });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const d = await r.json();
    selectedMs     = d.poll_ms;
    selectedThresh = d.power_threshold;
    updateUI();
    setMsg('Saved!', 'ok');
  } catch(e) {
    setMsg('Error: ' + e.message, 'err');
    document.getElementById('save-btn').disabled = false;
  }
}

function setMsg(text, cls) {
  const el = document.getElementById('msg');
  el.textContent = text;
  el.className = 'msg ' + cls;
}

loadSettings();
</script>
</body></html>
)HTML";

// ─────────────────────────────────────────────────────────────────────────────
// PAGE_LIVEPLOT  — minimum fetch interval raised to 1000 ms (Req 24 / Req 21)
// ─────────────────────────────────────────────────────────────────────────────
static const char PAGE_LIVEPLOT[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="de"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Live Scope – Shelly Logger</title>
<style>
  body { font-family: sans-serif; max-width: 650px; margin: 1em auto; padding: 1em;
         background: #f5f5f5; color: #222; }
  h1 { color: #000; margin-bottom: 0.5em; }
  .card { background: white; border-radius: 8px; padding: 1.2em; margin-bottom: 1em;
          box-shadow: 0 1px 3px rgba(0,0,0,.1); }
  .metrics-row { display: flex; justify-content: space-between; font-weight: bold;
                 margin-bottom: 0.8em; font-size: 1.1em; color: #007acc; }
  canvas { background: #111116; border-radius: 6px; width: 100%; display: block;
           box-shadow: inset 0 0 10px rgba(0,0,0,0.5); }
  a.back { display: inline-block; margin-top: .5em; color: #007acc;
           text-decoration: none; font-weight: bold; }
  a.back:hover { text-decoration: underline; }
  .legend { font-size: 0.85em; color: #666; margin-top: 0.5em; text-align: center; }
</style>
</head><body>
<h1>Echtzeit Leistungsprofil</h1>
<div class="card">
  <div class="metrics-row">
    <div>Aktuell: <span id="val-w">—</span> W</div>
    <div>Spannung: <span id="val-v">—</span> V</div>
    <div>Max: <span id="val-max">—</span> W</div>
  </div>
  <canvas id="scopeCanvas" width="600" height="280"></canvas>
  <div class="legend">Timeline-Profil — Automatische Skalierung der Ordinate</div>
</div>
<a class="back" href="/">← Zurück</a>

<script>
const canvas = document.getElementById('scopeCanvas');
const ctx    = canvas.getContext('2d');
const powerData   = [];
const maxDataPoints = 60;
let   fetchIntervalMs = 1000;  // floor raised to 1000 ms (Shelly meter cadence)

async function initScope() {
  try {
    const r = await fetch('/api/settings');
    const d = await r.json();
    if (d.poll_ms) {
      fetchIntervalMs = Math.max(d.poll_ms, 1000);  // enforce 1 s minimum
    }
  } catch(e) { /* keep default */ }
  startScopeLoop();
}

function startScopeLoop() {
  setInterval(async () => {
    try {
      const r = await fetch('/api/live');
      if (!r.ok) return;
      const d = await r.json();
      const p = (d.power === null) ? 0.0 : d.power;
      const v = (d.voltage === null) ? 0.0 : d.voltage;
      document.getElementById('val-w').textContent = p.toFixed(1);
      document.getElementById('val-v').textContent = Math.round(v);
      powerData.push(p);
      if (powerData.length > maxDataPoints) powerData.shift();
      renderChart();
    } catch(e) {
      document.getElementById('val-w').textContent = '—';
    }
  }, fetchIntervalMs);
}

function renderChart() {
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  if (powerData.length === 0) return;
  let maxObs   = Math.max(...powerData, 10);
  let scaleMax = Math.ceil(maxObs * 1.15 / 10) * 10;
  document.getElementById('val-max').textContent = scaleMax;
  const pL = 55, pB = 25, pT = 15, pR = 15;
  const gW = canvas.width - pL - pR;
  const gH = canvas.height - pT - pB;
  ctx.strokeStyle = '#33333f'; ctx.lineWidth = 1;
  ctx.strokeRect(pL, pT, gW, gH);
  ctx.font = '11px monospace'; ctx.fillStyle = '#8e8e93';
  ctx.strokeStyle = '#22222a';
  ctx.textAlign = 'right'; ctx.textBaseline = 'middle';
  for (let i = 0; i <= 4; i++) {
    let pct  = i / 4;
    let yPos = pT + gH * pct;
    if (i > 0 && i < 4) {
      ctx.beginPath(); ctx.moveTo(pL, yPos); ctx.lineTo(canvas.width - pR, yPos); ctx.stroke();
    }
    ctx.fillText(Math.round(scaleMax * (1 - pct)) + 'W', pL - 8, yPos);
  }
  ctx.textAlign = 'center'; ctx.textBaseline = 'top';
  let totalSec = Math.round(((maxDataPoints - 1) * fetchIntervalMs) / 1000);
  ctx.fillStyle = '#8e8e93';
  ctx.fillText('-' + totalSec + 's', pL, pT + gH + 6);
  ctx.fillText('-' + Math.round(totalSec / 2) + 's', pL + gW / 2, pT + gH + 6);
  ctx.fillStyle = '#28a745';
  ctx.fillText('Jetzt', canvas.width - pR, pT + gH + 6);
  ctx.strokeStyle = '#28a745'; ctx.lineWidth = 2.5;
  ctx.lineJoin = 'round'; ctx.lineCap = 'round';
  ctx.beginPath();
  for (let idx = 0; idx < powerData.length; idx++) {
    const x = pL + (gW / (maxDataPoints - 1)) * idx;
    const y = pT + gH - (powerData[idx] / scaleMax) * gH;
    idx === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
  }
  ctx.stroke();
  if (powerData.length > 1) {
    let lastX = pL + (gW / (maxDataPoints - 1)) * (powerData.length - 1);
    ctx.lineTo(lastX, pT + gH); ctx.lineTo(pL, pT + gH); ctx.closePath();
    ctx.fillStyle = 'rgba(40,167,69,0.06)'; ctx.fill();
  }
}
initScope();
</script>
</body></html>
)HTML";

// ─────────────────────────────────────────────────────────────────────────────
// PAGE_README  — updated for Shelly architecture
// ─────────────────────────────────────────────────────────────────────────────
static const char PAGE_README[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><title>Read Me – Shelly Logger</title>
<style>
  body { font-family: sans-serif; max-width: 600px; margin: 2em auto;
         padding: 1em; line-height: 1.5; }
  a { color: #007acc; }
  code { background: #f0f0f0; padding: .1em .4em; border-radius: 3px; }
</style></head><body>
<h1>BBRAUN Shelly Power Logger V1</h1>

<p><strong>Hardware:</strong></p>
<ul>
  <li>Shelly Plug S MTR Gen3 (CE-marked, 16 A) — measures voltage, power, current</li>
  <li>ESP32-WROOM-32 — logger, web server, SD writer</li>
  <li>MicroSD card — permanent log storage</li>
  <li>Certified 230 V → 5 V DC PSU — powers ESP32 only</li>
</ul>

<p><strong>Functions:</strong></p>
<ul>
  <li>Logs: <code>time_ms, voltage_V, power_W, pf_apparent</code></li>
  <li>pf_apparent = active power / (V × I) — derived value, accurate for resistive loads</li>
  <li>Adjustable poll/sampling frequency: 1, 2, 5, 10, 30 s</li>
  <li>Adjustable power threshold for start of log</li>
  <li>Default: polls every 1 s, writes to SD every 10 s</li>
  <li>Live data via Wi-Fi (ESP32 access point: <code>PZEM_Logger</code>)</li>
</ul>

<p><strong>First-time setup:</strong></p>
<ol>
  <li>Power on the ESP32 logger — AP <code>PZEM_Logger</code> / <code>logger1234</code> appears</li>
  <li>Power on the Shelly — it should already be provisioned to join <code>PZEM_Logger</code></li>
  <li>Connect your phone to <code>PZEM_Logger</code></li>
  <li>Browse to <a href="http://braun_PZEM.local">http://braun_PZEM.local</a>
      or <a href="http://192.168.4.1">http://192.168.4.1</a></li>
</ol>

<p><strong>Contact:</strong> jan.pfrang@delonghigroup.com</p>
<p><a href="/">← Back</a></p>
</body></html>
)HTML";

// ─────────────────────────────────────────────────────────────────────────────
// Implementation
// ─────────────────────────────────────────────────────────────────────────────

WebPortal::WebPortal(Logger& logger, ShellyClient& shelly)
  : _logger(logger), _shelly(shelly), _server(HTTP_PORT) {}

bool WebPortal::begin() {
  Serial.println("[Web] Starte WLAN AP+STA...");

  // Option B: AP+STA mode.
  // softAP is the network anchor for both the Shelly and the user's phone.
  // STA is used only if a future feature needs to reach out (e.g., NTP).
  // For now STA is started but not connected to any external network.
  WiFi.mode(WIFI_AP_STA);

  if (!WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD)) {
    Serial.println("[Web] softAP() fehlgeschlagen!");
    return false;
  }
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[Web] AP '%s' aktiv, IP: %s\n", WIFI_AP_SSID, ip.toString().c_str());

  // Captive portal DNS catch-all
  _dns.start(DNS_PORT, "*", ip);
  Serial.println("[Web] DNS-Server gestartet (catch-all)");

  // mDNS — braun_PZEM.local  (Req: plain-text hostname)
  if (MDNS.begin(WIFI_AP_HOSTNAME)) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    Serial.printf("[Web] mDNS aktiv: http://%s.local\n", WIFI_AP_HOSTNAME);
  }

  // ── Captive portal probes ──────────────────────────────────────────────
  _server.on("/generate_204",              HTTP_GET, [this](){ handleCaptivePortal(); });
  _server.on("/gen_204",                   HTTP_GET, [this](){ handleCaptivePortal(); });
  _server.on("/hotspot-detect.html",       HTTP_GET, [this](){ handleCaptivePortal(); });
  _server.on("/library/test/success.html", HTTP_GET, [this](){ handleCaptivePortal(); });
  _server.on("/ncsi.txt",                  HTTP_GET, [this](){ handleCaptivePortal(); });
  _server.on("/connecttest.txt",           HTTP_GET, [this](){ handleCaptivePortal(); });
  _server.on("/redirect",                  HTTP_GET, [this](){ handleCaptivePortal(); });
  _server.on("/canonical.html",            HTTP_GET, [this](){ handleCaptivePortal(); });

  // ── Application routes (unchanged from v5 except /api/shelly_push) ──────
  _server.on("/",                    HTTP_GET,  [this](){ handleRoot(); });
  _server.on("/api/live",            HTTP_GET,  [this](){ handleApiLive(); });
  _server.on("/api/settings",        HTTP_GET,  [this](){ handleApiSettings(); });
  _server.on("/api/settings",        HTTP_POST, [this](){ handleApiSettingsSave(); });
  _server.on("/api/shelly_push",     HTTP_POST, [this](){ handleShellyPush(); });   // NEW
  _server.on("/download",            HTTP_GET,  [this](){ handleDownload(); });
  _server.on("/reset",               HTTP_POST, [this](){ handleReset(); });
  _server.on("/settings",            HTTP_GET,  [this](){ handleSettings(); });
  _server.on("/readme",              HTTP_GET,  [this](){ handleReadme(); });
  _server.on("/liveplot",            HTTP_GET,  [this](){ handleLivePlot(); });
  _server.onNotFound(                          [this](){ handleNotFound(); });

  _server.begin();
  Serial.println("[Web] HTTP-Server gestartet");
  return true;
}

void WebPortal::update() {
  _dns.processNextRequest();
  _server.handleClient();
}

// ─────────────────────────────────────────────────────────────────────────────
// Handlers
// ─────────────────────────────────────────────────────────────────────────────

void WebPortal::handleRoot() {
  _server.send_P(200, "text/html", PAGE_INDEX);
}

void WebPortal::handleCaptivePortal() {
  _server.sendHeader("Location", "http://192.168.4.1/", true);
  _server.send(302, "text/plain", "");
}

// ── NEW: receives push from shelly_push.js ────────────────────────────────
void WebPortal::handleShellyPush() {
  String body = _server.arg("plain");   // raw POST body
  if (body.length() == 0) {
    _server.send(400, "application/json", "{\"error\":\"empty body\"}");
    return;
  }
  if (_shelly.ingest(body)) {
    _server.send(200, "application/json", "{\"ok\":true}");
  } else {
    _server.send(400, "application/json", "{\"error\":\"parse failed\"}");
  }
}

// ── /api/live — now includes shelly_ok ───────────────────────────────────
void WebPortal::handleApiLive() {
  char buf[API_BUFFER_SIZE];
  float p  = _logger.getLastPower();
  float v  = _logger.getLastVoltage();
  float pf = _logger.getLastPf();

  if (isnan(p) || isnan(v) || isnan(pf)) {
    snprintf(buf, sizeof(buf),
      "{\"power\":null,\"voltage\":null,\"pf\":null,"
      "\"buffer\":%u,\"dropped\":%lu,\"uptime\":%lu,"
      "\"shelly_ok\":%s,\"sd_ok\":%s}",
      (unsigned)_logger.getBufferCount(),
      (unsigned long)_logger.getDroppedSamples(),
      (unsigned long)(millis() / 1000),
      _logger.shellyOk() ? "true" : "false",
      _logger.sdOk()     ? "true" : "false");
  } else {
    snprintf(buf, sizeof(buf),
      "{\"power\":%.1f,\"voltage\":%.1f,\"pf\":%.2f,"
      "\"buffer\":%u,\"dropped\":%lu,\"uptime\":%lu,"
      "\"shelly_ok\":%s,\"sd_ok\":%s}",
      p, v, pf,
      (unsigned)_logger.getBufferCount(),
      (unsigned long)_logger.getDroppedSamples(),
      (unsigned long)(millis() / 1000),
      _logger.shellyOk() ? "true" : "false",
      _logger.sdOk()     ? "true" : "false");
  }
  _server.send(200, "application/json", buf);
}

void WebPortal::handleDownload() {
  _logger.flushToSD();
  File f = _logger.openLogFileForRead();
  if (!f) {
    _server.send(404, "text/plain", "Log-Datei nicht gefunden oder SD-Fehler.");
    return;
  }
  _server.sendHeader("Content-Type", "text/csv");
  _server.sendHeader("Content-Disposition", "attachment; filename=log.csv");
  _server.streamFile(f, "text/csv");
  f.close();
}

void WebPortal::handleReset() {
  if (_logger.resetSDFile()) {
    _server.send(200, "text/plain", "Log-Datei wurde gelöscht und neu angelegt.");
  } else {
    _server.send(500, "text/plain", "Fehler beim Löschen der Log-Datei (SD nicht verfügbar?).");
  }
}

void WebPortal::handleSettings() {
  _server.send_P(200, "text/html", PAGE_SETTINGS);
}

void WebPortal::handleLivePlot() {
  _server.send_P(200, "text/html", PAGE_LIVEPLOT);
}

// ── GET /api/settings ─────────────────────────────────────────────────────
void WebPortal::handleApiSettings() {
  char buf[128];
  snprintf(buf, sizeof(buf),
           "{\"poll_ms\":%lu,\"power_threshold\":%d}",
           (unsigned long)_logger.getPollInterval(),
           (int)_logger.getPowerThreshold());
  _server.send(200, "application/json", buf);
}

// ── POST /api/settings — poll_ms whitelist updated (200/500 removed) ──────
void WebPortal::handleApiSettingsSave() {
  if (_server.hasArg("poll_ms")) {
    uint32_t ms = (uint32_t)_server.arg("poll_ms").toInt();
    // Req 21: minimum 1000 ms; no sub-second options
    const uint32_t allowed_ms[] = {1000, 2000, 5000, 10000, 30000};
    bool valid = false;
    for (auto v : allowed_ms) { if (ms == v) { valid = true; break; } }
    if (valid) {
      _logger.setPollInterval(ms);
      Serial.printf("[Web] Poll-Intervall gesetzt: %lu ms\n", (unsigned long)ms);
    } else {
      _server.send(400, "application/json", "{\"error\":\"invalid poll_ms\"}");
      return;
    }
  }

  if (_server.hasArg("power_threshold")) {
    int thresh = _server.arg("power_threshold").toInt();
    const int allowed_w[] = {0, 1, 2, 5, 10, 20, 50};
    bool valid = false;
    for (auto v : allowed_w) { if (thresh == v) { valid = true; break; } }
    if (valid) {
      _logger.setPowerThreshold((float)thresh);
      Serial.printf("[Web] Power-Threshold gesetzt: %d W\n", thresh);
    } else {
      _server.send(400, "application/json", "{\"error\":\"invalid power_threshold\"}");
      return;
    }
  }

  handleApiSettings();   // echo current settings back
}

void WebPortal::handleReadme() {
  _server.send_P(200, "text/html", PAGE_README);
}

void WebPortal::handleNotFound() {
  handleCaptivePortal();
}
