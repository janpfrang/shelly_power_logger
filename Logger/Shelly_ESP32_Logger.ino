/*
 * Shelly_ESP32_Logger.ino  v3
 * ============================
 *
 * Changes vs. v2
 * --------------
 *   CHANGED  WebPortal webPortal(...) -- &rtc passed as third argument so
 *            WebPortal can serve POST /api/set_rtc and display RTC time
 *            on the home page and settings page.
 *   UNCHANGED: everything else
 *
 * Changes vs. v1
 * --------------
 *   ADDED    #include "PowerMonitor.h"
 *   ADDED    PowerMonitor powerMonitor   (no dependencies)
 *   ADDED    powerMonitor.begin() in setup()
 *   ADDED    handlePowerLoss()
 *   CHANGED  loop() -- power-loss guard runs first, every iteration
 *   ADDED    #include <RTClib.h> + #include <Wire.h>
 *   ADDED    RTC_DS3231 rtc  (no dependencies)
 *   CHANGED  Logger logger   (now takes &rtc as second argument)
 *   ADDED    Wire.begin() + rtc.begin() + lostPower check in setup()
 *   ADDED    shelly.beginStartupGrace() after webPortal.begin()
 *
 * Object ownership model
 * ----------------------
 *   shelly       — owned here; receives data from WebPortal::handleShellyPush()
 *   rtc          — owned here; passed by pointer to Logger and WebPortal
 *   logger       — owned here; reads cached data from shelly via pollIfDue()
 *   webPortal    — owned here; routes POST /api/shelly_push → shelly.ingest()
 *                              routes GET  /api/live         → logger getters
 *                              routes POST /api/set_rtc      → rtc.adjust()  (v8)
 *   powerMonitor — owned here; reads the 9V rail on GPIO 35, flags power loss
 *
 * Power-loss handling (Req 13)
 * ----------------------------
 *   powerMonitor.update() samples the 9V rail every 200 ms.  On a sustained
 *   drop below 7.35 V it latches isPowerLost().  loop() then calls
 *   handlePowerLoss(), which flushes the SD card, sheds load (Wi-Fi/LED off)
 *   to stretch the supercap hold-up, and idles until either mains returns
 *   (clean reboot) or the supercaps deplete (hardware brownout reset).
 *
 * Loop timing (approximate, single-core)
 * ---------------------------------------
 *   powerMonitor.update() — checks every ~0–5 ms; samples every 200 ms (~3 ms)
 *   pollIfDue()           — checks every ~0–5 ms; fires every 1000 ms (default)
 *   flushIfDue()          — checks every ~0–5 ms; fires every 10 000 ms
 *   webPortal.update()    — handles one HTTP request per call (~1–50 ms)
 *   statusLed.update()    — non-blocking toggle check (~0 ms)
 *
 * Required Arduino libraries (Library Manager)
 * ---------------------------------------------
 *   ArduinoJson  by Benoit Blanchon  (>= v6.x)
 *   RTClib       by Adafruit
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
//    RTC_DS3231 has no dependencies.
//    Logger depends on ShellyClient and optionally on RTC_DS3231.
//    WebPortal depends on Logger, ShellyClient, and optionally on RTC_DS3231.
//    PowerMonitor has no dependencies.
ShellyClient shelly;
RTC_DS3231   rtc;
Logger       logger(shelly, &rtc);      // &rtc: optional pointer; nullptr = RTC absent
WebPortal    webPortal(logger, shelly, &rtc);  // v8: rtc passed so /api/set_rtc works
StatusLed    statusLed;
PowerMonitor powerMonitor;

// Forward declaration (Arduino auto-prototypes, but explicit is clearer).
void handlePowerLoss();

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Shelly ESP32 Logger v3 Start ===");

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
    Serial.println("[Setup]          Bitte RTC kalibrieren via Settings > Set RTC Time.");
  } else {
    DateTime now = rtc.now();
    Serial.printf("[Setup] RTC OK -- %04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
  }

  statusLed.begin();
  statusLed.setOk(false);   // error blink until init completes

  logger.begin();
  if (logger.sdOk()) {
    ensureLogHeader();
  }

  if (!webPortal.begin()) {
    Serial.println("[Setup] FEHLER: WebPortal konnte nicht gestartet werden!");
    statusLed.setOk(false);
  }

  // Start the boot-order grace window for the Shelly watchdog.
  shelly.beginStartupGrace();

  Serial.println("[Setup] fertig -- warte auf ersten Shelly-Push...");
  statusLed.setOk(false);   // stays red until first push arrives
}

void loop() {
  // 0. Power-loss guard FIRST.
  powerMonitor.update();
  if (powerMonitor.isPowerLost()) {
    handlePowerLoss();
  }

  // 1. Check if new Shelly data is due to be sampled into the ring buffer
  logger.pollIfDue();

  // 2. Flush ring buffer to SD on schedule
  logger.flushIfDue();

  // 3. Service HTTP requests
  webPortal.update();

  // 4. Update LED -- reflects logger.ok() = shellyOk() && sdOk()
  statusLed.setOk(logger.ok());
  statusLed.update();
}

// ── Terminal graceful shutdown on mains loss (Req 13) ───────────────────────
void handlePowerLoss() {
  Serial.println();
  Serial.println("[Power] *** SPANNUNGSABFALL erkannt (<7.35V) -- Notabschaltung ***");

  uint32_t t0 = millis();
  logger.flushToSD();
  Serial.printf("[Power] SD-Flush abgeschlossen in %lu ms\n",
                (unsigned long)(millis() - t0));

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  digitalWrite(PIN_LED, LOW);

  Serial.println("[Power] Logging gestoppt. Warte auf Brownout oder Netz-Rueckkehr...");

  uint8_t recoverCount = 0;
  for (;;) {
    delay(100);
    uint32_t railMv = powerMonitor.readRailMilliVolts();

    if (railMv > POWER_THRESHOLD_HIGH_MV) {
      if (++recoverCount >= POWER_RECOVER_COUNT) {
        Serial.println("[Power] Netz wieder stabil -- Neustart zur Wiederaufnahme.");
        delay(50);
        ESP.restart();
      }
    } else {
      recoverCount = 0;
    }
  }
}
