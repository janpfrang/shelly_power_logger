/*
 * Logger.h v8  (Shelly + ESP32 + OTA-aware + DS3231 RTC + supply monitoring)
 * ===========================================================================
 *
 * Changes vs. v7
 * --------------
 *   CHANGED  Sample struct -- added float supply_V and uint8_t power_down.
 *              supply_V:    9V-rail voltage at the time of the sample (V, 2 dp).
 *                           Single analogReadMilliVolts() call, NOT the 16x
 *                           oversampled read in PowerMonitor -- sufficient for
 *                           CSV documentation, not the safety-critical path.
 *              power_down:  0 = rail above threshold / normal.
 *                           1 = PowerMonitor has latched an undervoltage event.
 *                           The 1-samples are the last entries before the
 *                           graceful shutdown flush -- exactly the interesting
 *                           moments in the CSV.
 *   CHANGED  pollIfDue()  -- reads PIN_VSUPPLY once per poll tick and reads
 *                           powerMonitor.isPowerLost() state; populates both
 *                           new fields in every pushed Sample.
 *              RAM cost: +5 bytes/sample (float=4 + uint8_t=1). 64 samples
 *              = +320 bytes.  ESP32 has 320 kB SRAM -- negligible.
 *              Time cost: one analogReadMilliVolts() ≈ 70 µs, once per second.
 *              Both costs are immaterial.
 *   CHANGED  flushToSD()  -- format string extended: "...%.2f,%u\n" for the
 *                           two new columns.
 *   CHANGED  LOG_FILE_HEADER -- 'supply_V,power_down' appended (Config.h).
 *
 *   COMPATIBILITY NOTE (existing log.csv files):
 *              ensureLogHeader() only writes a header if the file does NOT
 *              exist.  A pre-existing log.csv will get new 7-column rows
 *              appended after old 5-column rows -- the header won't match.
 *              Fix: call POST /reset via the WebPortal once after flashing.
 *              A Serial warning is printed by ensureLogHeader() if it detects
 *              a file already exists when this firmware version boots.
 *
 * Changes vs. v5
 * --------------
 *   ADDED  _otaInProgress flag (bool, default false)
 *   ADDED  setOtaInProgress(bool)  -- called by WebPortal OTA handlers
 *   ADDED  isOtaInProgress()       -- read by WebPortal for /api/live response
 *   CHANGED pollIfDue()   -- returns immediately when OTA is active
 *   CHANGED flushIfDue()  -- returns immediately when OTA is active;
 *                           flushes RAM buffer to SD BEFORE yielding to OTA
 *                           (called explicitly by WebPortal before Update.begin())
 *   ADDED  RTC DS3231 support via RTClib (Adafruit).
 *          Logger accepts an optional RTC_DS3231* (default nullptr) so the
 *          unit-test double (which calls Logger(sc)) keeps compiling without
 *          any test-side changes. When nullptr, unix_ts = 0 and CSV writes
 *          'RTC_NOT_SET' -- graceful degradation, not a hard failure.
 *   CHANGED Sample struct -- added uint32_t unix_ts (UTC epoch from DS3231).
 *           millis_ts kept for uptime / debug correlation.
 *   CHANGED flushToSD() -- writes ISO-8601 datetime as first CSV column.
 *   CHANGED LOG_FILE_HEADER -- 'datetime' prepended (Config.h).
 *   UNCHANGED: everything else -- ShellyClient, SD logic, RAM buffer,
 *              FIFO-drop, SD recovery, all getters.
 *
 * OTA DESIGN RATIONALE
 * --------------------
 * OTA is handled entirely in WebPortal (POST /update route, Update.h).
 * Logger.h does NOT own or include Update.h -- that would violate the
 * single-responsibility split that makes this codebase testable.
 *
 * What Logger.h DOES contribute:
 *
 *   1. PAUSE during flash  -- pollIfDue() and flushIfDue() are no-ops while
 *      _otaInProgress is true.  This prevents:
 *        * a concurrent SD write from corrupting the filesystem mid-flash
 *          (SD.open / SD.close share the same SPI bus; Update.h uses flash,
 *           not SPI, but the loop() timing jitter could still interleave)
 *        * misleading NAN values being cached while the Shelly watchdog trips
 *          (it will trip -- that is expected and cosmetic; see note below)
 *
 *   2. PRE-FLUSH before flash -- WebPortal calls flushToSD() explicitly just
 *      before calling Update.begin().  This ensures no logged samples are
 *      lost when the ESP32 reboots at the end of the update.
 *      flushToSD() is already public and unchanged.
 *
 * WATCHDOG NOTE
 * -------------
 * The Shelly push watchdog (SHELLY_ERROR_THRESHOLD x INTERVAL_SHELLY_POLL_MS
 * = 3 s) WILL trip during a firmware upload (typically 5-15 s for a 1 MB
 * binary over Wi-Fi at ~1 Mbps).  The LED will blink at 5 Hz.
 * This is expected behaviour and resets automatically when the device
 * reboots and the Shelly resumes pushing.  No user action required.
 *
 * The Sample struct field "pf" is kept as-is in RAM.
 * The CSV header uses "pf_apparent" (Config.h LOG_FILE_HEADER).
 * No struct change is needed -- the field stores whatever the caller puts in it.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "Config.h"
#include "RTClib.h"        // Adafruit RTClib -- DS3231 driver
#include "ShellyClient.h"

struct Sample {
  uint32_t millis_ts;   // ESP32 uptime ms at time of sample
  uint32_t unix_ts;     // UTC epoch from DS3231 (0 = RTC absent/not set)
  float    voltage_V;   // mains voltage from Shelly (V RMS)
  float    power_W;     // active power from Shelly (W)
  float    pf;          // stores pf_apparent in this firmware version
  float    supply_V;    // 9V-rail voltage at sample time (V, single ADC read)
  uint8_t  power_down;  // 0=normal, 1=undervoltage latched by PowerMonitor
};

class Logger {
public:
  // ShellyClient is injected; RTC_DS3231 is optional (nullptr = no RTC).
  // The power-loss flag is set externally via setPowerLost(bool) called from
  // loop() after powerMonitor.isPowerLost() -- avoids any dependency on
  // PowerMonitor.h inside Logger.h (prevents Arduino CLI header-ordering issues).
  explicit Logger(ShellyClient& shelly,
                  RTC_DS3231*   rtc = nullptr)
    : _shelly(shelly),
      _rtc(rtc),
      _powerLostFlag(false),
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
      _otaInProgress(false),
      _pollIntervalMs(INTERVAL_SHELLY_POLL_MS),   // 1000 ms default
      _powerThresholdW(DEFAULT_POWER_THRESHOLD_W)
  {}

  bool begin() {
    _sdSPI.begin(PIN_SD_CLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    _sdOk = initSDCard();
    Serial.printf("[Logger] SD=%s\n", _sdOk ? "OK" : "FEHLER");
    if (_sdOk) _openLogFile();   // FAT traversal happens once here only
    return _sdOk;
  }

  // -- Runtime-adjustable settings (called from WebPortal) ------------------
  void     setPollInterval(uint32_t ms) { if (ms >= 1000) _pollIntervalMs = ms; }
  uint32_t getPollInterval()  const     { return _pollIntervalMs; }

  void  setPowerThreshold(float watts)  { _powerThresholdW = watts; }
  float getPowerThreshold()   const     { return _powerThresholdW; }

  // -- OTA gate -- called by WebPortal OTA handlers ---------------------------
  //
  // setOtaInProgress(true)  is called by WebPortal just before Update.begin().
  //   At that point WebPortal MUST also call flushToSD() explicitly so that
  //   any buffered samples are persisted before the reboot that follows.
  //
  // setOtaInProgress(false) is never called in normal flow because the ESP32
  //   reboots at the end of a successful OTA.  It exists so that WebPortal
  //   can clear the flag on a failed/aborted update, restoring normal logging.
  void setOtaInProgress(bool active) {
    if (active && !_otaInProgress) {
      // Flush whatever is in RAM before handing over to the OTA writer.
      // This is a best-effort call -- if SD is unavailable the samples are
      // lost, but that is preferable to leaving the SD file in a torn state.
      flushToSD();
      Serial.println("[Logger] OTA gestartet -- Logging pausiert");
    }
    if (!active && _otaInProgress) {
      Serial.println("[Logger] OTA abgebrochen -- Logging wiederhergestellt");
    }
    _otaInProgress = active;
  }

  bool isOtaInProgress() const { return _otaInProgress; }

  // Called from loop() immediately after powerMonitor.isPowerLost() is checked.
  // Decouples Logger from PowerMonitor.h -- no include dependency needed.
  void setPowerLost(bool lost) { _powerLostFlag = lost; }

  // -- Called from loop() every iteration -----------------------------------
  //
  // pollIfDue() used to call _pzem.voltage() etc. synchronously.
  // Now it reads the last value cached by ShellyClient::ingest().
  // The "poll interval" is still honoured -- we don't push a sample to the
  // ring-buffer more often than _pollIntervalMs even if the Shelly pushes
  // faster (it shouldn't, but belt-and-suspenders).
  void pollIfDue() {
    // Suspend all sampling while a firmware update is being written.
    // The Shelly watchdog will trip (expected -- see header note).
    if (_otaInProgress) return;

    uint32_t now = millis();
    if (now - _lastPollMs < _pollIntervalMs) return;
    _lastPollMs = now;

    // If the Shelly watchdog has timed out or data is not yet valid,
    // do not push a sample -- just update live display fields to NAN so
    // the web UI shows "--" and the LED turns red.
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

    // Single (non-oversampled) ADC read of the 9V rail for the CSV column.
    // PowerMonitor uses 16x oversampling for the safety decision; one read
    // here is sufficient for logging purposes (±0.3 V is fine for post-
    // processing -- the exact trigger voltage is not needed).
    // When POWER_MONITOR_ENABLED == 0 the circuit is absent; PIN_VSUPPLY
    // floats, so we log 0.0 V as a clear "not populated" sentinel.
#if POWER_MONITOR_ENABLED
    uint32_t _supplyMv = analogReadMilliVolts(PIN_VSUPPLY);
    float supplyV = (float)(((uint64_t)_supplyMv *
                    (DIVIDER_R_TOP_OHM + DIVIDER_R_BOTTOM_OHM)) /
                    DIVIDER_R_BOTTOM_OHM) / 1000.0f;
    uint8_t pdFlag = _powerLostFlag ? 1 : 0;
#else
    float   supplyV = 0.0f;
    uint8_t pdFlag  = 0;
#endif

    // Apply logging threshold (Req 8): only store sample if P >= threshold
    if (P >= _powerThresholdW) {
      // Capture wall-clock time from DS3231. Returns 0 when RTC is absent
      // or has lost power -- CSV writes 'RTC_NOT_SET' for those rows.
      uint32_t uts = (_rtc != nullptr) ? (uint32_t)_rtc->now().unixtime() : 0;
      pushSample({ now, uts, V, P, PF, supplyV, pdFlag });
    }
  }

  // -- SD flush on schedule (unchanged from v4) -----------------------------
  void flushIfDue() {
    // Do not touch the SD card while OTA is writing flash.
    // setOtaInProgress(true) already flushed the buffer synchronously.
    if (_otaInProgress) return;

    uint32_t now = millis();
    if (now - _lastFlushMs < INTERVAL_SD_FLUSH_MS) return;
    _lastFlushMs = now;

    if (!_sdOk) {
      tryRecoverSD();
      return;
    }
    flushToSD();
  }

  // -- Public flush (called by /download handler before streaming) ----------
  //
  // Timing guard (fix for issue #6):
  //   _logFile.flush() syncs data to SD media via SPI.  On a healthy card at
  //   4 MHz SPI this takes < 10 ms.  On a marginal card it can stall for
  //   hundreds of ms, blocking the HTTP server and causing Shelly push timeouts.
  //   We time the entire flush call.  If it exceeds SD_FLUSH_TIMEOUT_MS we
  //   set _sdOk = false so the next flushIfDue() calls tryRecoverSD() rather
  //   than blocking again.  The data written in this call is NOT discarded —
  //   the rows are already in the file handle's write buffer before flush() is
  //   called; the sync may have partially succeeded even if it was slow.
  bool flushToSD() {
    if (!_sdOk || _bufferCount == 0) return _sdOk;

    if (!_logFile) _openLogFile();
    if (!_logFile) {
      Serial.println("[Logger] Log-Datei nicht verfuegbar");
      _sdOk = false;
      return false;
    }

    uint32_t t0 = millis();

    for (size_t i = 0; i < _bufferCount; i++) {
      char dtbuf[20];
      if (_buffer[i].unix_ts > 0) {
        DateTime dt(_buffer[i].unix_ts);
        snprintf(dtbuf, sizeof(dtbuf), "%04d-%02d-%02dT%02d:%02d:%02d",
                 dt.year(), dt.month(),  dt.day(),
                 dt.hour(), dt.minute(), dt.second());
      } else {
        snprintf(dtbuf, sizeof(dtbuf), "RTC_NOT_SET");
      }
      _logFile.printf("%s,%lu,%.1f,%.1f,%.2f,%.2f,%u\n",
               dtbuf,
               (unsigned long)_buffer[i].millis_ts,
               _buffer[i].voltage_V,
               _buffer[i].power_W,
               _buffer[i].pf,
               _buffer[i].supply_V,
               (unsigned)_buffer[i].power_down);
    }

    _logFile.flush();   // commit to SD -- this is the blocking call

    uint32_t dt = millis() - t0;
    if (dt > SD_FLUSH_TIMEOUT_MS) {
      // Card is too slow — flag it so tryRecoverSD() runs next cycle instead
      // of blocking here again.  Data already written above is not lost.
      Serial.printf("[Logger] WARNUNG: SD-Flush %lu ms (Limit %d ms) -- SD als fehlerhaft markiert\n",
                    (unsigned long)dt, SD_FLUSH_TIMEOUT_MS);
      _sdOk = false;
      _bufferCount = 0;
      return false;
    }
    if (dt > 50) {
      // Slower than expected but within limit — log it so marginal cards
      // are visible in the serial output without being fatal.
      Serial.printf("[Logger] SD-Flush %lu ms (%u Samples)\n",
                    (unsigned long)dt, (unsigned)_bufferCount);
    }

    Serial.printf("[Logger] %u Samples auf SD geschrieben\n",
                  (unsigned)_bufferCount);
    _bufferCount = 0;
    return true;
  }

  // -- Reset log file (POST /reset handler) ----------------------------------
  bool resetSDFile() {
    if (!_sdOk) return false;
    _bufferCount = 0;
    if (_logFile) _logFile.close();
    if (SD.exists(LOG_FILE_PATH)) {
      if (!SD.remove(LOG_FILE_PATH)) {
        Serial.println("[Logger] SD.remove fehlgeschlagen");
        _openLogFile();
        return false;
      }
    }
    File f = SD.open(LOG_FILE_PATH, FILE_WRITE);
    if (!f) { _openLogFile(); return false; }
    f.println(LOG_FILE_HEADER);
    f.close();
    Serial.println("[Logger] Log-Datei zurueckgesetzt");
    _openLogFile();
    return true;
  }

  // -- Status accessors -----------------------------------------------------
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
    if (_logFile) { _logFile.flush(); _logFile.close(); }
    File f = SD.open(LOG_FILE_PATH, FILE_READ);
    return f;
    // Note: _openLogFile() NOT called here -- WebPortal calls it via
    // reopenAfterRead() once the stream is closed.
  }

  void reopenAfterRead() { _openLogFile(); }

private:
  ShellyClient& _shelly;        // injected reference -- not owned
  RTC_DS3231*   _rtc;           // optional injected pointer -- not owned (nullptr = no RTC)
  bool          _powerLostFlag; // set via setPowerLost() from loop(); no PowerMonitor.h dep
  SPIClass      _sdSPI;
  File          _logFile;     // persistent write handle -- opened once, never closed between flushes
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
  bool          _otaInProgress;   // true while WebPortal is writing OTA flash
  uint32_t      _pollIntervalMs;
  float         _powerThresholdW;

  // -- Ring-buffer push with FIFO-drop on overflow (unchanged from v4) -------
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

  void _openLogFile() {
    if (_logFile) _logFile.close();
    _logFile = SD.open(LOG_FILE_PATH, FILE_APPEND);
    if (_logFile) {
      Serial.println("[Logger] Log-Datei geoeffnet");
    } else {
      Serial.println("[Logger] Log-Datei open() fehlgeschlagen");
      _sdOk = false;
    }
  }

  bool initSDCard() {
    return SD.begin(PIN_SD_CS, _sdSPI, 4000000);
  }

  // -- SD auto-recovery every 30 s ------------------------------------------
  void tryRecoverSD() {
    uint32_t now = millis();
    if (now - _lastSdRetryMs < 30000) return;
    _lastSdRetryMs = now;
    Serial.println("[Logger] Versuche SD-Karte neu zu initialisieren...");
    if (_logFile) _logFile.close();
    SD.end();
    delay(50);
    _sdOk = initSDCard();
    if (_sdOk) {
      if (!SD.exists(LOG_FILE_PATH)) {
        File f = SD.open(LOG_FILE_PATH, FILE_WRITE);
        if (f) { f.println(LOG_FILE_HEADER); f.close(); }
      }
      _openLogFile();
      Serial.println("[Logger] SD wieder verfuegbar");
    }
  }
};

// Called once from setup() to ensure the log file has a header row.
// COMPATIBILITY WARNING: If a pre-existing log.csv is found (from an older
// firmware version with fewer columns), this function does NOT rewrite the
// header. New 7-column rows will be appended after old 5-column rows, making
// the file inconsistent. Solution: call POST /reset via the WebPortal once
// after flashing this firmware version.
inline void ensureLogHeader() {
  if (!SD.exists(LOG_FILE_PATH)) {
    File f = SD.open(LOG_FILE_PATH, FILE_WRITE);
    if (f) {
      f.println(LOG_FILE_HEADER);
      f.close();
    }
  } else {
    // File already exists -- warn that it may have been written by an older
    // firmware version with a different column layout.
    Serial.println("[Logger] HINWEIS: log.csv existiert bereits. Falls von einer");
    Serial.println("[Logger]          aelteren Firmware-Version: bitte /reset ausfuehren,");
    Serial.println("[Logger]          da neue Spalten (supply_V, power_down) hinzukamen.");
  }
}

#endif  // LOGGER_H
