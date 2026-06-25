/*
 * Shelly_ESP32_Logger.ino  v3
 * ============================
 *
 * Main sketch for the Shelly Plug S MTR Gen3 + ESP32 + SD power logger.
 *
 * Changes vs. v1
 * --------------
 *   ADDED    #include "PowerMonitor.h"
 *   ADDED    PowerMonitor powerMonitor   (no dependencies)
 *   ADDED    powerMonitor.begin() in setup() (replaces the bare
 *            pinMode(PIN_VSUPPLY, INPUT) placeholder -- Req 13 now implemented)
 *   ADDED    handlePowerLoss()  -- terminal graceful-shutdown sequence
 *   CHANGED  loop() -- power-loss guard runs first, every iteration
 *   ADDED    #include <RTClib.h> + #include <Wire.h>
 *   ADDED    RTC_DS3231 rtc  (no dependencies)
 *   CHANGED  Logger logger   (now takes &rtc as second argument)
 *   ADDED    Wire.begin() + rtc.begin() + lostPower check in setup()
 *   ADDED    shelly.beginStartupGrace() after webPortal.begin()
 *   UNCHANGED: everything else (Shelly push path, SD logic, OTA, web UI)
 *
 * Changes vs. PZEM_Logger.ino  v4 (kept for history)
 * --------------------------------------------------
 *   REMOVED  #include (PZEM library)
 *   ADDED    ShellyClient shelly  (declared before logger)
 *   CHANGED  Logger logger        (now takes ShellyClient& in constructor)
 *   CHANGED  WebPortal webPortal  (now takes ShellyClient& as second arg)
 *
 * Object ownership model
 * ----------------------
 *   shelly       — owned here; receives data from WebPortal::handleShellyPush()
 *   logger       — owned here; reads cached data from shelly via pollIfDue()
 *   webPortal    — owned here; routes POST /api/shelly_push → shelly.ingest()
 *                              routes GET  /api/live         → logger getters
 *   powerMonitor — owned here; reads the 9V rail on GPIO 35, flags power loss
 *
 * Power-loss handling (Req 13)
 * ----------------------------
 *   powerMonitor.update() samples the 9V rail every 200 ms.  On a sustained
 *   drop below 7.35 V it latches isPowerLost().  loop() then calls
 *   handlePowerLoss(), which flushes the SD card, sheds load (Wi-Fi/LED off)
 *   to stretch the supercap hold-up, and idles until either mains returns
 *   (clean reboot) or the supercaps deplete (hardware brownout reset).
 *   See PowerMonitor.h for the full threshold/timing rationale.
 *
 *   NOTE: monitoring is naturally suspended during an OTA flash, because the
 *   multipart upload blocks inside webPortal.update().  That is safe: the OTA
 *   chunk handler already flushes the SD buffer before Update.begin(), so no
 *   samples are at risk, and a power loss mid-flash simply leaves the old
 *   firmware in place.
 *
 * Loop timing (approximate, single-core)
 * ---------------------------------------
 *   powerMonitor.update() — checks every ~0–5 ms; samples every 200 ms (~3 ms)
 *   webPortal.update()    — handles one HTTP request per call (~1–50 ms); runs FIRST
 *   pollIfDue()           — checks every ~0–5 ms; fires every 1000 ms (default)
 *   flushIfDue()          — checks every ~0–5 ms; fires every 10 000 ms
 *   statusLed.update()    — non-blocking toggle check (~0 ms)
 *
 * Required Arduino libraries (Library Manager)
 * ---------------------------------------------
 *   ArduinoJson  by Benoit Blanchon  (≥ v6.x)
 *   SD           (built-in)
 *   WiFi         (built-in ESP32)
 *   WebServer    (built-in ESP32)
 *   ESPmDNS      (built-in ESP32)
 *   DNSServer    (built-in ESP32)
 */

#include <WiFi.h>            // WiFi.mode(WIFI_OFF) in handlePowerLoss()
#include <Wire.h>            // I2C bus for DS3231 RTC
#include <RTClib.h>          // Adafruit RTClib -- DS3231 driver
#include "Config.h"
#include "StatusLed.h"
#include "ShellyClient.h"
#include "Logger.h"
#include "WebPortal.h"
#include "PowerMonitor.h"

// ── Object instantiation order matters:
//    ShellyClient has no dependencies.
//    Logger depends on ShellyClient.
//    WebPortal depends on both Logger and ShellyClient.
//    PowerMonitor has no dependencies.
ShellyClient shelly;
RTC_DS3231   rtc;
Logger       logger(shelly, &rtc);   // &rtc: optional pointer; nullptr = RTC absent
WebPortal    webPortal(logger, shelly);
StatusLed    statusLed;
PowerMonitor powerMonitor;

// Forward declaration (Arduino auto-prototypes, but explicit is clearer).
void handlePowerLoss();

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Shelly ESP32 Logger v2 Start ===");

  // 9V-rail monitor on GPIO 35 (Req 13). begin() configures the ADC and
  // starts the startup grace window, so it must run early.
  powerMonitor.begin();

  // DS3231 RTC on GPIO 21 (SDA) / GPIO 22 (SCL) -- hardware I2C bus 0.
  Wire.begin(PIN_RTC_SDA, PIN_RTC_SCL);
  if (!rtc.begin(&Wire)) {
    Serial.println("[Setup] WARNUNG: DS3231 nicht gefunden (I2C-Fehler)!");
    Serial.println("[Setup]          Logging laeuft weiter, CSV zeigt 'RTC_NOT_SET'.");
  } else if (rtc.lostPower()) {
    Serial.println("[Setup] WARNUNG: DS3231 Batterie leer / Uhrzeit nicht gesetzt!");
    Serial.println("[Setup]          Bitte RTC kalibrieren. CSV zeigt 'RTC_NOT_SET'.");
  } else {
    DateTime now = rtc.now();
    Serial.printf("[Setup] RTC OK -- %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
  }

  // GPIO 33 (manual-reset button, Req 25/26) is not configured here:
  // there is no code that reads it yet. Configure it when the handler exists.

  statusLed.begin();
  statusLed.setOk(false);   // error blink until init completes

  logger.begin();
  if (logger.sdOk()) {
    ensureLogHeader();
  }

  if (!webPortal.begin()) {   // starts AP+STA WiFi and HTTP server
    // softAP() failed — without the web server the device cannot receive
    // Shelly pushes or serve the UI. Make the failure visible instead of
    // continuing silently with a dead server.
    Serial.println("[Setup] FEHLER: WebPortal konnte nicht gestartet werden!");
    statusLed.setOk(false);
  }

  // Start the boot-order grace window: if the Shelly is already running
  // when the ESP32 boots, its WiFi client needs up to ~10 s to re-associate
  // with the freshly-started AP. beginStartupGrace() suppresses the
  // 3 s watchdog for SHELLY_STARTUP_GRACE_MS so the UI doesn't lock on '--'.
  shelly.beginStartupGrace();

  Serial.println("[Setup] fertig — warte auf ersten Shelly-Push...");
  statusLed.setOk(false);   // stays red until first push arrives
}

void loop() {
  // 0. Power-loss guard FIRST. If the 9V rail has collapsed, persist data
  //    and shut down gracefully before anything else can touch the SD card.
  //    handlePowerLoss() never returns.
  powerMonitor.update();
  if (powerMonitor.isPowerLost()) {
    handlePowerLoss();
  }

  // 1. Service HTTP requests FIRST so that any arriving Shelly push is
  //    ingested into the ShellyClient cache before pollIfDue() reads it.
  //    Previous order (poll → flush → update) meant every sample was read
  //    from the *previous* tick's cached value — a structural one-loop lag.
  //    New order: update (ingest) → poll (read fresh cache) → flush.
  //    No other dependency is affected: flushIfDue() does not care about
  //    network state, and the LED update always reads the post-poll state.
  webPortal.update();

  // 2. Sample the freshly-ingested Shelly data into the ring buffer.
  //    pollIfDue() now always sees the measurement that arrived this loop
  //    iteration rather than the one from the previous iteration.
  logger.pollIfDue();

  // 3. Flush ring buffer to SD on schedule.
  logger.flushIfDue();

  // 4. Update LED — reflects logger.ok() = shellyOk() && sdOk()
  //    idempotent: no flicker if state hasn't changed
  statusLed.setOk(logger.ok());
  statusLed.update();
}

// ── Terminal graceful shutdown on mains loss (Req 13) ───────────────────────
//
// Called once, from loop(), when PowerMonitor latches a sustained
// under-voltage. Does NOT return under normal circumstances:
//   • genuine power loss  -> idle until the supercaps deplete and the ESP32
//                            brownout detector resets the chip (data already
//                            on the SD card);
//   • mains comes back    -> reboot cleanly via ESP.restart() to resume
//                            logging (the startup grace window re-arms).
void handlePowerLoss() {
  Serial.println();
  Serial.println("[Power] *** SPANNUNGSABFALL erkannt (<7.35V) -- Notabschaltung ***");

  // 1. Persist everything still in RAM. This is the whole point of Req 13.
  //    flushToSD() is best-effort: if the SD has already failed there is
  //    nothing more we can do, but normally the buffer holds <= 10 samples
  //    and writes in well under 500 ms.
  uint32_t t0 = millis();
  logger.flushToSD();
  Serial.printf("[Power] SD-Flush abgeschlossen in %lu ms\n",
                (unsigned long)(millis() - t0));

  // 2. Shed load to stretch the supercap hold-up window. Killing the Wi-Fi
  //    radio saves ~80-120 mA, which buys several extra seconds before
  //    brownout — far more than the flush actually needed.
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  digitalWrite(PIN_LED, LOW);

  Serial.println("[Power] Logging gestoppt. Warte auf Brownout oder Netz-Rueckkehr...");

  // 3. Terminal idle with mains-recovery escape hatch.
  //    With Wi-Fi off the ADC1 noise floor is much lower, so the rail reading
  //    here is cleaner than during normal operation.
  uint8_t recoverCount = 0;
  for (;;) {
    delay(100);
    uint32_t railMv = powerMonitor.readRailMilliVolts();

    if (railMv > POWER_THRESHOLD_HIGH_MV) {
      if (++recoverCount >= POWER_RECOVER_COUNT) {   // ~1 s sustained above clear
        Serial.println("[Power] Netz wieder stabil -- Neustart zur Wiederaufnahme.");
        delay(50);
        ESP.restart();
      }
    } else {
      recoverCount = 0;   // not yet stable; keep waiting (brownout will reset)
    }
  }
}
