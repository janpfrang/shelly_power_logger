/*
 * WebPortal.h v8  (Shelly + ESP32 + OTA + RTC set-time)
 * =======================================================
 *
 * Changes vs. v7 (Shelly + ESP32 + OTA):
 *   ADDED    #include <RTClib.h>
 *   CHANGED  Constructor: third parameter RTC_DS3231* rtc = nullptr
 *   ADDED    _rtc member (RTC_DS3231*)
 *   ADDED    handleApiSetRtc()   -- POST /api/set_rtc
 *   ADDED    route registration  POST /api/set_rtc  in begin()
 *   CHANGED  PAGE_SETTINGS       -- "Set RTC Time" card added
 *   CHANGED  PAGE_INDEX          -- RTC time display added to status card
 *   UNCHANGED: all other routes, pages, handlers
 *
 * SET-RTC FLOW
 * ------------
 *  1. User opens /settings
 *  2. Browser pre-fills <input type="datetime-local"> with local time
 *  3. User clicks "Set RTC Time"
 *  4. JS reads the picker value, POSTs JSON to POST /api/set_rtc:
 *       { "year":2026, "month":6, "day":18,
 *         "hour":14,   "minute":33, "second":0 }
 *  5. handleApiSetRtc() calls _rtc->adjust(DateTime(...))
 *  6. ESP responds { "ok":true, "time":"2026-06-18T14:33:00" }
 *  7. JS shows a confirmation message with the confirmed time
 *
 * If _rtc == nullptr (RTC not wired or begin() failed) the handler
 * responds 503 {"error":"RTC not available"} -- graceful degradation.
 */

#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>          // ESP32 OTA flash writer (bundled with arduino-esp32)
#include <RTClib.h>          // Adafruit RTClib -- DS3231 driver
#include "Config.h"
#include "Logger.h"
#include "ShellyClient.h"

class WebPortal {
public:
  // Logger and ShellyClient are injected (required).
  // RTC_DS3231 is optional: pass nullptr when the RTC is absent.
  // Keeping nullptr as default preserves backward-compatibility if any
  // test harness constructs WebPortal without an RTC.
  WebPortal(Logger& logger, ShellyClient& shelly, RTC_DS3231* rtc = nullptr);

  bool begin();
  void update();

private:
  Logger&       _logger;
  ShellyClient& _shelly;
  RTC_DS3231*   _rtc;        // optional -- nullptr means no RTC available
  WebServer     _server;
  DNSServer     _dns;

  // -- Page handlers (unchanged from v7) ------------------------------------
  void handleRoot();
  void handleApiLive();
  void handleApiSettings();
  void handleApiSettingsSave();
  void handleDownload();
  void handleReset();
  void handleSettings();
  void handleReadme();
  void handleHistogram();
  void handleLivePlot();
  void handleCaptivePortal();
  void handleNotFound();

  // POST /api/shelly_push  -- called by shelly_push.js on the Shelly device
  void handleShellyPush();

  // -- OTA routes (unchanged from v7) ---------------------------------------
  void handleOtaForm();
  void handleOtaUpload();
  void handleOtaChunk();

  // -- RTC set-time route (NEW v8) ------------------------------------------
  // POST /api/set_rtc
  // Body (application/json):
  //   { "year":YYYY, "month":M, "day":D,
  //     "hour":H, "minute":M, "second":S }
  // Response 200: { "ok":true, "time":"YYYY-MM-DDTHH:MM:SS" }
  // Response 400: { "error":"..." }  -- missing / out-of-range fields
  // Response 503: { "error":"RTC not available" }  -- _rtc == nullptr
  void handleApiSetRtc();
};

#endif  // WEB_PORTAL_H
