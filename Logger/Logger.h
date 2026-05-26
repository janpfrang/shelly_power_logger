/*
 * Logger.h v5  (Shelly + ESP32)
 * ==============================
 *
 * Changes vs. v4 (PZEM/ESP32)
 * ----------------------------
 *   REMOVED  #include <PZEM004Tv30.h>
 *   REMOVED  PZEM004Tv30 _pzem member, constructor init, UART2 usage
 *   REMOVED  _pzemErrorCount (replaced by ShellyClient::shellyOk())
 *   ADDED    ShellyClient& _shelly reference (injected by main .ino)
 *   CHANGED  pollIfDue()  — reads from _shelly instead of _pzem.*
 *   CHANGED  pzemOk()     — renamed to shellyOk(), delegates to _shelly
 *   CHANGED  ok()         — now checks shellyOk() && _sdOk
 *   CHANGED  constructor  — takes ShellyClient& parameter
 *   CHANGED  _pollIntervalMs default: 500 → 1000 ms
 *   UNCHANGED: flushToSD(), resetSDFile(), pushSample(), tryRecoverSD(),
 *              initSDCard(), all getter methods, RAM buffer logic,
 *              SD FIFO-drop on overflow, SD retry every 30 s.
 *
 * The Sample struct field "pf" is kept as-is in RAM.
 * The CSV header uses "pf_apparent" (Config.h LOG_FILE_HEADER).
 * No struct change is needed — the field stores whatever the caller puts in it.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "Config.h"
#include "ShellyClient.h"

struct Sample {
  uint32_t millis_ts;
  float    voltage_V;
  float    power_W;
  float    pf;          // stores pf_apparent in this firmware version
};

class Logger {
public:
  // ShellyClient is injected so Logger does not own or initialise it.
  // This mirrors how v4 owned _pzem but makes the dependency explicit.
  explicit Logger(ShellyClient& shelly)
    : _shelly(shelly),
      _sdSPI(VSPI),
      _bufferCount(0),
      _droppedSamples(0),
      _lastPollMs(0),
      _lastFlushMs(0),
      _lastSdRetryMs(0),
      _lastVoltage(NAN),
      _lastPower(NAN),
      _lastPf(NAN),
      _sdOk(false),
      _pollIntervalMs(INTERVAL_SHELLY_POLL_MS),   // 1000 ms default
      _powerThresholdW(DEFAULT_POWER_THRESHOLD_W)
  {}

  bool begin() {
    _sdSPI.begin(PIN_SD_CLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    _sdOk = initSDCard();
    Serial.printf("[Logger] SD=%s\n", _sdOk ? "OK" : "FEHLER");
    return _sdOk;
  }

  // ── Runtime-adjustable settings (called from WebPortal) ──────────────────
  void     setPollInterval(uint32_t ms) { if (ms >= 1000) _pollIntervalMs = ms; }
  uint32_t getPollInterval()  const     { return _pollIntervalMs; }

  void  setPowerThreshold(float watts)  { _powerThresholdW = watts; }
  float getPowerThreshold()   const     { return _powerThresholdW; }

  // ── Called from loop() every iteration ───────────────────────────────────
  //
  // pollIfDue() used to call _pzem.voltage() etc. synchronously.
  // Now it reads the last value cached by ShellyClient::ingest().
  // The "poll interval" is still honoured — we don't push a sample to the
  // ring-buffer more often than _pollIntervalMs even if the Shelly pushes
  // faster (it shouldn't, but belt-and-suspenders).
  void pollIfDue() {
    uint32_t now = millis();
    if (now - _lastPollMs < _pollIntervalMs) return;
    _lastPollMs = now;

    // If the Shelly watchdog has timed out or data is not yet valid,
    // do not push a sample — just update live display fields to NAN so
    // the web UI shows "—" and the LED turns red.
    if (!_shelly.shellyOk() || !_shelly.hasData()) {
      _lastVoltage = NAN;
      _lastPower   = NAN;
      _lastPf      = NAN;
      return;
    }

    float V  = _shelly.getVoltage();
    float P  = _shelly.getPower();
    float PF = _shelly.getPfApparent();

    // Update live-display cache (shown in /api/live regardless of threshold)
    _lastVoltage = V;
    _lastPower   = P;
    _lastPf      = PF;

    // Apply logging threshold (Req 8): only store sample if P >= threshold
    if (P >= _powerThresholdW) {
      pushSample({ now, V, P, PF });
    }
  }

  // ── SD flush on schedule (unchanged from v4) ─────────────────────────────
  void flushIfDue() {
    uint32_t now = millis();
    if (now - _lastFlushMs < INTERVAL_SD_FLUSH_MS) return;
    _lastFlushMs = now;

    if (!_sdOk) {
      tryRecoverSD();
      return;
    }
    flushToSD();
  }

  // ── Public flush (called by /download handler before streaming) ──────────
  bool flushToSD() {
    if (!_sdOk || _bufferCount == 0) return _sdOk;

    File f = SD.open(LOG_FILE_PATH, FILE_APPEND);
    if (!f) {
      Serial.println("[Logger] SD-Open fehlgeschlagen, markiere SD als defekt");
      _sdOk = false;
      return false;
    }

    for (size_t i = 0; i < _bufferCount; i++) {
      f.printf("%lu,%.1f,%.1f,%.2f\n",
               (unsigned long)_buffer[i].millis_ts,
               _buffer[i].voltage_V,
               _buffer[i].power_W,
               _buffer[i].pf);
    }
    f.flush();
    f.close();

    Serial.printf("[Logger] %u Samples auf SD geschrieben\n",
                  (unsigned)_bufferCount);
    _bufferCount = 0;
    return true;
  }

  // ── Reset log file (POST /reset handler) — unchanged from v4 ─────────────
  bool resetSDFile() {
    if (!_sdOk) return false;
    _bufferCount = 0;
    if (SD.exists(LOG_FILE_PATH)) {
      if (!SD.remove(LOG_FILE_PATH)) {
        Serial.println("[Logger] SD.remove fehlgeschlagen");
        return false;
      }
    }
    File f = SD.open(LOG_FILE_PATH, FILE_WRITE);
    if (!f) return false;
    f.println(LOG_FILE_HEADER);
    f.close();
    Serial.println("[Logger] Log-Datei zurückgesetzt");
    return true;
  }

  // ── Status accessors ─────────────────────────────────────────────────────
  bool     shellyOk()          const { return _shelly.shellyOk(); }
  bool     sdOk()              const { return _sdOk; }
  bool     ok()                const { return shellyOk() && _sdOk; }  // drives LED
  float    getLastVoltage()    const { return _lastVoltage; }
  float    getLastPower()      const { return _lastPower; }
  float    getLastPf()         const { return _lastPf; }
  size_t   getBufferCount()    const { return _bufferCount; }
  uint32_t getDroppedSamples() const { return _droppedSamples; }

  File openLogFileForRead() {
    if (!_sdOk) return File();
    return SD.open(LOG_FILE_PATH, FILE_READ);
  }

private:
  ShellyClient& _shelly;      // injected reference — not owned
  SPIClass      _sdSPI;
  Sample        _buffer[RAM_BUFFER_SIZE];
  size_t        _bufferCount;
  uint32_t      _droppedSamples;
  uint32_t      _lastPollMs;
  uint32_t      _lastFlushMs;
  uint32_t      _lastSdRetryMs;
  float         _lastVoltage;
  float         _lastPower;
  float         _lastPf;
  bool          _sdOk;
  uint32_t      _pollIntervalMs;
  float         _powerThresholdW;

  // ── Ring-buffer push with FIFO-drop on overflow (unchanged from v4) ───────
  void pushSample(const Sample& s) {
    if (_bufferCount < RAM_BUFFER_SIZE) {
      _buffer[_bufferCount++] = s;
      return;
    }
    // Buffer full: try an emergency SD flush
    if (_sdOk && flushToSD()) {
      _buffer[_bufferCount++] = s;
      return;
    }
    // SD also unavailable: drop oldest sample (FIFO)
    memmove(&_buffer[0], &_buffer[1],
            (RAM_BUFFER_SIZE - 1) * sizeof(Sample));
    _buffer[RAM_BUFFER_SIZE - 1] = s;
    _droppedSamples++;
    if (_droppedSamples == 1 || _droppedSamples % 100 == 0) {
      Serial.printf("[Logger] WARNUNG: Sample verworfen (gesamt: %lu)\n",
                    (unsigned long)_droppedSamples);
    }
  }

  bool initSDCard() {
    return SD.begin(PIN_SD_CS, _sdSPI, 4000000);
  }

  // ── SD auto-recovery every 30 s (unchanged from v4) ──────────────────────
  void tryRecoverSD() {
    uint32_t now = millis();
    if (now - _lastSdRetryMs < 30000) return;
    _lastSdRetryMs = now;

    Serial.println("[Logger] Versuche SD-Karte neu zu initialisieren...");
    SD.end();
    delay(50);
    _sdOk = initSDCard();
    if (_sdOk) {
      if (!SD.exists(LOG_FILE_PATH)) {
        File f = SD.open(LOG_FILE_PATH, FILE_WRITE);
        if (f) { f.println(LOG_FILE_HEADER); f.close(); }
      }
      Serial.println("[Logger] SD wieder verfügbar");
    }
  }
};

// Called once from setup() to ensure the log file has a header row.
inline void ensureLogHeader() {
  if (!SD.exists(LOG_FILE_PATH)) {
    File f = SD.open(LOG_FILE_PATH, FILE_WRITE);
    if (f) {
      f.println(LOG_FILE_HEADER);
      f.close();
    }
  }
}

#endif  // LOGGER_H
