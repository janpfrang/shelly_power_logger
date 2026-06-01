/*
 * WebPortal.h v7  (Shelly + ESP32 + OTA)
 * ========================================
 *
 * Changes vs. v6 (Shelly + ESP32):
 *   ADDED    #include <Update.h>
 *   ADDED    handleOtaForm()    — GET  /update  (upload form page)
 *   ADDED    handleOtaUpload()  — POST /update  (binary stream handler)
 *   ADDED    route registrations for GET+POST /update in begin()
 *   CHANGED  handleApiLive()    — adds "ota_active" boolean field
 *   NOTE     API_BUFFER_SIZE 320 in Config.h is still sufficient
 *            (worst-case /api/live JSON is ~146 chars)
 *   UNCHANGED: all other routes, pages, members, and handlers
 *
 * OTA UPLOAD FLOW
 * ---------------
 *  1. User opens  GET /update  → served PAGE_OTA (upload form)
 *  2. User selects .bin, clicks Upload
 *  3. Browser POSTs multipart/form-data to POST /update
 *  4. WebServer calls the upload callback for each chunk
 *     (registered via _server.onFileUpload — see begin())
 *  5. On first chunk: _logger.setOtaInProgress(true) → flushes SD,
 *     pauses polling; Update.begin(UPDATE_SIZE_UNKNOWN) starts flash writer
 *  6. Per chunk: Update.write(data, len)
 *  7. On last chunk: Update.end(true) — finalises flash; ESP32 reboots
 *  8. On any error: Update.abort(), _logger.setOtaInProgress(false)
 *     → logging resumes (no reboot on error)
 *
 * SECURITY NOTE
 * -------------
 * This OTA endpoint has NO authentication.  It is only reachable from
 * devices connected to the PZEM_Logger Wi-Fi access point.  Physical
 * proximity to the device is required — acceptable for a lab instrument.
 * If you deploy this in a shared-network context, add HTTP Basic Auth
 * or move the endpoint behind a separate port.
 */

#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>          // ESP32 OTA flash writer (bundled with arduino-esp32)
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
  void handleHistogram();
  void handleLivePlot();
  void handleCaptivePortal();
  void handleNotFound();

  // ── New route ─────────────────────────────────────────────────────────────
  // POST /api/shelly_push  — called by shelly_push.js on the Shelly device
  void handleShellyPush();

  // ── OTA routes ────────────────────────────────────────────────────────────
  // GET  /update  — serves the firmware upload form (PAGE_OTA)
  void handleOtaForm();
  // POST /update  — body handler (called once, sends final HTTP response)
  void handleOtaUpload();
  // Upload callback — called per-chunk by WebServer during multipart upload
  void handleOtaChunk();
};

#endif  // WEB_PORTAL_H
