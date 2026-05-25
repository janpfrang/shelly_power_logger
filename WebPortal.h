/*
 * WebPortal.h v6  (Shelly + ESP32)
 * ==================================
 *
 * Changes vs. v5 (PZEM/ESP32):
 *   ADDED    ShellyClient& parameter to constructor
 *   ADDED    handleShellyPush()  — POST /api/shelly_push receiver
 *   CHANGED  handleApiSettingsSave() whitelist: 200/500 ms removed (Req 21)
 *   CHANGED  WiFi.mode: WIFI_AP_STA  (Option B, Req 9 / Req 28b)
 *   UNCHANGED: all other routes, pages, and handlers
 */

#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "Config.h"
#include "Logger.h"
#include "ShellyClient.h"

class WebPortal {
public:
  // Both Logger and ShellyClient are injected.
  // ShellyClient::ingest() is called directly from handleShellyPush()
  // so the data path bypasses Logger entirely for the push endpoint.
  WebPortal(Logger& logger, ShellyClient& shelly);

  bool begin();
  void update();

private:
  Logger&       _logger;
  ShellyClient& _shelly;
  WebServer     _server;
  DNSServer     _dns;

  // ── Existing routes (unchanged from v5) ──────────────────────────────────
  void handleRoot();
  void handleApiLive();
  void handleApiSettings();
  void handleApiSettingsSave();
  void handleDownload();
  void handleReset();
  void handleSettings();
  void handleReadme();
  void handleLivePlot();
  void handleCaptivePortal();
  void handleNotFound();

  // ── New route ─────────────────────────────────────────────────────────────
  // POST /api/shelly_push  — called by shelly_push.js on the Shelly device
  void handleShellyPush();
};

#endif  // WEB_PORTAL_H
