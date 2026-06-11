/*
 * Config.h - Zentrale Konfiguration v8  (Shelly + ESP32 + OTA + Power-loss fix)
 * ==============================================================================
 *
 * Changes vs. v7 (Shelly + ESP32 + OTA + Power-loss):
 *   ADDED    POWER_MONITOR_ENABLED  -- set to 0 to disable the power-loss
 *              watchdog entirely when the 9V/supercap circuit is not yet
 *              populated.  This was the root cause of the WiFi-not-visible bug:
 *              GPIO 35 floats to 0 V when the divider resistors are absent,
 *              which reads as railMv = 0 -- below the 7350 mV threshold --
 *              triggering handlePowerLoss() within ~600 ms of boot and
 *              calling WiFi.mode(WIFI_OFF) before the AP was ever visible.
 *   CHANGED  POWER_STARTUP_GRACE_MS  3000 -> 10000 ms
 *              Even with the circuit populated the old 3 s grace was too short:
 *              webPortal.begin() (WiFi.softAP) can take 200-500 ms, and the
 *              first loop() iterations consumed the remaining grace window
 *              before the AP was fully established.  10 s is safe for all
 *              hardware states.
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
#define PIN_VSUPPLY     35   // ADC1_CH7 -- 9V-rail monitor for power-loss flush (Req 13)
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
// 10 x 1000 ms = 10 s of silence before error is flagged.
//
// Why 10 and not 3:
//   When the ESP32 boots after the Shelly, the softAP takes ~1-2 s to start.
//   The Shelly then needs 3-10 s to re-associate its WiFi to the newly-appeared
//   AP. With a 3 s threshold the watchdog tripped during that reconnection window
//   and the UI stayed at '--' permanently. 10 s covers the worst-case
//   re-association delay with margin, while still flagging a dead Shelly quickly.
#define SHELLY_ERROR_THRESHOLD  10

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

// ===== Versorgungs-Ueberwachung / Power-loss detection (Req 13) =====
// GPIO 35 (PIN_VSUPPLY) reads the 9 V rail through a resistor divider:
//
//     9V rail --[ R_TOP 180k ]--+--[ R_BOTTOM 47k ]-- GND
//                               |
//                            GPIO 35 (ADC1_CH7)
//
//   V_gpio = V_rail * R_BOTTOM / (R_TOP + R_BOTTOM) = V_rail * 0.2070
//   PowerMonitor multiplies the measured pin voltage back up to recover the
//   rail voltage, so the thresholds below are expressed in real 9V-rail mV.
//
// Why 7.35 V and not lower:
//   The supply chain is 9V -> TSR-1-2450 (->5V) -> ESP32 LDO (->3.3V) -> ADC Vref.
//   The TSR-1-2450 loses regulation at ~6.5 V input; below that the 3.3 V rail
//   and the ADC reference sag, making readings untrustworthy.  Triggering at
//   7.35 V keeps the worst-case ADC error window (~+/-0.3 V) ABOVE the 6.5 V
//   regulator cliff, leaving a clean ~2.8 s window to flush the SD card.
//   (Supercaps 2x1F series = 0.5 F; ~4.5 s from mains loss to 7.35 V at ~150 mA.)
#define DIVIDER_R_TOP_OHM        180000UL  // R1: 9V rail -> GPIO35
#define DIVIDER_R_BOTTOM_OHM      47000UL  // R2: GPIO35 -> GND

// POWER_MONITOR_ENABLED
// ---------------------
// Set to 1 when the 9V rail + supercapacitor + resistor-divider circuit
// is fully populated on the PCB.
// Set to 0 (default) when the circuit is absent or not yet soldered:
//   GPIO 35 floats / reads 0 V -> railMv = 0 -> always below 7350 mV ->
//   handlePowerLoss() fires ~600 ms after boot -> WiFi.mode(WIFI_OFF) ->
//   the softAP disappears before anyone can connect.
// With this flag = 0, PowerMonitor::update() and isPowerLost() are
// no-ops; all other firmware is completely unaffected.
#define POWER_MONITOR_ENABLED      0       // <<<  set to 1 once HW circuit is built

#define POWER_THRESHOLD_LOW_MV     7350    // trigger shutdown below this (mV, 9V rail)
#define POWER_THRESHOLD_HIGH_MV    7750    // hysteresis: clear / recover above this (mV)
#define POWER_STARTUP_GRACE_MS    10000    // ignore readings while supercaps charge
                                           // (was 3000 -- too short; AP needs ~500 ms to
                                           //  start, leaving almost no margin before the
                                           //  first loop() checks began firing)
#define POWER_CHECK_INTERVAL_MS     200    // how often the rail is sampled
#define POWER_ADC_SAMPLES            16    // oversampling count (noise ~ 1/sqrt(N))
#define POWER_ADC_SAMPLE_GAP_US     200    // spacing between oversamples
#define POWER_MAJORITY_COUNT          3    // consecutive low reads required to trigger
#define POWER_RECOVER_COUNT          10    // consecutive OK reads during idle -> reboot

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
