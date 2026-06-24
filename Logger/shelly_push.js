/**
 * shelly_push.js  –  Shelly Plug S MTR Gen3  •  Firmware Script  v4
 * ==================================================================
 *
 * Changes vs. v3
 * --------------
 *   FIXED    bufferSample() — replaced _buf.shift() with mJS-compatible loop
 *
 *            Root cause of "Uncaught Error: Function 'shift' not found":
 *            Shelly uses mJS (Mongoose JS), a minimal embedded JS engine.
 *            Array.shift() is NOT implemented in mJS. The crash was silent
 *            during normal operation (buffer stays at length 1) but triggered
 *            reliably after 10 consecutive failed POSTs (BUFFER_MAX reached),
 *            which happens on every autostart while the ESP32 AP is not yet
 *            visible, and during any mid-operation WiFi dropout > 10 s.
 *
 *            Fix: rebuild the array manually using a for-loop and push().
 *            Both Array.push() and Array.length are confirmed available in mJS.
 *
 *   FIXED    Startup Switch.Set call now wrapped in a 3 s one-shot timer
 *            so the Switch subsystem is guaranteed to be ready before we
 *            touch it. Calling Switch.Set too early after boot caused
 *            "unknown error" on autostart.
 *
 *   UNCHANGED  All CONFIG constants, doPush(), readSample(), HTTP payload
 *              formats, batch/single-sample routing — byte-for-byte identical.
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

// ─── CONFIG ──────────────────────────────────────────────────────────────────

var PUSH_INTERVAL_MS = 1000;          // Must match ESP32 INTERVAL_SHELLY_POLL_MS
var ESP32_IP         = "192.168.4.1"; // ESP32 softAP gateway — fixed IP
var PUSH_URL         = "http://" + ESP32_IP + "/api/shelly_push";
var SWITCH_ID        = 0;             // Plug S has a single switch channel

var RELAY_DELAY_MS   = 2000;          // ms after script start before relay turns on
var BUFFER_MAX       = 10;            // max samples held offline (sliding window)

// ─── STATE ───────────────────────────────────────────────────────────────────

var _pushPending = false; // guard: don't stack HTTP calls if ESP32 is slow
var _connected   = false; // true after first successful HTTP 200 from ESP32
var _relayOn     = false; // true after RELAY_DELAY_MS has elapsed
var _buf         = [];    // offline sample ring buffer

// ─── HELPERS ─────────────────────────────────────────────────────────────────

function roundTo(val, decimals) {
  var m = 1;
  var i;
  for (i = 0; i < decimals; i++) { m = m * 10; }
  return Math.round(val * m) / m;
}

/**
 * Read the current Switch measurement and return a sample object,
 * or null if the hardware reading is not yet available.
 */
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

/**
 * Append sample to the offline buffer.
 * If BUFFER_MAX is reached, drop the oldest entry (sliding window).
 *
 * NOTE: Array.shift() is NOT available in Shelly mJS — replaced with
 *       a manual for-loop that rebuilds the array without the first element.
 */
function bufferSample(s) {
  if (_buf.length >= BUFFER_MAX) {
    // mJS-compatible replacement for _buf.shift():
    // copy elements [1..end] into a new array, then replace _buf.
    var tmp = [];
    var i;
    for (i = 1; i < _buf.length; i++) {
      tmp.push(_buf[i]);
    }
    _buf = tmp;
  }
  _buf.push(s);
}

// ─── CORE PUSH FUNCTION ───────────────────────────────────────────────────────

function doPush() {
  // ── 1. Relay delay ─────────────────────────────────────────────────────────
  if (!_relayOn && Shelly.getUptimeMs() >= RELAY_DELAY_MS) {
    Shelly.call("Switch.Set", { id: SWITCH_ID, on: true }, null);
    _relayOn = true;
    print("[shelly_push] relay ON after", RELAY_DELAY_MS, "ms delay");
  }

  // ── 2. Always sample and buffer ────────────────────────────────────────────
  // The buffer is cleared only on a confirmed HTTP 200. This means:
  //   • Startup:          _buf grows until first successful batch POST.
  //   • Normal operation: _buf holds exactly 1 sample; cleared each tick.
  //   • Dropout:          _buf grows (sliding window) until reconnect.
  var s = readSample();
  if (!s) {
    print("[shelly_push] measurement unavailable, skipping");
    return;
  }
  bufferSample(s);

  // ── 3. Guard: only one HTTP request in flight at a time ────────────────────
  if (_pushPending) {
    print("[shelly_push] skipping tick - previous request still pending");
    return;
  }

  // ── 4. Send buffer (single-sample fast path or batch) ──────────────────────
  // Single-sample when already connected and buffer has exactly 1 entry.
  // Batch on startup, dropout recovery, or any buffer length > 1.

  if (_connected && _buf.length === 1) {
    // ── 4a. Normal operation: single-sample POST (fast path) ─────────────────
    var body = JSON.stringify(s);
    _pushPending = true;

    Shelly.call(
      "HTTP.Request",
      {
        method:  "POST",
        url:     PUSH_URL,
        headers: {"Content-Type": "application/json"},
        body:    body,
        timeout: 1
      },
      function(result, error_code, error_msg) {
        _pushPending = false;
        if (error_code === 0 && result && result.code === 200) {
          _buf = [];   // confirmed delivered — discard
        } else {
          // Dropout detected: re-enter offline mode so next tick buffers
          // and attempts a batch recovery POST.
          _connected = false;
          print("[shelly_push] dropout detected, buffering:", error_code, error_msg);
        }
      }
    );

  } else {
    // ── 4b. Offline or dropout recovery: batch POST ───────────────────────────
    var batchBody = JSON.stringify({ batch: _buf });
    _pushPending = true;

    Shelly.call(
      "HTTP.Request",
      {
        method:  "POST",
        url:     PUSH_URL,
        headers: {"Content-Type": "application/json"},
        body:    batchBody,
        timeout: 1
      },
      function(result, error_code, error_msg) {
        _pushPending = false;
        if (error_code === 0 && result && result.code === 200) {
          _connected = true;
          _buf = [];   // ESP32 has all buffered samples — discard
          print("[shelly_push] (re)connected, batch delivered");
        } else {
          // Still offline — keep buffering, try again next tick
          print("[shelly_push] batch attempt failed:", error_code, error_msg);
        }
      }
    );
  }
}

// ─── STARTUP ─────────────────────────────────────────────────────────────────

print("[shelly_push] v4 starting - relay delay:", RELAY_DELAY_MS,
      "ms, buffer:", BUFFER_MAX, "samples");

// Defer Switch.Set by 3 s so the Switch subsystem is fully initialised
// before we touch it. Calling Switch.Set immediately at script start
// causes "unknown error" on autostart because the relay driver is not
// yet ready when mJS begins executing.
Timer.set(3000, false, function() {
  Shelly.call("Switch.Set", { id: SWITCH_ID, on: false }, null);
}, null);

// Start the main push loop after the same 3 s delay so the first
// doPush() tick also runs on a fully initialised switch component.
Timer.set(3000, false, function() {
  Timer.set(PUSH_INTERVAL_MS, true, doPush, null);
}, null);
