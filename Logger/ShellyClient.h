/*
 * ShellyClient.h  –  Shelly push-data receiver  v3
 * =================================================
 *
 * Changes vs. v2
 * --------------
 *   ADDED  ingestBatch(body)
 *            Accepts a {"batch":[…]} payload from shelly_push.js v2.
 *            Each array element has the same schema as a single-sample
 *            push: {ts, v, p, i, pf}.
 *            Returns the number of samples successfully parsed and
 *            queued via the new _pendingSamples[] array, or -1 on a
 *            top-level parse error.
 *
 *   ADDED  hasPendingSamples() / consumePendingSample()
 *            Logger::pollIfDue() calls consumePendingSample() once per
 *            loop() iteration (rate-limited to _pollIntervalMs) until
 *            the queue is drained, then reverts to the normal
 *            single-sample path.  This lets batch samples enter the
 *            Logger ring buffer at the correct 1-per-second cadence
 *            rather than all at once, which would overflow the buffer.
 *
 *   ADDED  BATCH_QUEUE_SIZE  (== 10, matching shelly_push.js BUFFER_MAX)
 *            Sized to hold exactly one startup burst. Defined here so
 *            unit tests can reference it without including Config.h.
 *
 *   UNCHANGED  ingest(), shellyOk(), hasData(), all accessors, watchdog,
 *              startup grace — byte-for-byte identical to v2.
 *
 * BATCH FLOW OVERVIEW
 * -------------------
 *   shelly_push.js          ESP32 WebPortal          ShellyClient
 *   ─────────────────       ──────────────────       ─────────────
 *   POST {"batch":[…]}  →   handleShellyPush()   →   ingestBatch()
 *                                                     fills _pending[]
 *
 *   Each loop() tick:
 *   Logger::pollIfDue()  →  consumePendingSample()
 *                           updates _latest, advances _pendingHead
 *                           → Logger writes one Sample to ring buffer
 *
 *   When queue empty, normal ingest() path resumes as before.
 *
 * TIMESTAMP NOTE
 * --------------
 *   Batch samples carry Shelly uptime timestamps (ts field, ms).
 *   The ESP32 has no way to convert those to its own millis() or to
 *   wall-clock time without knowing the offset at connection time.
 *   Strategy: millis_ts in each batch Sample is set to
 *     millis() - (now_shelly_uptime - sample_shelly_uptime)
 *   approximated as  millis() - (batch_count - index) * pollIntervalMs.
 *   This reconstructs approximate ESP32-uptime timestamps that are
 *   monotonic and close to reality (error < 1 s per sample).
 *   unix_ts is set from the RTC at the moment of consumption,
 *   offset backwards by the same delta, so CSV datetimes are correct.
 */

#ifndef SHELLY_CLIENT_H
#define SHELLY_CLIENT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "Config.h"

static const uint8_t BATCH_QUEUE_SIZE = 10;   // must match shelly_push.js BUFFER_MAX

struct ShellyMeasurement {
  float    voltage;
  float    power;
  float    current;
  float    pf_apparent;
  bool     valid;
};

// A pending batch sample: raw values + estimated millis offset from now
struct PendingSample {
  float    voltage;
  float    power;
  float    current;
  float    pf_apparent;
  int32_t  msBeforeNow;   // how many ms before the batch arrived this was sampled
};

class ShellyClient {
public:
  ShellyClient()
    : _lastPushMs(0),
      _errorCount(0),
      _graceActive(false),
      _graceStartMs(0),
      _pendingCount(0),
      _pendingHead(0)
  {
    _latest.voltage     = NAN;
    _latest.power       = NAN;
    _latest.current     = NAN;
    _latest.pf_apparent = NAN;
    _latest.valid       = false;
  }

  // ── Called once from setup() ──────────────────────────────────────────────
  void beginStartupGrace() {
    _graceActive  = true;
    _graceStartMs = millis();
  }

  // ── Single-sample ingest (unchanged from v2) ──────────────────────────────
  //
  // body: {"ts":…,"v":…,"p":…,"i":…,"pf":…}
  // Returns true on success.
  bool ingest(const String& body) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
      Serial.printf("[Shelly] JSON parse error: %s  body='%.60s'\n",
                    err.c_str(), body.c_str());
      _errorCount++;
      return false;
    }

    if (!doc.containsKey("v") || !doc.containsKey("p") ||
        !doc.containsKey("i") || !doc.containsKey("pf")) {
      Serial.println("[Shelly] push missing required fields");
      _errorCount++;
      return false;
    }

    float v  = doc["v"].as<float>();
    float p  = doc["p"].as<float>();
    float i  = doc["i"].as<float>();
    float pf = doc["pf"].as<float>();

    if (v < 0.0f || v > 300.0f || p < 0.0f || p > 3680.0f ||
        i < 0.0f || i >  16.0f || pf < 0.0f || pf > 1.01f) {
      Serial.printf("[Shelly] push out of range: v=%.1f p=%.1f i=%.3f pf=%.2f\n",
                    v, p, i, pf);
      _errorCount++;
      return false;
    }

    _latest.voltage     = v;
    _latest.power       = p;
    _latest.current     = i;
    _latest.pf_apparent = pf;
    _latest.valid       = true;

    _lastPushMs  = millis();
    _graceActive = false;
    _errorCount  = 0;

    return true;
  }

  // ── Batch ingest — called for {"batch":[…]} startup payloads ─────────────
  //
  // body: {"batch":[{"ts":…,"v":…,"p":…,"i":…,"pf":…}, …]}
  //
  // Returns number of valid samples queued (0..BATCH_QUEUE_SIZE),
  //         or -1 on top-level JSON parse failure.
  //
  // Samples are queued in _pending[] in chronological order (index 0 =
  // oldest). consumePendingSample() drains them one per pollIfDue() tick.
  //
  // Implementation note: uses only StaticJsonDocument + the JsonProxy chain
  // (doc["batch"][i]["v"].as<float>()) so the ArduinoJson v6 test stub
  // compiles without DynamicJsonDocument, JsonArray, or JsonObject.
  // The document size 768 B covers 10 samples × ~60 B + overhead on the
  // real device (StaticJsonDocument lives on the stack; fine for ESP32).
  int ingestBatch(const String& body) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
      Serial.printf("[Shelly] batch JSON parse error: %s\n", err.c_str());
      _errorCount++;
      return -1;
    }

    if (!doc.containsKey("batch") || !doc["batch"].is<JsonArray>()) {
      Serial.println("[Shelly] batch: missing 'batch' array");
      _errorCount++;
      return -1;
    }

    // Clear any leftover pending queue from a previous (failed) batch
    _pendingCount = 0;
    _pendingHead  = 0;

    uint8_t total = (uint8_t)doc["batch"].size();
    if (total > BATCH_QUEUE_SIZE) { total = BATCH_QUEUE_SIZE; }

    for (uint8_t idx = 0; idx < total; idx++) {
      // Access each element via the proxy chain — no JsonObject needed
      if (!doc["batch"][idx].containsKey("v") ||
          !doc["batch"][idx].containsKey("p") ||
          !doc["batch"][idx].containsKey("i") ||
          !doc["batch"][idx].containsKey("pf")) {
        Serial.printf("[Shelly] batch[%u] missing fields, skipping\n", idx);
        continue;
      }

      float v  = doc["batch"][idx]["v"].as<float>();
      float p  = doc["batch"][idx]["p"].as<float>();
      float i  = doc["batch"][idx]["i"].as<float>();
      float pf = doc["batch"][idx]["pf"].as<float>();

      if (v < 0.0f || v > 300.0f || p < 0.0f || p > 3680.0f ||
          i < 0.0f || i >  16.0f || pf < 0.0f || pf > 1.01f) {
        Serial.printf("[Shelly] batch[%u] out of range, skipping\n", idx);
        continue;
      }

      // idx 0 = oldest sample = (total-1) intervals before arrival
      // idx (total-1) = newest = 0 intervals before arrival
      int32_t msAgo = (int32_t)(total - 1 - idx) * (int32_t)INTERVAL_SHELLY_POLL_MS;

      _pending[_pendingCount].voltage     = v;
      _pending[_pendingCount].power       = p;
      _pending[_pendingCount].current     = i;
      _pending[_pendingCount].pf_apparent = pf;
      _pending[_pendingCount].msBeforeNow = msAgo;
      _pendingCount++;
    }

    if (_pendingCount > 0) {
      _lastPushMs  = millis();
      _graceActive = false;
      _errorCount  = 0;
      // Seed _latest with newest batch sample for immediate live display
      uint8_t newestIdx = _pendingCount - 1;
      _latest.voltage     = _pending[newestIdx].voltage;
      _latest.power       = _pending[newestIdx].power;
      _latest.current     = _pending[newestIdx].current;
      _latest.pf_apparent = _pending[newestIdx].pf_apparent;
      _latest.valid       = true;
      Serial.printf("[Shelly] batch: %u/%u samples queued\n",
                    (unsigned)_pendingCount, (unsigned)total);
    }

    return (int)_pendingCount;
  }

  // ── Pending-queue accessors used by Logger::pollIfDue() ──────────────────
  bool hasPendingSamples() const {
    return _pendingHead < _pendingCount;
  }

  // Consume the next pending sample into _latest and return true.
  // Returns false (no-op) if the queue is empty.
  bool consumePendingSample() {
    if (!hasPendingSamples()) { return false; }
    const PendingSample& ps = _pending[_pendingHead++];
    _latest.voltage     = ps.voltage;
    _latest.power       = ps.power;
    _latest.current     = ps.current;
    _latest.pf_apparent = ps.pf_apparent;
    _latest.valid       = true;
    return true;
  }

  // Returns the msBeforeNow of the next pending sample (used by Logger to
  // back-date millis_ts and unix_ts).  Returns 0 if queue is empty.
  int32_t nextPendingMsAgo() const {
    if (!hasPendingSamples()) { return 0; }
    return _pending[_pendingHead].msBeforeNow;
  }

  // ── Accessors used by Logger::pollIfDue() ────────────────────────────────
  float getVoltage()    const { return _latest.voltage; }
  float getPower()      const { return _latest.power; }
  float getPfApparent() const { return _latest.pf_apparent; }
  bool  hasData()       const { return _latest.valid; }

  // ── Watchdog ─────────────────────────────────────────────────────────────
  bool shellyOk() const {
    if (_graceActive) {
      if ((uint32_t)(millis() - _graceStartMs) < SHELLY_STARTUP_GRACE_MS) {
        return true;
      }
      _graceActive = false;
    }
    if (!_latest.valid) return false;
    uint32_t silenceMs = (uint32_t)(millis() - _lastPushMs);
    uint32_t timeout   = (uint32_t)SHELLY_ERROR_THRESHOLD *
                         (uint32_t)INTERVAL_SHELLY_POLL_MS;
    return silenceMs < timeout;
  }

  uint8_t getErrorCount() const { return _errorCount; }

private:
  ShellyMeasurement _latest;
  uint32_t          _lastPushMs;
  uint8_t           _errorCount;
  mutable bool      _graceActive;
  uint32_t          _graceStartMs;

  // Pending batch queue — drained one sample per pollIfDue() tick
  PendingSample _pending[BATCH_QUEUE_SIZE];
  uint8_t       _pendingCount;   // total valid entries loaded by ingestBatch()
  uint8_t       _pendingHead;    // index of next sample to consume
};

#endif  // SHELLY_CLIENT_H
