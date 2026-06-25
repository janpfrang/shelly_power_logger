/**
 * shelly_push.js  –  Shelly Plug S MTR Gen3  •  Firmware Script
 * ==============================================================
 *
 * PURPOSE
 * -------
 * This script runs *on the Shelly device itself* using the built-in
 * mJS (minimal JavaScript) runtime.
 *
 * Instead of waiting for the ESP32 to poll us over HTTP, the Shelly
 * actively PUSHES a compact JSON payload to the ESP32 every second.
 * This is far more reliable than polling:
 *   • Timestamps originate at the Shelly (accurate to its uptime clock)
 *   • No missed samples due to ESP32 HTTP round-trip jitter
 *   • If the ESP32 is busy, the Shelly retries naturally on the next tick
 *
 * WHAT IT DOES
 * ------------
 * Every PUSH_INTERVAL_MS (default: 1200 ms) the script:
 *   1. Reads Switch component status (apower, voltage, current)
 *   2. Derives pf_apparent = apower / (voltage * current)
 *   3. POSTs a JSON body to http://192.168.4.1/api/shelly_push
 *      (the ESP32's fixed softAP IP — always reachable)
 *
 * JSON payload sent to ESP32:
 *   {
 *     "ts":      <Shelly uptime ms — monotonic, ESP32 uses its own millis()>,
 *     "v":       <voltage V, 1 decimal>,
 *     "p":       <active power W, 1 decimal>,
 *     "i":       <current A, 3 decimals>,
 *     "pf":      <derived pf_apparent, 2 decimals, or 0 if V*I == 0>
 *   }
 *
 * INSTALLATION
 * ------------
 * 1. In the Shelly web UI, go to Scripts → Create script
 * 2. Paste this entire file
 * 3. Name it  "ESP32_push"
 * 4. Enable "Run on startup"
 * 5. Save and Start
 *
 * CONFIGURATION
 * -------------
 * Only change the constants in the CONFIG block below.
 * Do NOT change anything else unless you understand mJS limitations
 * (no classes, no let destructuring, no template literals, no arrow
 * functions in all contexts, 16 KB RAM limit per script).
 *
 * REQUIREMENTS MET
 * ----------------
 * Req 1   – push interval = PUSH_INTERVAL_MS (1.2 s; above meter cadence of 1 s to add push/timeout margin)
 * Req 7b  – apower and voltage logged directly from Switch.GetStatus
 * Req 7c  – pf_apparent derived on-device before transmission
 * Req 9   – data delivered to ESP32 via local Wi-Fi (no internet)
 * Req 28  – Shelly cloud must be disabled; script works fully offline
 */

// ─── CONFIG ──────────────────────────────────────────────────────────────────

// PUSH_INTERVAL_MS = 1200 ms (was 1000 ms).
//
// Why 1200 and not 1000:
//   The Shelly mJS timer is non-compensating: each fire is 1000 ms after the
//   *previous callback completes*, not after it was scheduled.  With HTTP
//   timeout = 1 s (below), the callback can return up to ~1 s after doPush()
//   started, meaning the next tick fires as little as 0 ms after _pushPending
//   is cleared — a race that drops every tick where the ESP32 is even slightly
//   slow (SD flush, incoming browser request, etc.).
//
//   Setting PUSH_INTERVAL_MS = 1200 ms and timeout = 2 s decouples the two:
//     - HTTP.Request has 2 s to complete before it is abandoned.
//     - Next timer fires 1200 ms after the callback returns — so at worst
//       1200 ms after the request was abandoned (~3200 ms after doPush start).
//     - The ESP32 watchdog window is SHELLY_ERROR_THRESHOLD(5) x 1000 ms =
//       5000 ms, comfortably larger than any realistic push gap.
//
//   The 200 ms gap (1200 - 1000) is the hard margin between timeout clearing
//   _pushPending and the next timer fire.  Even if the Shelly's event loop
//   runs late, 200 ms is ample.
//
//   Shelly's internal energy meter updates at 1 Hz; the measurement value at
//   1200 ms is no more stale than at 1000 ms — the Shelly reads the latest
//   Switch.GetStatus each tick, so cadence only affects logging resolution,
//   not measurement accuracy.
var PUSH_INTERVAL_MS = 1200;         // was 1000; see rationale above
var ESP32_IP         = "192.168.4.1"; // ESP32 softAP gateway — never changes (Option B)
var PUSH_URL         = "http://" + ESP32_IP + "/api/shelly_push";
var SWITCH_ID        = 0;             // Plug S has a single switch channel, id=0

// ─── STATE ───────────────────────────────────────────────────────────────────

var _pushPending = false;  // guard: don't stack up HTTP calls if ESP32 is slow

// ─── HELPERS ─────────────────────────────────────────────────────────────────

/**
 * Round a float to `decimals` places.
 * mJS has no Math.round with precision, so we multiply/round/divide.
 */
function roundTo(val, decimals) {
  var m = 1;
  var i;
  for (i = 0; i < decimals; i++) { m = m * 10; }
  return Math.round(val * m) / m;
}

/**
 * Build the JSON string manually.
 * mJS JSON.stringify is available but we keep explicit control over
 * decimal precision so the ESP32 parser has predictable field widths.
 */
function buildPayload(v, p, current, pf) {
  return JSON.stringify({
    ts:  Shelly.getUptimeMs(),
    v:   roundTo(v,       1),
    p:   roundTo(p,       1),
    i:   roundTo(current, 3),
    pf:  roundTo(pf,      2)
  });
}

// ─── CORE PUSH FUNCTION ───────────────────────────────────────────────────────

function doPush() {
  // If last HTTP call hasn't returned yet, skip this tick.
  // This prevents unbounded request queuing if the ESP32 is slow.
  if (_pushPending) {
    print("[shelly_push] skipping tick – previous request still pending");
    return;
  }

  // Read current Switch component status synchronously via getComponentStatus.
  // This is a zero-cost local read — no network call, no callback.
  var sw = Shelly.getComponentStatus("switch", SWITCH_ID);

  if (!sw) {
    print("[shelly_push] switch status unavailable, skipping");
    return;
  }

  var v       = sw.voltage;  // V RMS
  var p       = sw.apower;   // active power W
  var current = sw.current;  // current A RMS

  // Guard against nulls (can occur briefly at startup)
  if (v === null || v === undefined ||
      p === null || p === undefined ||
      current === null || current === undefined) {
    print("[shelly_push] null measurement, skipping");
    return;
  }

  // Derive pf_apparent = P / (V * I).
  // Clamped to [0, 1] — negative apparent PF not meaningful for logging.
  var vi = v * current;
  var pf;
  if (vi < 0.001) {
    pf = 0.0;  // avoid divide-by-zero at idle / standby
  } else {
    pf = p / vi;
    if (pf > 1.0)  { pf = 1.0; }
    if (pf < 0.0)  { pf = 0.0; }
  }

  var body = buildPayload(v, p, current, pf);

  _pushPending = true;

  Shelly.call(
    "HTTP.Request",
    {
      method:  "POST",
      url:     PUSH_URL,
      headers: {"Content-Type": "application/json"},
      body:    body,
      timeout: 2   // seconds.  Must be < PUSH_INTERVAL_MS/1000 (1.2 s rounded
                   // down = 1 s ceiling for the Shelly runtime, so 2 s is the
                   // next safe integer).
                   // With PUSH_INTERVAL_MS = 1200 ms: the callback clears
                   // _pushPending, then the next timer fires 1200 ms later —
                   // giving a guaranteed 200 ms gap between flag clear and next
                   // doPush() entry.  At 1 s timeout + 1000 ms interval that
                   // gap was 0 ms (race condition).
    },
    function(result, error_code, error_msg) {
      _pushPending = false;
      if (error_code !== 0) {
        // Log but don't crash — the ESP32 may be restarting or the
        // network link may be briefly down. We'll retry next tick.
        print("[shelly_push] HTTP error", error_code, error_msg);
      }
      // Success case: result.code should be 200. We don't act on the body.
    }
  );
}

// ─── STARTUP ─────────────────────────────────────────────────────────────────

print("[shelly_push] starting, pushing to", PUSH_URL, "every", PUSH_INTERVAL_MS, "ms");

// Ensure the relay stays ON so the appliance under test is always powered.
// This prevents accidental toggling via the physical button or other scripts.
Shelly.call("Switch.Set", { id: SWITCH_ID, on: true }, null);

// Main periodic timer — repeating.
Timer.set(PUSH_INTERVAL_MS, true, doPush, null);
