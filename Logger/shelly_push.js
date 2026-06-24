/**
 * shelly_push.js  -  Shelly Plug S MTR Gen3  -  Firmware Script  v6
 * ==================================================================
 *
 * Changes vs. v5
 * --------------
 *   FIXED    Replaced all Array.push() calls with index-based assignment.
 *            mJS on some Shelly firmware versions does not support push().
 *            All array operations now use only [] indexing and .length.
 *
 * Changes vs. v4
 * --------------
 *   FIXED    HTTP timeout raised from 1 s to 2 s (both POST paths).
 *   FIXED    Switch.Set on startup sets relay ON (was OFF).
 *
 * mJS COMPATIBILITY - confirmed available:
 *   [], .length, JSON.stringify(), Math.round(),
 *   Timer.set(), Shelly.call(), Shelly.getComponentStatus(),
 *   Shelly.getUptimeMs(), print()
 *
 * mJS NOT available:
 *   push(), shift(), splice(), indexOf(), forEach(), map(), filter()
 */

// --- CONFIG ------------------------------------------------------------------

var PUSH_INTERVAL_MS = 1000;
var ESP32_IP         = "192.168.4.1";
var PUSH_URL         = "http://" + ESP32_IP + "/api/shelly_push";
var SWITCH_ID        = 0;
var RELAY_DELAY_MS   = 2000;
var BUFFER_MAX       = 10;

// --- STATE -------------------------------------------------------------------

var _pushPending = false;
var _connected   = false;
var _relayOn     = false;

// Ring buffer implemented with plain indexed array + length counter
var _buf    = [];
var _bufLen = 0;

// --- HELPERS -----------------------------------------------------------------

function roundTo(val, decimals) {
  var m = 1;
  var i;
  for (i = 0; i < decimals; i++) { m = m * 10; }
  return Math.round(val * m) / m;
}

function readSample() {
  var sw = Shelly.getComponentStatus("switch", SWITCH_ID);
  if (!sw) { return null; }

  var v       = sw.voltage;
  var p       = sw.apower;
  var current = sw.current;

  if (v === null || v === undefined ||
      p === null || p === undefined ||
      current === null || current === undefined) {
    return null;
  }

  var vi = v * current;
  var pf;
  if (vi < 0.001) {
    pf = 0.0;
  } else {
    pf = p / vi;
    if (pf > 1.0) { pf = 1.0; }
    if (pf < 0.0) { pf = 0.0; }
  }

  return {
    ts: Shelly.getUptimeMs(),
    v:  roundTo(v,       1),
    p:  roundTo(p,       1),
    i:  roundTo(current, 3),
    pf: roundTo(pf,      2)
  };
}

// Add sample to buffer. If full, drop oldest by shifting indices manually.
function bufferSample(s) {
  var i;
  if (_bufLen >= BUFFER_MAX) {
    // Drop oldest: shift everything left by one
    for (i = 0; i < BUFFER_MAX - 1; i++) {
      _buf[i] = _buf[i + 1];
    }
    _bufLen = BUFFER_MAX - 1;
  }
  _buf[_bufLen] = s;
  _bufLen = _bufLen + 1;
}

// Build a plain array of _bufLen entries for JSON.stringify
function getBufSnapshot() {
  var out = [];
  var i;
  for (i = 0; i < _bufLen; i++) {
    out[i] = _buf[i];
  }
  return out;
}

// --- CORE PUSH FUNCTION ------------------------------------------------------

function doPush() {

  // 1. Relay delay
  if (!_relayOn && Shelly.getUptimeMs() >= RELAY_DELAY_MS) {
    Shelly.call("Switch.Set", { id: SWITCH_ID, on: true }, null);
    _relayOn = true;
    print("[shelly_push] relay ON");
  }

  // 2. Sample and buffer
  var s = readSample();
  if (!s) {
    print("[shelly_push] measurement unavailable, skipping");
    return;
  }
  bufferSample(s);

  // 3. Guard: one HTTP call at a time
  if (_pushPending) {
    print("[shelly_push] skipping - request pending");
    return;
  }

  // 4a. Normal: single-sample POST
  if (_connected && _bufLen === 1) {
    var body = JSON.stringify(s);
    _pushPending = true;
    Shelly.call(
      "HTTP.Request",
      {
        method:  "POST",
        url:     PUSH_URL,
        headers: { "Content-Type": "application/json" },
        body:    body,
        timeout: 2
      },
      function(result, error_code, error_msg) {
        _pushPending = false;
        if (error_code === 0 && result && result.code === 200) {
          _bufLen = 0;
        } else {
          _connected = false;
          print("[shelly_push] dropout:", error_code, error_msg);
        }
      }
    );
    return;
  }

  // 4b. Startup / recovery: batch POST
  var batchBody = JSON.stringify({ batch: getBufSnapshot() });
  _pushPending = true;
  Shelly.call(
    "HTTP.Request",
    {
      method:  "POST",
      url:     PUSH_URL,
      headers: { "Content-Type": "application/json" },
      body:    batchBody,
      timeout: 2
    },
    function(result, error_code, error_msg) {
      _pushPending = false;
      if (error_code === 0 && result && result.code === 200) {
        _connected = true;
        _bufLen = 0;
        print("[shelly_push] (re)connected, batch delivered");
      } else {
        print("[shelly_push] batch failed:", error_code, error_msg);
      }
    }
  );
}

// --- STARTUP -----------------------------------------------------------------

print("[shelly_push] v6 starting - interval:", PUSH_INTERVAL_MS, "ms");

Timer.set(3000, false, function() {
  Shelly.call("Switch.Set", { id: SWITCH_ID, on: true }, null);
}, null);

Timer.set(3000, false, function() {
  Timer.set(PUSH_INTERVAL_MS, true, doPush, null);
}, null);
