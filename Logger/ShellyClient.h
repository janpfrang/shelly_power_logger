/*
 * ShellyClient.h  –  Shelly push-data receiver  (v1)
 * ====================================================
 *
 * DESIGN RATIONALE
 * ----------------
 * In the PZEM architecture, Logger.h owned the sensor: it called
 * _pzem.voltage(), _pzem.power(), _pzem.pf() synchronously on a timer.
 *
 * In the Shelly architecture the data flow is REVERSED:
 *   • The Shelly script (shelly_push.js) POSTS JSON to /api/shelly_push
 *     on the ESP32 every 1 s.
 *   • WebPortal.cpp calls ShellyClient::ingest() when that POST arrives.
 *   • Logger.h calls ShellyClient::getLastSample() in pollIfDue() —
 *     the same call-site as before, just reading a cached value instead
 *     of querying hardware.
 *
 * This separation keeps Logger.h almost identical to v4 and makes the
 * data path testable independently of the web server.
 *
 * WATCHDOG
 * --------
 * ShellyClient tracks the time of the last successful ingest.
 * If no push arrives within SHELLY_ERROR_THRESHOLD × INTERVAL_SHELLY_POLL_MS,
 * shellyOk() returns false → Logger::ok() → LED_ERROR (Req 15/16).
 *
 * THREAD SAFETY
 * -------------
 * The ESP32 Arduino WebServer runs on the same core/task as loop().
 * WebServer::handleClient() is called from loop(); it runs the POST handler
 * synchronously in the same execution context.  No RTOS mutexes needed.
 */

#ifndef SHELLY_CLIENT_H
#define SHELLY_CLIENT_H

#include <Arduino.h>
#include <ArduinoJson.h>   // Benoit Blanchon — add to Arduino Library Manager
#include "Config.h"

// ShellyMeasurement is the decoded form of one push from shelly_push.js.
// Field names mirror the JSON keys to make ingest() readable.
struct ShellyMeasurement {
  float    voltage;     // V RMS
  float    power;       // active power W  (apower)
  float    current;     // A RMS
  float    pf_apparent; // derived: power / (voltage × current)
  bool     valid;       // false until the first successful push arrives
};

class ShellyClient {
public:
  ShellyClient()
    : _lastPushMs(0),
      _errorCount(0)
  {
    _latest.voltage     = NAN;
    _latest.power       = NAN;
    _latest.current     = NAN;
    _latest.pf_apparent = NAN;
    _latest.valid       = false;
  }

  // ── Called by WebPortal when POST /api/shelly_push arrives ─────────────────
  //
  // body: raw HTTP request body string, expected JSON:
  //   { "ts": <ms>, "v": <V>, "p": <W>, "i": <A>, "pf": <0-1> }
  //
  // Returns true if the body parsed correctly, false on malformed JSON.
  bool ingest(const String& body) {
    // StaticJsonDocument lives on the stack — no heap allocation.
    // 192 bytes is sufficient for our 5-field payload.
    StaticJsonDocument<192> doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
      Serial.printf("[Shelly] JSON parse error: %s  body='%.60s'\n",
                    err.c_str(), body.c_str());
      _errorCount++;
      return false;
    }

    // Validate required fields exist and are numeric
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

    // Sanity bounds — reject physically impossible values
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

    _lastPushMs = millis();
    _errorCount = 0;   // successful push clears consecutive error count

    return true;
  }

  // ── Accessors used by Logger::pollIfDue() ───────────────────────────────────
  float getVoltage()    const { return _latest.voltage; }
  float getPower()      const { return _latest.power; }
  float getPfApparent() const { return _latest.pf_apparent; }
  bool  hasData()       const { return _latest.valid; }

  // ── Watchdog: returns false if no push for THRESHOLD × INTERVAL ms ──────────
  bool shellyOk() const {
    if (!_latest.valid) return false;   // never received anything
    uint32_t silenceMs = (uint32_t)(millis() - _lastPushMs);
    uint32_t timeout   = (uint32_t)SHELLY_ERROR_THRESHOLD *
                         (uint32_t)INTERVAL_SHELLY_POLL_MS;
    return silenceMs < timeout;
  }

  // Expose error count for /api/live diagnostic field
  uint8_t getErrorCount() const { return _errorCount; }

private:
  ShellyMeasurement _latest;
  uint32_t          _lastPushMs;
  uint8_t           _errorCount;
};

#endif  // SHELLY_CLIENT_H
