/**
 * shelly_push.js  -  Shelly Plug S MTR Gen3  -  Firmware Script  v5
 * ==================================================================
 *
 * Changes vs. v4
 * --------------
 *   FIXED    HTTP timeout raised from 1 s to 2 s in both the single-sample
 *            and batch POST paths.
 *
 *            Root cause of observed 2 s log gaps:
 *            The ESP32 SD flush (flushIfDue, every 10 s) blocks the HTTP
 *            response handler for 50-200 ms.  With timeout=1 s, any push
 *            that arrived near a flush boundary exceeded the 1 s window,
 *            causing the Shelly to treat it as a dropout, set _connected=false,
 *            and send a batch POST on the next tick.  The ESP32 drains the
 *            batch one sample per pollIfDue() tick (1 s each), producing a
 *            visible 2 s gap in the CSV log every ~10 s.
 *            With timeout=2 s the flush latency is always absorbed.
 *            The _pushPending guard still prevents HTTP call stacking.
 *
 *   FIXED    Switch.Set on startup now correctly sets relay ON (was OFF).
 *            The relay was being turned OFF 3 s after every script start,
 *            cutting power to the load on every ESP32 reboot.
 *
 * Changes vs. v3
 * --------------
 *   FIXED    bufferSample() - replaced _buf.shift() with mJS-compatible loop
 *   FIXED    Startup Switch.Set call now wrapped in a 3 s one-shot timer
 *
 * mJS COMPATIBILITY NOTES
 * -----------------------
 * The following standard JS Array methods are NOT available in mJS:
 *   shift(), splice(), indexOf(), forEach(), map(), filter(), reduce()
 * Use plain for-loops and push() instead.
 * JSON.stringify(), Math.round(), Timer.set(), Shelly.call(),
 * Shelly.getComponentStatus(), Shelly.getUptimeMs() are all available.
 *
 * CONFIGURATION
 * -------------
 * Only change the constants in the CONFIG block below.
 */

// --- CONFIG ------------------------------------------------------------------

var PUSH_INTERVAL_MS = 1000;          // Must match ESP32 INTERVAL_SHELLY_POLL_MS
var ESP32_IP         = "192.168.4.1"; // ESP32 softAP gateway - fixed IP
var PUSH_URL         = "http://" + ESP32_IP + "/api/shelly_push";
var SWITCH_ID        = 0;             // Plug S has a single switch channel

var RELAY_DELAY_MS   = 2000;          // ms after script start before relay turns on
var BUFFER_MAX       = 10;            // max samples held offline (sliding window)

// --- STATE -------------------------------------------------------------------

var _pushPending = false;
var _connected   = false;
var _relayOn     = false;
var _buf         = [];

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

function bufferSample(s) {
  if (_buf.length >= BUFFER_MAX) {
    var tmp = [];
    var i;
    for (i = 1; i < _buf.length; i++) {
      tmp.push(_buf[i]);
    }
    _buf = tmp;
  }
  _buf.push(s);
}

// --- CORE PUSH FUNCTION ------------------------------------------------------

function doPush() {

  // 1. Relay delay
  if (!_relayOn && Shelly.getUptimeMs() >= RELAY_DELAY_MS) {
    Shelly.call("Switch.Set", { id: SWITCH_ID, on: true }, null);
    _relayOn = true;
    print("[shelly_push] relay ON after", RELAY_DELAY_MS, "ms delay");
  }

  // 2. Always sample and buffer
  var s = readSample();
  if (!s) {
    print("[shelly_push] measurement unavailable, skipping");
    return;
  }
  bufferSample(s);

  // 3. Guard: only one HTTP request in flight at a time
  if (_pushPending) {
    print("[shelly_push] skipping tick - previous request still pending");
    return;
  }

  // 4a. Normal operation: single-sample POST (fast path)
  if (_connected && _buf.length === 1) {
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
          _buf = [];
        } else {
          _connected = false;
          print("[shelly_push] dropout:", error_code, error_msg);
        }
      }
    );
    return;
  }

  // 4b. Startup / dropout recovery: batch POST
  var batchBody = JSON.stringify({ batch: _buf });
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
        _buf = [];
        print("[shelly_push] (re)connected, batch delivered");
      } else {
        print("[shelly_push] batch failed:", error_code, error_msg);
      }
    }
  );
}

// --- STARTUP -----------------------------------------------------------------

print("[shelly_push] v5 starting - interval:", PUSH_INTERVAL_MS,
      "ms, buffer:", BUFFER_MAX, "samples");

// Defer Switch.Set by 3 s so the Switch subsystem is fully initialised.
Timer.set(3000, false, function() {
  Shelly.call("Switch.Set", { id: SWITCH_ID, on: true }, null);
}, null);

// Start the main push loop after the same 3 s delay.
Timer.set(3000, false, function() {
  Timer.set(PUSH_INTERVAL_MS, true, doPush, null);
}, null);
