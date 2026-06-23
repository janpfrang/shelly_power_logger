/**
 * shelly_push.js  –  Shelly Plug S MTR Gen3  •  Firmware Script  v3
 * ==================================================================
 *
 * Changes vs. v2
 * --------------
 *   CHANGED  doPush() — unified buffer strategy
 *            v2 only buffered while _connected was false (startup phase).
 *            During a mid-operation WiFi dropout _connected stayed true,
 *            no buffering occurred, and samples were silently lost.
 *
 *            v3 buffers EVERY sample unconditionally. The buffer is
 *            cleared only on a confirmed HTTP 200 response. This means:
 *
 *            Startup (offline):
 *              Samples accumulate in _buf[]. Each tick attempts a batch
 *              POST. On first success _connected=true, _buf cleared.
 *
 *            Normal operation (online):
 *              Each tick buffers the current sample, then immediately
 *              attempts to POST the buffer (which is always length 1
 *              under normal conditions). On HTTP 200 _buf is cleared.
 *              Net effect: identical to v2 single-sample behaviour.
 *
 *            Mid-operation dropout:
 *              Failed push → _connected=false. Buffering continues
 *              (sliding window, max BUFFER_MAX). On reconnect the
 *              accumulated samples are sent as a batch and _buf cleared.
 *              Maximum data loss = samples beyond BUFFER_MAX (> 10 s
 *              of continuous dropout).
 *
 *   UNCHANGED  Relay delay, BUFFER_MAX, payload formats, ESP32 API
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
 */
function bufferSample(s) {
  _buf.push(s);
  if (_buf.length > BUFFER_MAX) {
    _buf.shift();  // drop oldest
  }
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
    print("[shelly_push] skipping tick – previous request still pending");
    return;
  }

  // ── 4. Send buffer as batch (works for both 1-sample and multi-sample) ─────
  // Using batch format always would change the ESP32 routing path on every
  // tick. Instead: single-sample format when _buf has exactly 1 entry AND
  // we are already connected — preserves the fast normal-operation path.
  // Multi-sample (startup or dropout recovery): always batch format.

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
          // Current sample stays in _buf — will be included in next batch.
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

print("[shelly_push] v2 starting – relay delay:", RELAY_DELAY_MS,
      "ms, buffer:", BUFFER_MAX, "samples");

// Relay starts OFF. doPush() turns it on after RELAY_DELAY_MS.
// Do NOT call Switch.Set here — leave relay in its current state until
// the delay expires. On a cold boot the Shelly relay defaults to OFF.
Shelly.call("Switch.Set", { id: SWITCH_ID, on: false }, null);

// Main periodic timer — repeating every PUSH_INTERVAL_MS.
Timer.set(PUSH_INTERVAL_MS, true, doPush, null);
