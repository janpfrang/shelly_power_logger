/*
 * Shelly_ESP32_Logger.ino  v1
 * ============================
 *
 * Main sketch for the Shelly Plug S MTR Gen3 + ESP32 + SD power logger.
 *
 * Changes vs. PZEM_Logger.ino v4
 * --------------------------------
 *   REMOVED  #include (PZEM library)
 *   ADDED    ShellyClient shelly  (declared before logger)
 *   CHANGED  Logger logger        (now takes ShellyClient& in constructor)
 *   CHANGED  WebPortal webPortal  (now takes ShellyClient& as second arg)
 *   REMOVED  analogReadResolution(12) — PIN_VSUPPLY still configured but
 *            ADC read deferred to future Req 13 implementation
 *   UNCHANGED: setup()/loop() structure, ensureLogHeader(), all object calls
 *
 * Object ownership model
 * ----------------------
 *   shelly    — owned here; receives data from WebPortal::handleShellyPush()
 *   logger    — owned here; reads cached data from shelly via pollIfDue()
 *   webPortal — owned here; routes POST /api/shelly_push → shelly.ingest()
 *                           routes GET  /api/live         → logger getters
 *
 * Loop timing (approximate, single-core)
 * ---------------------------------------
 *   pollIfDue()     — checks every ~0–5 ms; fires every 1000 ms (default)
 *   flushIfDue()    — checks every ~0–5 ms; fires every 10 000 ms
 *   webPortal.update() — handles one HTTP request per call (~1–50 ms)
 *   statusLed.update() — non-blocking toggle check (~0 ms)
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

#include "Config.h"
#include "StatusLed.h"
#include "ShellyClient.h"
#include "Logger.h"
#include "WebPortal.h"

// ── Object instantiation order matters:
//    ShellyClient has no dependencies.
//    Logger depends on ShellyClient.
//    WebPortal depends on both Logger and ShellyClient.
ShellyClient shelly;
Logger       logger(shelly);
WebPortal    webPortal(logger, shelly);
StatusLed    statusLed;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Shelly ESP32 Logger v1 Start ===");

  // Pin modes
  pinMode(PIN_BUTTON,  INPUT_PULLUP);   // future: manual reset (Req 25/26)
  pinMode(PIN_VSUPPLY, INPUT);          // future: supply monitor (Req 13)

  statusLed.begin();
  statusLed.setOk(false);   // error blink until init completes

  logger.begin();
  if (logger.sdOk()) {
    ensureLogHeader();
  }

  webPortal.begin();   // starts AP+STA WiFi and HTTP server

  Serial.println("[Setup] fertig — warte auf ersten Shelly-Push...");
  statusLed.setOk(false);   // stays red until first push arrives
}

void loop() {
  // 1. Check if new Shelly data is due to be sampled into the ring buffer
  logger.pollIfDue();

  // 2. Flush ring buffer to SD on schedule
  logger.flushIfDue();

  // 3. Service HTTP requests (this is where handleShellyPush() fires,
  //    updating shelly's cache, which pollIfDue() will read next tick)
  webPortal.update();

  // 4. Update LED — reflects logger.ok() = shellyOk() && sdOk()
  //    idempotent: no flicker if state hasn't changed
  statusLed.setOk(logger.ok());
  statusLed.update();
}
