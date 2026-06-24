/*
 * WebPortal.cpp v8  (Shelly + ESP32 + OTA + batch push)
 * ======================================================
 *
 * Changes vs. v7:
 *
 *  1. handleShellyPush() — detects {"batch":[…]} payloads from
 *     shelly_push.js v2 and routes them to _shelly.ingestBatch().
 *     Single-sample payloads {"ts":…,"v":…,…} continue to use
 *     _shelly.ingest() as before — no behaviour change for normal
 *     steady-state operation.
 *
 *     Detection: if the raw body contains the key "batch" it is
 *     treated as a batch payload.  This is a fast string check
 *     (strstr) that avoids a full JSON parse just for routing.
 *
 *  Everything else -- unchanged from v7.
 */

#include "WebPortal.h"

// -----------------------------------------------------------------------------
// PAGE_INDEX  -- identical structure to v5; only label change
// -----------------------------------------------------------------------------
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
  .banner { display: none; background: #fff3cd; color: #856404;
            border: 1px solid #ffc107; border-radius: 6px;
            padding: .7em 1em; margin-bottom: 1em;
            font-weight: bold; text-align: center; }
</style>
</head><body>

<h1>BRAUN Shelly Power Logger</h1>

<div id="ota-banner" class="banner">&#x2B06; Firmware-Update l&auml;uft &mdash; bitte warten&hellip;</div>

<div class="card">
  <div class="label">Aktuelle Werte</div>
  <div class="live"><span id="power">&mdash;</span> W</div>
  <div class="sub-live">
    <span id="voltage">&mdash;</span> V &nbsp;|&nbsp; pf<sub>apparent</sub>: <span id="pf">&mdash;</span>
  </div>
  <hr style="border: 0; border-top: 1px solid #eee; margin: 1em 0;">
  <div class="status">
    Shelly: <span id="shelly-status" class="ok">?</span>
    SD:     <span id="sd-status"     class="ok">?</span>
    Versorg: <span id="supply-status" class="ok">?</span><br><br>
    Puffer: <span id="buf">0</span>
    | Verworfen: <span id="drop">0</span>
    | Uptime: <span id="uptime">0</span> s
  </div>
</div>

<div class="card">
  <div class="btn-row">
    <button onclick="location.href='/liveplot'" style="background: #28a745;">&#x1F4C8; Live Plot</button>
    <button onclick="location.href='/histogram'" style="background: #17a2b8;">&#x1F4CA; Histogram</button>
    <button onclick="location.href='/download'">Download Log Files</button>
    <button class="danger" onclick="confirmReset()">Reset &amp; Delete SD</button>
    <button class="muted" onclick="location.href='/settings'">Settings</button>
    <button class="muted" onclick="location.href='/readme'">Read Me</button>
    <button class="muted" onclick="location.href='/update'">&#x2B06; Firmware Update</button>
  </div>
</div>

<script>
function confirmReset() {
  if (!confirm('Wirklich alle Log-Daten auf der SD-Karte l&ouml;schen?')) return;
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
    document.getElementById('power').textContent   = d.power   === null ? '&mdash;' : d.power.toFixed(1);
    document.getElementById('voltage').textContent = d.voltage === null ? '&mdash;' : d.voltage.toFixed(1);
    document.getElementById('pf').textContent      = d.pf      === null ? '&mdash;' : d.pf.toFixed(2);
    document.getElementById('buf').textContent     = d.buffer;
    document.getElementById('drop').textContent    = d.dropped;
    document.getElementById('uptime').textContent  = d.uptime;
    const se = document.getElementById('shelly-status');
    se.textContent = d.shelly_ok ? 'OK' : 'FEHLER';
    se.className   = d.shelly_ok ? 'ok' : 'err';
    const sd = document.getElementById('sd-status');
    sd.textContent = d.sd_ok ? 'OK' : 'FEHLER';
    sd.className   = d.sd_ok   ? 'ok' : 'err';
    const sv = document.getElementById('supply-status');
    if (d.supply_mv === 0) {
      sv.textContent = 'N/A';
      sv.className   = 'ok';
    } else {
      const supplyV = (d.supply_mv / 1000.0).toFixed(1);
      sv.textContent = supplyV + ' V';
      sv.className   = d.supply_ok ? 'ok' : 'err';
    }
    document.getElementById('ota-banner').style.display =
      d.ota_active ? 'block' : 'none';
  } catch (e) {
    document.getElementById('power').textContent   = '&mdash;';
    document.getElementById('voltage').textContent = '&mdash;';
    document.getElementById('pf').textContent      = '&mdash;';
  }
}
refresh();
setInterval(refresh, 1000);
</script>
</body></html>
)HTML";

// -----------------------------------------------------------------------------
// PAGE_SETTINGS  -- rate buttons updated; pf label updated
// -----------------------------------------------------------------------------
static const char PAGE_SETTINGS[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Settings &ndash; PZEM Logger</title>
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
  <p class="current">Current rate: <span id="cur-rate">&hellip;</span></p>
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
  <p class="current">Log data if power exceeds: <span id="cur-thresh">&hellip;</span></p>
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

<a class="back" href="/">&larr; Back</a>

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

// -----------------------------------------------------------------------------
// PAGE_LIVEPLOT  -- minimum fetch interval raised to 1000 ms (Req 24 / Req 21)
// -----------------------------------------------------------------------------
static const char PAGE_LIVEPLOT[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="de"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Live Scope &ndash; Shelly Logger</title>
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
    <div>Aktuell: <span id="val-w">&mdash;</span> W</div>
    <div>Spannung: <span id="val-v">&mdash;</span> V</div>
    <div>Max: <span id="val-max">&mdash;</span> W</div>
  </div>
  <canvas id="scopeCanvas" width="600" height="280"></canvas>
  <div class="legend">Timeline-Profil &mdash; Automatische Skalierung der Ordinate</div>
</div>
<a class="back" href="/">&larr; Zur&uuml;ck</a>

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
      document.getElementById('val-w').textContent = '&mdash;';
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

// -----------------------------------------------------------------------------
// PAGE_README  -- updated for Shelly architecture
// -----------------------------------------------------------------------------
static const char PAGE_README[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><title>Read Me &ndash; Shelly Logger</title>
<style>
  body { font-family: sans-serif; max-width: 640px; margin: 2em auto;
         padding: 1em; line-height: 1.6; color: #222; }
  h1 { font-size: 1.4em; margin-bottom: 0.2em; }
  h2 { font-size: 1.05em; margin: 1.4em 0 0.4em; border-bottom: 1px solid #ddd;
       padding-bottom: 0.2em; color: #333; }
  a  { color: #007acc; }
  code { background: #f0f0f0; padding: .1em .4em; border-radius: 3px;
         font-size: 0.92em; }
  ul, ol { margin: 0.4em 0 0.8em; padding-left: 1.4em; }
  li { margin-bottom: 0.3em; }
  table { border-collapse: collapse; width: 100%; margin: 0.6em 0; font-size: 0.93em; }
  th { background: #f0f0f0; text-align: left; padding: 0.4em 0.6em;
       border: 1px solid #ccc; }
  td { padding: 0.35em 0.6em; border: 1px solid #ddd; vertical-align: top; }
  .note { background: #fffbe6; border-left: 3px solid #f0c040;
          padding: 0.5em 0.8em; border-radius: 3px; font-size: 0.9em;
          margin: 0.8em 0; }
</style></head><body>

<h1>BRAUN Shelly Power Logger V1</h1>
<p>Portable power logger for 230 V appliances. The Shelly Plug S measures
voltage, current and active power; the ESP32 records and serves the data.</p>

<h2>Hardware</h2>
<ul>
  <li><strong>Shelly Plug S MTR Gen3</strong> &mdash; CE-marked, max 16 A / 3680 W.
      Measures voltage, current, active power. Pushes data to the ESP32 every 1 s.</li>
  <li><strong>ESP32-WROOM-32</strong> &mdash; runs the web server, buffers samples in RAM,
      writes to SD card.</li>
  <li><strong>MicroSD card</strong> &mdash; permanent CSV log storage.</li>
  <li><strong>230 V &rarr; 5 V PSU</strong> &mdash; powers the ESP32 only. Must be a certified
      mains-rated supply.</li>
</ul>

<h2>Connecting to the logger</h2>
<ol>
  <li>Power on the ESP32 &mdash; Wi-Fi access point
      <code>PZEM_Logger</code> / password <code>logger1234</code> appears within ~3 s.</li>
  <li>Power on the Shelly Plug S &mdash; it automatically joins <code>PZEM_Logger</code>
      and starts pushing measurements.</li>
  <li>Connect your phone or laptop to <code>PZEM_Logger</code>.</li>
  <li>Open a browser and go to
      <a href="http://192.168.4.1">http://192.168.4.1</a>
      &nbsp;or&nbsp;
      <a href="http://braun_PZEM.local">http://braun_PZEM.local</a>
      (mDNS &mdash; works on iOS, macOS, most Android).</li>
</ol>

<div class="note">
  <strong>Shelly web interface:</strong> while connected to <code>PZEM_Logger</code>
  the Shelly itself is reachable at <code>http://192.168.4.2</code>
  (or whichever IP the ESP32 DHCP server assigned &mdash; check the status badge on the
  home screen if unsure). Use this to update Shelly firmware or change its script.
</div>

<h2>Web interface &mdash; page by page</h2>

<table>
  <tr><th>Page</th><th>What it does</th></tr>
  <tr>
    <td><strong>Home</strong><br><code>/</code></td>
    <td>Shows live voltage, power and pf<sub>apparent</sub> updated every second.
        Status badges show whether the Shelly is pushing data (green = OK,
        red = no push received in the last 3 s) and whether the SD card is
        working. Also shows RAM buffer fill, dropped sample count, and uptime.</td>
  </tr>
  <tr>
    <td><strong>Live Plot</strong><br><code>/liveplot</code></td>
    <td>Oscilloscope-style scrolling power chart covering the last 60 samples.
        Y-axis scales automatically to the peak value seen. Useful for watching
        switch-on transients or duty cycles in real time.</td>
  </tr>
  <tr>
    <td><strong>Download Log</strong><br><code>/download</code></td>
    <td>Flushes the RAM buffer to SD, then streams <code>log.csv</code> directly
        to your browser as a file download. CSV columns:
        <code>time_ms, voltage_V, power_W, pf_apparent</code>.</td>
  </tr>
  <tr>
    <td><strong>Reset &amp; Delete SD</strong><br><code>POST /reset</code></td>
    <td>Deletes <code>log.csv</code> and creates a fresh file with only the header
        row. RAM buffer is also cleared. Asks for confirmation before proceeding.</td>
  </tr>
  <tr>
    <td><strong>Settings</strong><br><code>/settings</code></td>
    <td>
      <em>Sampling rate</em> &mdash; how often a measurement is taken from the Shelly
      and stored in the buffer: 1 s, 2 s, 5 s, 10 s, or 30 s.<br>
      <em>Power threshold</em> &mdash; only log a sample if active power &ge; this value.
      Set to 0 W to log everything including standby. Useful to avoid filling
      the SD card with idle readings.<br>
      Changes take effect immediately but are lost on reboot.
    </td>
  </tr>
  <tr>
    <td><strong>Firmware Update</strong><br><code>/update</code></td>
    <td>Over-the-air firmware update. Select a compiled <code>.bin</code> file
        (from Arduino IDE: <em>Sketch &rarr; Export Compiled Binary</em>), then click
        Upload. A progress bar shows transfer progress. The ESP32 reboots
        automatically on success &mdash; the page will redirect to <code>/</code>
        after 8 seconds. The SD log is flushed before flashing so no data is
        lost. Logging resumes automatically after reboot.</td>
  </tr>
</table>

<h2>Log file format</h2>
<p>CSV file at <code>/log.csv</code> on the SD card. One row per logged sample.</p>
<table>
  <tr><th>Column</th><th>Unit</th><th>Description</th></tr>
  <tr><td><code>time_ms</code></td><td>ms</td>
      <td>ESP32 uptime at time of sample (not wall-clock time)</td></tr>
  <tr><td><code>voltage_V</code></td><td>V RMS</td>
      <td>Mains voltage measured by Shelly</td></tr>
  <tr><td><code>power_W</code></td><td>W</td>
      <td>Active power (apower) measured by Shelly</td></tr>
  <tr><td><code>pf_apparent</code></td><td>&mdash;</td>
      <td>Derived: power_W / (voltage_V &times; current_A). Range 0&ndash;1.
          Close to 1 for resistive loads (heater, kettle),
          lower for motors or switching supplies.</td></tr>
</table>

<h2>Default settings</h2>
<ul>
  <li>Sampling rate: 1 s</li>
  <li>SD flush interval: every 10 s</li>
  <li>Power threshold: 0 W (log everything)</li>
  <li>RAM buffer: 64 samples (~64 s reserve if SD is temporarily unavailable)</li>
</ul>

<h2>LED status</h2>
<ul>
  <li><strong>1 Hz blink</strong> &mdash; system healthy (Shelly pushing, SD OK)</li>
  <li><strong>5 Hz blink</strong> &mdash; error (Shelly silent for &gt;3 s, or SD fault)</li>
</ul>

<p style="margin-top:1.5em"><strong>Contact:</strong>
<a href="mailto:jan.pfrang@delonghigroup.com">jan.pfrang@delonghigroup.com</a></p>
<p><a href="/">&larr; Back</a></p>

</body></html>
)HTML";

// -----------------------------------------------------------------------------
// PAGE_HISTOGRAM  -- live power distribution since page load
// -----------------------------------------------------------------------------
static const char PAGE_HISTOGRAM[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Power Histogram &ndash; Shelly Logger</title>
<style>
  body { font-family: sans-serif; max-width: 650px; margin: 1em auto;
         padding: 1em; background: #f5f5f5; color: #222; }
  h1   { color: #000; margin-bottom: 0.4em; }
  .card { background: white; border-radius: 8px; padding: 1.2em;
          margin-bottom: 1em; box-shadow: 0 1px 3px rgba(0,0,0,.1); }
  .meta { display: flex; justify-content: space-between; font-size: 0.9em;
          color: #555; margin-bottom: 1em; flex-wrap: wrap; gap: 0.4em; }
  .meta span strong { color: #007acc; }
  canvas { width: 100%; display: block; background: #111116;
           border-radius: 6px; box-shadow: inset 0 0 8px rgba(0,0,0,0.4); }
  .legend { font-size: 0.82em; color: #666; margin-top: 0.5em; text-align: center; }
  .btn-row { display: flex; gap: 0.6em; margin-top: 0.8em; }
  button { padding: 0.7em 1.2em; font-size: 0.95em; border: none;
           border-radius: 6px; background: #007acc; color: white; cursor: pointer; }
  button:hover { background: #005f99; }
  button.danger { background: #d33; }
  button.danger:hover { background: #a22; }
  a.back { display: inline-block; margin-top: 0.5em; color: #007acc;
           text-decoration: none; font-weight: bold; }
  a.back:hover { text-decoration: underline; }
</style>
</head><body>

<h1>Power Distribution</h1>

<div class="card">
  <div class="meta">
    <span>Samples: <strong id="n-samples">0</strong></span>
    <span>Max recorded: <strong id="max-w">&mdash; W</strong></span>
    <span>Current: <strong id="cur-w">&mdash; W</strong></span>
    <span>Bin width: <strong id="bin-w">&mdash;</strong></span>
  </div>

  <canvas id="histCanvas" width="600" height="300"></canvas>
  <div class="legend">20 equal bins &mdash; upper edge = max recorded power since page load</div>

  <div class="btn-row">
    <button onclick="resetData()">Reset</button>
  </div>
</div>

<a class="back" href="/">&larr; Back</a>

<script>
const BINS      = 20;
const BAR_COLOR = '#007acc';
const BAR_HOVER = '#28a745';

// All raw samples collected since page load (or last reset)
let samples  = [];
let maxPower = 0;          // running maximum
let pollMs   = 1000;       // synced from /api/settings on startup

const canvas = document.getElementById('histCanvas');
const ctx    = canvas.getContext('2d');

// &mdash;&mdash; Fetch current poll interval so we match the device cadence &mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;
async function init() {
  try {
    const r = await fetch('/api/settings');
    const d = await r.json();
    if (d.poll_ms) pollMs = Math.max(d.poll_ms, 1000);
  } catch(e) { /* keep default */ }
  setInterval(tick, pollMs);
  tick();
}

// &mdash;&mdash; Called every poll interval &mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;
async function tick() {
  try {
    const r = await fetch('/api/live');
    if (!r.ok) return;
    const d = await r.json();
    if (d.power === null) return;

    const p = d.power;
    samples.push(p);
    if (p > maxPower) maxPower = p;

    document.getElementById('n-samples').textContent = samples.length;
    document.getElementById('max-w').textContent     = maxPower.toFixed(1) + ' W';
    document.getElementById('cur-w').textContent     = p.toFixed(1) + ' W';

    draw();
  } catch(e) { /* network hiccup &mdash; skip tick */ }
}

// &mdash;&mdash; Build bins and draw &mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;
function buildBins() {
  // Guard: if max is 0 (all readings are 0) use 1 W as a floor so bins exist
  const upper = maxPower > 0 ? maxPower : 1;
  const width = upper / BINS;

  document.getElementById('bin-w').textContent = width.toFixed(1) + ' W';

  const counts = new Array(BINS).fill(0);
  for (const s of samples) {
    // Clamp to last bin for values exactly equal to maxPower
    let idx = Math.min(Math.floor(s / width), BINS - 1);
    counts[idx]++;
  }
  return { counts, width, upper };
}

function draw() {
  const W = canvas.width;
  const H = canvas.height;
  ctx.clearRect(0, 0, W, H);

  if (samples.length === 0) return;

  const { counts, width, upper } = buildBins();
  const maxCount = Math.max(...counts, 1);

  // &mdash;&mdash; Layout constants &mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;
  const pL = 52, pR = 12, pT = 16, pB = 38;
  const gW = W - pL - pR;
  const gH = H - pT - pB;

  // &mdash;&mdash; Y-axis gridlines and labels &mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;
  ctx.font      = '11px monospace';
  ctx.fillStyle = '#8e8e93';
  ctx.strokeStyle = '#1e1e26';
  ctx.lineWidth   = 0.8;
  const Y_DIVS = 4;
  ctx.textAlign    = 'right';
  ctx.textBaseline = 'middle';
  for (let i = 0; i <= Y_DIVS; i++) {
    const pct  = i / Y_DIVS;
    const yPos = pT + gH * pct;
    const val  = Math.round(maxCount * (1 - pct));
    ctx.fillText(val, pL - 6, yPos);
    if (i > 0 && i < Y_DIVS) {
      ctx.beginPath();
      ctx.moveTo(pL, yPos);
      ctx.lineTo(W - pR, yPos);
      ctx.stroke();
    }
  }

  // &mdash;&mdash; Bars &mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;
  const barSlot = gW / BINS;
  const barGap  = Math.max(1, barSlot * 0.08);
  const barW    = barSlot - barGap;

  for (let i = 0; i < BINS; i++) {
    const barH  = (counts[i] / maxCount) * gH;
    const x     = pL + i * barSlot + barGap / 2;
    const y     = pT + gH - barH;

    // Highlight the bin containing the most recent reading
    const latest = samples[samples.length - 1];
    const latestBin = Math.min(Math.floor(latest / (upper / BINS)), BINS - 1);
    ctx.fillStyle = (i === latestBin) ? BAR_HOVER : BAR_COLOR;

    ctx.beginPath();
    ctx.roundRect(x, y, barW, Math.max(barH, 1), 2);
    ctx.fill();
  }

  // &mdash;&mdash; X-axis labels (every 5th bin) &mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;
  ctx.fillStyle    = '#8e8e93';
  ctx.textAlign    = 'center';
  ctx.textBaseline = 'top';
  for (let i = 0; i <= BINS; i += 5) {
    const xPos  = pL + (i / BINS) * gW;
    const label = (i * upper / BINS).toFixed(0) + 'W';
    ctx.fillText(label, xPos, pT + gH + 6);
  }

  // &mdash;&mdash; Axes outline &mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;
  ctx.strokeStyle = '#44444f';
  ctx.lineWidth   = 1;
  ctx.strokeRect(pL, pT, gW, gH);
}

function resetData() {
  samples  = [];
  maxPower = 0;
  document.getElementById('n-samples').textContent = '0';
  document.getElementById('max-w').textContent     = '&mdash; W';
  document.getElementById('cur-w').textContent     = '&mdash; W';
  document.getElementById('bin-w').textContent     = '&mdash;';
  ctx.clearRect(0, 0, canvas.width, canvas.height);
}

init();
</script>
</body></html>
)HTML";

// -----------------------------------------------------------------------------
// PAGE_OTA  -- firmware upload form
// Accepts a single .bin file via multipart POST to /update.
// Progress bar driven by XMLHttpRequest upload events (avoids fetch() which
// gives no progress on most mobile browsers).
// On success the page auto-redirects to / after 8 s (long enough for reboot).
// On error it shows the server's error text and offers a retry link.
// -----------------------------------------------------------------------------
static const char PAGE_OTA[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Firmware Update &ndash; Shelly Logger</title>
<style>
  body { font-family: sans-serif; max-width: 560px; margin: 2em auto;
         padding: 1em; background: #f5f5f5; color: #222; }
  h1   { color: #000; font-size: 1.4em; margin-bottom: 0.3em; }
  .card { background: white; border-radius: 8px; padding: 1.4em;
          margin-bottom: 1em; box-shadow: 0 1px 3px rgba(0,0,0,.1); }
  label  { display: block; font-weight: bold; margin-bottom: .5em; color: #444; }
  input[type=file] { width: 100%; padding: .5em 0; font-size: 1em;
                     margin-bottom: 1em; }
  button { padding: .85em 1.8em; font-size: 1em; border: none;
           border-radius: 6px; background: #007acc; color: white;
           cursor: pointer; }
  button:hover    { background: #005f99; }
  button:disabled { background: #aaa; cursor: default; }
  .progress-wrap { background: #e0e0e0; border-radius: 4px;
                   height: 22px; margin: 1em 0; overflow: hidden;
                   display: none; }
  .progress-bar  { height: 100%; width: 0%; background: #28a745;
                   transition: width .15s ease; text-align: center;
                   color: white; font-size: .85em; line-height: 22px; }
  .msg { margin-top: 1em; padding: .6em .9em; border-radius: 5px;
         font-size: .95em; display: none; }
  .msg.ok  { background: #d4edda; color: #155724; display: block; }
  .msg.err { background: #f8d7da; color: #721c24; display: block; }
  .note { font-size: .85em; color: #888; margin-top: .8em; }
  a.back { display: inline-block; margin-top: .8em; color: #007acc;
           text-decoration: none; }
  a.back:hover { text-decoration: underline; }
</style>
</head><body>

<h1>Firmware Update</h1>

<div class="card">
  <label for="binfile">Select compiled firmware (.bin)</label>
  <input type="file" id="binfile" accept=".bin">

  <button id="upload-btn" onclick="startUpload()">Upload &amp; Flash</button>

  <div class="progress-wrap" id="prog-wrap">
    <div class="progress-bar" id="prog-bar">0%</div>
  </div>

  <div class="msg" id="msg"></div>

  <p class="note">
    Export the binary from Arduino IDE via
    <em>Sketch &rarr; Export Compiled Binary</em>, then select the
    <code>*.bin</code> (not <code>*.elf</code>) file above.<br>
    The device will reboot automatically after a successful flash.
    Logging resumes on its own &mdash; no further action needed.
  </p>
</div>

<a class="back" href="/">&larr; Back to home</a>

<script>
function startUpload() {
  const fileInput = document.getElementById('binfile');
  if (!fileInput.files.length) {
    showMsg('Please select a .bin file first.', 'err');
    return;
  }
  const file = fileInput.files[0];
  if (!file.name.endsWith('.bin')) {
    showMsg('File must have a .bin extension.', 'err');
    return;
  }

  const btn      = document.getElementById('upload-btn');
  const progWrap = document.getElementById('prog-wrap');
  const progBar  = document.getElementById('prog-bar');

  btn.disabled        = true;
  progWrap.style.display = 'block';
  hideMsg();

  const formData = new FormData();
  formData.append('firmware', file, file.name);

  const xhr = new XMLHttpRequest();

  // &mdash;&mdash; Progress &mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;
  xhr.upload.onprogress = function(e) {
    if (!e.lengthComputable) return;
    const pct = Math.round((e.loaded / e.total) * 100);
    progBar.style.width  = pct + '%';
    progBar.textContent  = pct + '%';
  };

  // &mdash;&mdash; Done &mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;
  xhr.onload = function() {
    if (xhr.status === 200) {
      progBar.style.width  = '100%';
      progBar.style.background = '#28a745';
      progBar.textContent  = '100%';
      showMsg('&#x2713; Flash successful &mdash; device is rebooting. ' +
              'Redirecting to home in 8 s&hellip;', 'ok');
      setTimeout(function() { location.href = '/'; }, 8000);
    } else {
      showMsg('&#x2717; Upload failed (HTTP ' + xhr.status + '): ' +
              xhr.responseText, 'err');
      btn.disabled = false;
    }
  };

  // &mdash;&mdash; Network error &mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;&mdash;
  // A network error here is actually EXPECTED on success: the ESP32 reboots
  // mid-response.  If we already showed the 100% bar, treat it as success.
  xhr.onerror = function() {
    if (progBar.textContent === '100%') {
      showMsg('&#x2713; Flash likely successful &mdash; device rebooting. ' +
              'Redirecting to home in 8 s&hellip;', 'ok');
      setTimeout(function() { location.href = '/'; }, 8000);
    } else {
      showMsg('&#x2717; Network error &mdash; check device and try again.', 'err');
      btn.disabled = false;
    }
  };

  xhr.open('POST', '/update');
  xhr.send(formData);
}

function showMsg(text, cls) {
  const el = document.getElementById('msg');
  el.textContent = text;
  el.className   = 'msg ' + cls;
}
function hideMsg() {
  const el = document.getElementById('msg');
  el.className = 'msg';
}
</script>
</body></html>
)HTML";

// -----------------------------------------------------------------------------
// Implementation
// -----------------------------------------------------------------------------

WebPortal::WebPortal(Logger& logger, ShellyClient& shelly, RTC_DS3231* rtc, PowerMonitor* pm)
  : _logger(logger), _shelly(shelly), _rtc(rtc), _pm(pm), _server(HTTP_PORT) {}

bool WebPortal::begin() {
  Serial.println("[Web] Starte WLAN AP+STA...");

  // AP-only mode: no STA scanning, radio stays locked on AP channel.
  // WIFI_AP_STA was causing random 1-3 s AP blackouts every few seconds
  // because the ESP32 radio performs background channel scans when STA
  // is active but unconnected, making the softAP deaf during each scan.
  WiFi.mode(WIFI_AP);

  if (!WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD)) {
    Serial.println("[Web] softAP() fehlgeschlagen!");
    return false;
  }
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[Web] AP '%s' aktiv, IP: %s\n", WIFI_AP_SSID, ip.toString().c_str());

  // Captive portal DNS catch-all
  _dns.start(DNS_PORT, "*", ip);
  Serial.println("[Web] DNS-Server gestartet (catch-all)");

  // mDNS -- braun_PZEM.local  (Req: plain-text hostname)
  if (MDNS.begin(WIFI_AP_HOSTNAME)) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    Serial.printf("[Web] mDNS aktiv: http://%s.local\n", WIFI_AP_HOSTNAME);
  }

  // -- Captive portal probes ----------------------------------------------
  _server.on("/generate_204",              HTTP_GET, [this](){ handleCaptivePortal(); });
  _server.on("/gen_204",                   HTTP_GET, [this](){ handleCaptivePortal(); });
  _server.on("/hotspot-detect.html",       HTTP_GET, [this](){ handleCaptivePortal(); });
  _server.on("/library/test/success.html", HTTP_GET, [this](){ handleCaptivePortal(); });
  _server.on("/ncsi.txt",                  HTTP_GET, [this](){ handleCaptivePortal(); });
  _server.on("/connecttest.txt",           HTTP_GET, [this](){ handleCaptivePortal(); });
  _server.on("/redirect",                  HTTP_GET, [this](){ handleCaptivePortal(); });
  _server.on("/canonical.html",            HTTP_GET, [this](){ handleCaptivePortal(); });

  // -- Application routes (unchanged from v5 except /api/shelly_push) ------
  _server.on("/",                    HTTP_GET,  [this](){ handleRoot(); });
  _server.on("/api/live",            HTTP_GET,  [this](){ handleApiLive(); });
  _server.on("/api/settings",        HTTP_GET,  [this](){ handleApiSettings(); });
  _server.on("/api/settings",        HTTP_POST, [this](){ handleApiSettingsSave(); });
  _server.on("/api/shelly_push",     HTTP_POST, [this](){ handleShellyPush(); });   // v6
  _server.on(OTA_ENDPOINT,           HTTP_GET,  [this](){ handleOtaForm(); });      // v7
  // POST OTA_ENDPOINT: the body handler sends the final HTTP response;
  // the upload (chunk) handler is registered separately via onFileUpload.
  _server.on(OTA_ENDPOINT,           HTTP_POST,
    [this](){ handleOtaUpload(); },
    [this](){ handleOtaChunk(); });
  _server.on("/download",            HTTP_GET,  [this](){ handleDownload(); });
  _server.on("/reset",               HTTP_POST, [this](){ handleReset(); });
  _server.on("/settings",            HTTP_GET,  [this](){ handleSettings(); });
  _server.on("/readme",              HTTP_GET,  [this](){ handleReadme(); });
  _server.on("/histogram",            HTTP_GET,  [this](){ handleHistogram(); });
  _server.on("/liveplot",            HTTP_GET,  [this](){ handleLivePlot(); });
  _server.on("/api/set_rtc",         HTTP_POST, [this](){ handleApiSetRtc(); });  // v8
  _server.onNotFound(                          [this](){ handleNotFound(); });

  _server.begin();
  Serial.println("[Web] HTTP-Server gestartet");
  return true;
}

void WebPortal::update() {
  _dns.processNextRequest();
  _server.handleClient();
}

// -----------------------------------------------------------------------------
// Handlers
// -----------------------------------------------------------------------------

void WebPortal::handleRoot() {
  _server.send_P(200, "text/html", PAGE_INDEX);
}

void WebPortal::handleCaptivePortal() {
  _server.sendHeader("Location", "http://192.168.4.1/", true);
  _server.send(302, "text/plain", "");
}

// -- NEW: receives push from shelly_push.js --------------------------------
void WebPortal::handleShellyPush() {
  String body = _server.arg("plain");
  if (body.length() == 0) {
    _server.send(400, "application/json", "{\"error\":\"empty body\"}");
    return;
  }

  // Fast check: does the body look like a batch payload?
  // strstr is cheaper than a full JSON parse for this routing decision.
  if (body.indexOf("\"batch\"") >= 0) {
    int count = _shelly.ingestBatch(body);
    if (count >= 0) {
      char resp[48];
      snprintf(resp, sizeof(resp), "{\"ok\":true,\"batch\":%d}", count);
      _server.send(200, "application/json", resp);
    } else {
      _server.send(400, "application/json", "{\"error\":\"batch parse failed\"}");
    }
    return;
  }

  // Normal single-sample path (unchanged)
  if (_shelly.ingest(body)) {
    _server.send(200, "application/json", "{\"ok\":true}");
  } else {
    _server.send(400, "application/json", "{\"error\":\"parse failed\"}");
  }
}

// -- /api/live -- includes shelly_ok (v6), ota_active (v7), supply_mv (v9) --
void WebPortal::handleApiLive() {
  char buf[API_BUFFER_SIZE];
  float p  = _logger.getLastPower();
  float v  = _logger.getLastVoltage();
  float pf = _logger.getLastPf();

  // Build only the sensor part conditionally. The status fields
  // (buffer/dropped/uptime/shelly_ok/sd_ok/ota_active/supply_mv) are identical in
  // both cases, so they are formatted exactly once below -- adding a new
  // status field means editing one place, not two.
  char sensor[72];
  if (isnan(p) || isnan(v) || isnan(pf)) {
    snprintf(sensor, sizeof(sensor),
             "\"power\":null,\"voltage\":null,\"pf\":null");
  } else {
    snprintf(sensor, sizeof(sensor),
             "\"power\":%.1f,\"voltage\":%.1f,\"pf\":%.2f", p, v, pf);
  }

  // Supply voltage: read from PowerMonitor cache (0 when monitor disabled).
  uint32_t supplyMv   = _pm ? _pm->getLastRailMilliVolts() : 0;
  bool     supplyOk   = (supplyMv == 0) ||
                        (supplyMv >= POWER_THRESHOLD_LOW_MV);  // green when disabled too

  snprintf(buf, sizeof(buf),
    "{%s,\"buffer\":%u,\"dropped\":%lu,\"uptime\":%lu,"
    "\"shelly_ok\":%s,\"sd_ok\":%s,\"ota_active\":%s,"
    "\"supply_mv\":%lu,\"supply_ok\":%s}",
    sensor,
    (unsigned)_logger.getBufferCount(),
    (unsigned long)_logger.getDroppedSamples(),
    (unsigned long)(millis() / 1000),
    _logger.shellyOk()        ? "true" : "false",
    _logger.sdOk()            ? "true" : "false",
    _logger.isOtaInProgress() ? "true" : "false",
    (unsigned long)supplyMv,
    supplyOk                  ? "true" : "false");

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
    _server.send(200, "text/plain", "Log-Datei wurde geloescht und neu angelegt.");
  } else {
    _server.send(500, "text/plain", "Fehler beim Loeschen der Log-Datei (SD nicht verfuegbar?).");
  }
}

void WebPortal::handleSettings() {
  _server.send_P(200, "text/html", PAGE_SETTINGS);
}

void WebPortal::handleHistogram() {
  _server.send_P(200, "text/html", PAGE_HISTOGRAM);
}

void WebPortal::handleLivePlot() {
  _server.send_P(200, "text/html", PAGE_LIVEPLOT);
}

// -- GET /api/settings -----------------------------------------------------
void WebPortal::handleApiSettings() {
  char buf[128];
  snprintf(buf, sizeof(buf),
           "{\"poll_ms\":%lu,\"power_threshold\":%d}",
           (unsigned long)_logger.getPollInterval(),
           (int)_logger.getPowerThreshold());
  _server.send(200, "application/json", buf);
}

// -- POST /api/settings -- poll_ms whitelist updated (200/500 removed) ------
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

// -- GET /update -- serve the OTA upload form -------------------------------
void WebPortal::handleOtaForm() {
  _server.send_P(200, "text/html", PAGE_OTA);
}

// -- POST /update body handler -- sends the final HTTP response ------------
//
// WebServer calls this AFTER the last handleOtaChunk() invocation.
// Update.end() has already been called inside the chunk handler, so here
// we only check Update.hasError() and send the appropriate response.
//
// If the flash succeeded the ESP32 reboots inside ESP.restart() below;
// the HTTP response may or may not reach the browser before the reboot --
// PAGE_OTA handles both outcomes (the xhr.onerror path on the JS side).
void WebPortal::handleOtaUpload() {
  if (Update.hasError()) {
    // Restore logging so the device stays usable after a failed update.
    _logger.setOtaInProgress(false);
    String err = Update.errorString();
    Serial.printf("[OTA] Fehlgeschlagen: %s\n", err.c_str());
    _server.send(500, "text/plain", "OTA fehlgeschlagen: " + err);
  } else {
    // Success path -- send 200 then reboot.
    // The response may be cut off mid-TCP by the reboot; PAGE_OTA JS
    // handles the resulting xhr.onerror as a successful outcome.
    _server.send(200, "text/plain", "OK");
    delay(200);   // give TCP stack a moment to flush
    Serial.println("[OTA] Erfolgreich -- Neustart...");
    ESP.restart();
  }
}

// -- Upload chunk callback -- called by WebServer for every multipart chunk -
//
// Execution context: synchronous inside _server.handleClient(), same task
// as loop().  No RTOS or ISR concerns.
//
// HTTPUpload fields used:
//   status   -- UPLOAD_FILE_START / UPLOAD_FILE_WRITE / UPLOAD_FILE_END / UPLOAD_FILE_ABORTED
//   buf      -- pointer to this chunk's data
//   currentSize -- number of valid bytes in buf this call
//   totalSize   -- total bytes received so far (increases each WRITE call)
void WebPortal::handleOtaChunk() {
  HTTPUpload& upload = _server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[OTA] Start: %s  (%u Bytes erwartet)\n",
                  upload.filename.c_str(), upload.totalSize);

    // Pause logging and flush SD before touching flash.
    // setOtaInProgress(true) calls flushToSD() internally.
    _logger.setOtaInProgress(true);

    // UPDATE_SIZE_UNKNOWN lets the Update library allocate flash
    // dynamically.  Passing the actual file size is also valid if known,
    // but the browser does not always send Content-Length reliably in
    // multipart uploads over the ESP32 WebServer.
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Serial.printf("[OTA] Update.begin() fehlgeschlagen: %s\n",
                    Update.errorString());
      // Do NOT abort here -- let handleOtaUpload() send the error response
      // so the HTTP transaction completes cleanly.
    }

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    // Feed this chunk to the flash writer.
    // Update.write() returns the number of bytes actually written;
    // a mismatch means flash is full or the image is corrupt.
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Serial.printf("[OTA] Update.write() Fehler bei Byte %u: %s\n",
                    upload.totalSize, Update.errorString());
    } else {
      // Print a progress dot every ~16 KB to Serial without flooding it.
      if ((upload.totalSize / upload.currentSize) % 64 == 0) {
        Serial.print('.');
      }
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    Serial.printf("\n[OTA] Uebertragen: %u Bytes -- finalisiere...\n",
                  upload.totalSize);
    // Finalise: verify the flash and mark the new partition as bootable.
    // Passing 'true' triggers MD5 verification if the image contains a hash.
    if (!Update.end(true)) {
      Serial.printf("[OTA] Update.end() fehlgeschlagen: %s\n",
                    Update.errorString());
    }

  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    // Client disconnected mid-upload.
    Update.abort();
    _logger.setOtaInProgress(false);
    Serial.println("[OTA] Upload abgebrochen -- Logging wiederhergestellt");
  }
}

// -- POST /api/set_rtc -- set the DS3231 RTC time (NEW v8) ------------------
//
// Body (application/json):
//   { "year":YYYY, "month":M, "day":D, "hour":H, "minute":M, "second":S }
// Response 200: { "ok":true, "time":"YYYY-MM-DDTHH:MM:SS" }
// Response 400: { "error":"..." }   -- missing or out-of-range fields
// Response 503: { "error":"RTC not available" }  -- _rtc == nullptr
void WebPortal::handleApiSetRtc() {
  if (!_rtc) {
    _server.send(503, "application/json", "{\"error\":\"RTC not available\"}");
    return;
  }

  String body = _server.arg("plain");
  if (body.length() == 0) {
    _server.send(400, "application/json", "{\"error\":\"empty body\"}");
    return;
  }

  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    _server.send(400, "application/json", "{\"error\":\"JSON parse failed\"}");
    return;
  }

  int y  = doc["year"]   | 0;
  int mo = doc["month"]  | 0;
  int d  = doc["day"]    | 0;
  int h  = doc["hour"]   | 0;
  int mi = doc["minute"] | 0;
  int s  = doc["second"] | 0;

  if (y < 2020 || y > 2099 ||
      mo < 1   || mo > 12  ||
      d  < 1   || d  > 31  ||
      h  < 0   || h  > 23  ||
      mi < 0   || mi > 59  ||
      s  < 0   || s  > 59) {
    _server.send(400, "application/json", "{\"error\":\"invalid or missing fields\"}");
    return;
  }

  _rtc->adjust(DateTime(y, mo, d, h, mi, s));
  Serial.printf("[RTC] Zeit gesetzt: %04d-%02d-%02d %02d:%02d:%02d\n",
                y, mo, d, h, mi, s);

  char resp[72];
  snprintf(resp, sizeof(resp),
           "{\"ok\":true,\"time\":\"%04d-%02d-%02dT%02d:%02d:%02d\"}",
           y, mo, d, h, mi, s);
  _server.send(200, "application/json", resp);
}

void WebPortal::handleNotFound() {
  handleCaptivePortal();
}
