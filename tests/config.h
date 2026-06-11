#pragma once
/*
 * tests/Config.h  —  stub constants for native unit tests
 * ========================================================
 * Shadows the real Logger/Config.h (which pulls in Arduino types and
 * hardware pin numbers that don't compile on the host).
 *
 * Define only the constants that ShellyClient.h and Logger.h actually
 * reference during compilation of the test binaries.
 *
 * Changes vs previous version:
 *   ADDED  SHELLY_STARTUP_GRACE_MS  -- required by ShellyClient v2.
 *          Value doesn't matter for tests (beginStartupGrace() is never
 *          called from test code), but the symbol must be defined.
 */
 
// ── ShellyClient constants ────────────────────────────────────────────────────
#define SHELLY_ERROR_THRESHOLD    3       // matches firmware: 3 × 1000 ms = 3 s watchdog
#define INTERVAL_SHELLY_POLL_MS   1000    // 1 s push cadence
#define SHELLY_STARTUP_GRACE_MS   15000   // 15 s boot-order grace (inactive in tests)
 
// ── Logger constants ──────────────────────────────────────────────────────────
// Guards needed: test_logger.cpp defines these itself before including Config.h
// (it sets #define CONFIG_H but our stub is included via Arduino.h path before that)
#ifndef LOG_FILE_PATH
#define LOG_FILE_PATH             "/log.csv"
#endif
#ifndef LOG_FILE_HEADER
#define LOG_FILE_HEADER           "datetime,time_ms,voltage_V,power_W,pf_apparent"
#endif
#ifndef RAM_BUFFER_SIZE
#define RAM_BUFFER_SIZE           64
#endif
#ifndef INTERVAL_SD_FLUSH_MS
#define INTERVAL_SD_FLUSH_MS      10000
#endif
#ifndef DEFAULT_POWER_THRESHOLD_W
#define DEFAULT_POWER_THRESHOLD_W 0.0f
#endif
 
// ── Pin stubs (referenced by Logger.h; not used at runtime in tests) ──────────
#define PIN_SD_CS    25
#define PIN_SD_MOSI  14
#define PIN_SD_CLK   27
#define PIN_SD_MISO  26
 
