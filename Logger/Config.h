/*
 * Config.h - Zentrale Konfiguration v6  (Shelly + ESP32 + OTA)
 * =============================================================
 *
 * Changes vs. v5 (Shelly + ESP32):
 *   ADDED    OTA_ENDPOINT  -- route WebPortal listens on for firmware upload
 *   CHANGED  API_BUFFER_SIZE comment  (ota_active field added to /api/live)
 *   ALL other constants unchanged
 */

#ifndef CONFIG_H
#define CONFIG_H

// ===== Pin-Belegung ESP32-WROOM =====
// GPIO 16/17 (PZEM UART) are now free -- do not assign here to avoid confusion.
#define PIN_SD_CS       25
#define PIN_SD_MOSI     14
#define PIN_SD_CLK      27
#define PIN_SD_MISO     26
#define PIN_LED         32
#define PIN_VSUPPLY     35   // ADC -- supply monitoring (future, Req 13)
// GPIO 33 is reserved for a future manual-reset button (Req 25/26).
// It is intentionally NOT defined here: defining a pin that no code reads
// is dead code. Add the #define together with the handler that uses it.

// ===== Shelly Plug S MTR Gen3 =====
// The Shelly joins the ESP32 softAP as a STA client.
// 192.168.4.1 is the fixed ESP32 AP gateway IP -- the Shelly always uses this
// as its push target.  The ESP32 finds the Shelly at the IP assigned by its
// own DHCP server.  Using mDNS ("shellyplugsg3-XXXXXX.local") is an
// alternative if your router assigns stable leases; IP is more portable.
//
// If you change the AP SSID/password below you must also re-provision the
// Shelly's STA Wi-Fi settings to match.
#define SHELLY_HOST             "192.168.4.2"    // First DHCP lease from ESP32 AP
                                                  // Change to mDNS name if preferred:
                                                  // "shellyplugsg3-XXXXXX.local"
#define SHELLY_PUSH_ENDPOINT    "/api/shelly_push"  // Must match shelly_push.js PUSH_URL
#define OTA_ENDPOINT            "/update"            // Must match WebPortal route registration

// Shelly push watchdog: if no push is received for this many consecutive
// expected intervals, the Shelly is declared unreachable (-> LED_ERROR).
// 3 x 1000 ms = 3 s of silence before error is flagged.
#define SHELLY_ERROR_THRESHOLD  3

// ===== Zeitintervalle (ms) =====
// INTERVAL_SHELLY_POLL_MS is the EXPECTED push cadence from shelly_push.js.
// The ESP32 uses this value:
//   a) as the watchdog timeout multiplier (SHELLY_ERROR_THRESHOLD x this)
//   b) as the default value returned by /api/settings  (Req 21 rate selector)
//   c) as the minimum allowed rate on the Settings page
//
// Shelly's internal energy meter updates at ~1 Hz.  Pushing faster returns
// stale values.  Minimum meaningful value: 1000 ms.
#define INTERVAL_SHELLY_POLL_MS   1000   // default 1 s; adjustable via /api/settings
#define INTERVAL_SD_FLUSH_MS     10000   // flush RAM buffer to SD every 10 s (unchanged)
#define INTERVAL_LED_OK_MS         500   // 1 Hz blink when system OK  (unchanged)
#define INTERVAL_LED_ERR_MS        100   // 5 Hz blink on error        (unchanged)

// ===== Log-Bedingungen =====
#define DEFAULT_POWER_THRESHOLD_W    0.0f   // 0 = log everything (no lower limit)

// ===== WLAN =====
#define WIFI_AP_SSID        "PZEM_Logger"    // Shelly must be pre-paired to this SSID
#define WIFI_AP_PASSWORD    "logger1234"     // >= 8 chars; change for your deployment
#define WIFI_AP_HOSTNAME    "braun_PZEM"     // -> http://braun_PZEM.local  (mDNS)
#define DNS_PORT            53

// ===== SD-Karte =====
#define LOG_FILE_PATH    "/log.csv"
// Column "pf" renamed to "pf_apparent" to reflect that it is a derived value
// (apower / (V x I)), not a direct hardware measurement.  See Req 5 / Req 7c.
#define LOG_FILE_HEADER  "time_ms,voltage_V,power_W,pf_apparent"

// ===== RAM-Puffer =====
// 64 x 16 Byte = 1024 Byte.
// At 1 s cadence: 64 samples = 64 s of reserve before SD must flush.
// INTERVAL_SD_FLUSH_MS = 10 s -> buffer only ever holds <= 10 samples normally.
// 64 entries gives substantial margin if SD is temporarily unavailable.
#define RAM_BUFFER_SIZE  64

// ===== Webserver =====
#define HTTP_PORT        80
#define API_BUFFER_SIZE  320   // sufficient for /api/live incl. shelly_ok + ota_active (~146 chars worst-case)

#endif
