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
 * Every PUSH_INTERVAL_MS (default: 1000 ms) the script:
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
 * Req 1   – push interval = PUSH_INTERVAL_MS (default 1 s, matches meter cadence)
 * Req 7b  – apower and voltage logged directly from Switch.GetStatus
 * Req 7c  – pf_apparent derived on-device before transmission
 * Req 9   – data delivered to ESP32 via local Wi-Fi (no internet)
 * Req 28  – Shelly cloud must be disabled; script works fully offline
 */

// ─── CONFIG ──────────────────────────────────────────────────────────────────

var PUSH_INTERVAL_MS = 1000;          // Must match ESP32 INTERVAL_SHELLY_POLL_MS
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
      timeout: 1   // seconds; MUST be < PUSH_INTERVAL_MS/1000 (1 s) so _pushPending
                   // is always cleared before the next tick fires.
                   // At 2 s the flag could stay set across a tick boundary during
                   // ESP32 boot/reconnect, causing the watchdog to trip permanently.
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
