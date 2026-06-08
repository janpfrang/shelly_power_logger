/*
 * ADC_Bringup.ino  –  9V Supply Voltage Monitor
 * ==============================================
 *
 * PURPOSE
 * -------
 * Minimal bring-up firmware to characterize the 9 V supply rail voltage
 * via GPIO 35 (ADC1_CH7). Designed to be flashed via OTA to replace the
 * main Shelly_ESP32_Logger.ino during commissioning/validation.
 *
 * FEATURES
 * --------
 *  • Continuous ADC sampling of PIN_VSUPPLY (GPIO 35)
 *  • Serial output: raw ADC count, calculated voltage, status
 *  • WiFi enabled (AP+STA mode) to simulate operational current draw
 *  • Simple web endpoint /adc to read current ADC value as JSON
 *  • Configurable voltage divider ratio for different hardware layouts
 *  • Threshold detection (future: triggers SD flush before brownout)
 *
 * CALIBRATION
 * -----------
 * The voltage divider constant (VDIV_RATIO) must be determined from the
 * actual hardware. Start with VDIV_RATIO = 1.0 and measure:
 *  1. The actual voltage on the 9 V rail using a multimeter
 *  2. The ADC reading printed to serial
 *  3. Calculate: VDIV_RATIO = (actual_voltage) / (measured_adc_voltage)
 *
 * For a typical 10 kΩ + 30 kΩ divider (3.3 V max → 9 V max):
 *  • V_rail = 9 V  →  V_adc ≈ 2.25 V  →  ADC ≈ 2790 counts (12-bit)
 *  • V_rail = 5 V  →  V_adc ≈ 1.25 V  →  ADC ≈ 1548 counts
 *
 * FLASHING
 * --------
 * 1. Connect to PZEM_Logger Wi-Fi (default AP)
 * 2. Navigate to http://braun_PZEM.local/update (or 192.168.4.1/update)
 * 3. Select ADC_Bringup.ino (after Arduino export)
 * 4. Upload via OTA
 * 5. Monitor serial output (115200 baud)
 *
 * SHUTDOWN LOGIC (FUTURE)
 * -----------------------
 * Once VDIV_RATIO is calibrated, this firmware can be extended to:
 *  • Set SHUTDOWN_THRESHOLD to the minimum acceptable voltage (e.g., 5.5 V)
 *  • Detect brownout and trigger Logger::flushToSD() before capacitor depletion
 *  • Coordinate with the main firmware via a shared setting
 *
 * REQUIREMENTS MET
 * ----------------
 * Req 13 (partial): ADC infrastructure for mains-loss detection
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// ===== PIN CONFIGURATION (from Config.h) =====
#define PIN_VSUPPLY       35    // ADC1_CH7 -- 9 V supply monitor input
#define WIFI_AP_SSID      "PZEM_Logger"
#define WIFI_AP_PASSWORD  "logger1234"
#define WIFI_AP_HOSTNAME  "braun_PZEM"
#define HTTP_PORT         80
#define DNS_PORT          53

// ===== ADC CALIBRATION =====
// Voltage divider ratio: V_rail = V_adc * VDIV_RATIO
// Start with 1.0 and adjust based on actual hardware measurements (see CALIBRATION above)
#define VDIV_RATIO        3.0f        // Placeholder: adjust to match your circuit

// ADC parameters
#define ADC_RESOLUTION    12           // 12-bit ADC: 0-4095
#define ADC_VREF          3.3f         // ESP32 ADC reference voltage
#define ADC_SAMPLES       16           // Average over N samples to reduce noise

// ===== SHUTDOWN LOGIC =====
// Mains loss detection threshold (9 V rail drops below this = brownout imminent)
#define SHUTDOWN_THRESHOLD_V  5.5f     // Brown-out threshold voltage
#define SHUTDOWN_HYSTERESIS   0.3f     // Hysteresis to prevent oscillation

// ===== TIMING =====
#define ADC_SAMPLE_INTERVAL_MS  1000   // Read ADC every 1 s
#define SERIAL_REPORT_INTERVAL_MS 1000 // Print to serial every 1 s

// ===== STATE =====
WebServer server(HTTP_PORT);
uint32_t lastAdcSampleMs = 0;
uint32_t lastSerialReportMs = 0;
float lastVoltage = -1.0f;
bool isInShutdown = false;

// ===== HELPERS =====

/**
 * Read ADC with oversampling to reduce noise.
 * Takes ADC_SAMPLES consecutive 12-bit readings and returns the average.
 */
uint16_t readAdcAveraged() {
  uint32_t sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += analogRead(PIN_VSUPPLY);
    delayMicroseconds(100);  // Small delay between reads
  }
  return (uint16_t)(sum / ADC_SAMPLES);
}

/**
 * Convert raw ADC count to voltage on the 9 V rail.
 * ADC range: 0–4095 (12-bit)  →  0–3.3 V at ADC input
 * Actual rail voltage depends on voltage divider ratio.
 */
float adcToVoltage(uint16_t adcCount) {
  float vAdc = (float)adcCount / (float)(1 << ADC_RESOLUTION) * ADC_VREF;
  return vAdc * VDIV_RATIO;
}

/**
 * Detect mains loss by checking if voltage drops below threshold with hysteresis.
 */
void updateShutdownState(float voltage) {
  if (!isInShutdown && voltage < SHUTDOWN_THRESHOLD_V) {
    isInShutdown = true;
    Serial.println("[ADC] ⚠️  SHUTDOWN THRESHOLD CROSSED — MAINS LOSS DETECTED!");
    Serial.printf("[ADC] V_rail = %.2f V (threshold = %.2f V)\n",
                  voltage, SHUTDOWN_THRESHOLD_V);
    // In the full firmware, this would trigger: logger.flushToSD()
  } else if (isInShutdown && voltage > (SHUTDOWN_THRESHOLD_V + SHUTDOWN_HYSTERESIS)) {
    isInShutdown = false;
    Serial.println("[ADC] ✓ Voltage recovered — shutdown state cleared");
  }
}

// ===== WEB HANDLERS =====

/**
 * Serve current ADC reading as JSON.
 * Allows remote monitoring (useful for phone-based commissioning).
 */
void handleApiAdc() {
  uint16_t raw = readAdcAveraged();
  float v = adcToVoltage(raw);
  
  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"adc_raw\":%u,\"voltage_v\":%.2f,\"vdiv_ratio\":%.2f,"
    "\"is_shutdown\":%s,\"threshold_v\":%.2f}",
    raw, v, VDIV_RATIO,
    isInShutdown ? "true" : "false",
    SHUTDOWN_THRESHOLD_V);
  
  server.send(200, "application/json", buf);
}

/**
 * Health check / status page.
 */
void handleRoot() {
  String html = R"HTML(
<!DOCTYPE html><html><head>
<title>ADC Bringup Monitor</title>
<style>
  body { font-family: monospace; max-width: 600px; margin: 2em auto;
         padding: 1em; background: #1e1e1e; color: #00ff00; }
  h1 { color: #00ff00; }
  .value { font-size: 1.2em; margin: 0.5em 0; }
  .status-ok { color: #00ff00; }
  .status-warn { color: #ffaa00; }
  .status-err { color: #ff3333; }
  pre { background: #0a0a0a; padding: 1em; border-radius: 4px;
        overflow-x: auto; }
</style>
<meta http-equiv="refresh" content="1">
</head><body>
<h1>ESP32 ADC Bringup Monitor</h1>
<p>Monitoring 9 V supply via GPIO 35 (ADC1_CH7)</p>
<hr>
)HTML";

  uint16_t raw = readAdcAveraged();
  float v = adcToVoltage(raw);

  html += "<div class=\"value\">";
  html += "Raw ADC: <strong>" + String(raw) + "</strong> counts (0–4095)<br>";
  html += "Voltage: <strong>" + String(v, 2) + "</strong> V<br>";
  html += "Status: <span class=\"" + String(isInShutdown ? "status-err" : "status-ok") + "\">";
  html += isInShutdown ? "⚠️  SHUTDOWN" : "✓ OK";
  html += "</span><br>";
  html += "Threshold: " + String(SHUTDOWN_THRESHOLD_V, 2) + " V<br>";
  html += "VDIV Ratio: " + String(VDIV_RATIO, 2) + "<br>";
  html += "</div><hr>";
  html += "<p><a href=\"/adc\">JSON API: /adc</a></p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ===== SETUP & LOOP =====

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== ADC Bringup Firmware v1 ===");
  Serial.println();

  // Configure ADC input
  pinMode(PIN_VSUPPLY, INPUT);
  analogReadResolution(ADC_RESOLUTION);
  analogSetAttenuation(ADC_11db);  // 11 dB attenuation: 0–3.3 V range
  Serial.printf("[Setup] ADC configured: PIN=%d, resolution=%d-bit, ref=%.2f V\n",
                PIN_VSUPPLY, ADC_RESOLUTION, ADC_VREF);
  Serial.printf("[Setup] VDIV_RATIO=%.2f, SHUTDOWN_THRESHOLD=%.2f V\n",
                VDIV_RATIO, SHUTDOWN_THRESHOLD_V);

  // Start WiFi (AP+STA to match main firmware behavior)
  Serial.println("[Setup] Starting WiFi AP+STA...");
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD)) {
    Serial.println("[Setup] ❌ softAP failed!");
  } else {
    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[Setup] ✓ AP active: '%s' at %s\n", WIFI_AP_SSID, ip.toString().c_str());
  }

  // Start mDNS
  if (MDNS.begin(WIFI_AP_HOSTNAME)) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    Serial.printf("[Setup] ✓ mDNS: http://%s.local\n", WIFI_AP_HOSTNAME);
  }

  // Register web handlers
  server.on("/", HTTP_GET, [](){ handleRoot(); });
  server.on("/adc", HTTP_GET, [](){ handleApiAdc(); });
  server.onNotFound([](){ handleNotFound(); });
  server.begin();
  Serial.println("[Setup] ✓ HTTP server started on port 80");

  Serial.println();
  Serial.println("=== Starting ADC monitoring ===");
  Serial.println("CALIBRATION INSTRUCTIONS:");
  Serial.println("  1. Measure actual voltage on 9V rail with multimeter");
  Serial.println("  2. Note the ADC reading printed below");
  Serial.println("  3. Calculate: VDIV_RATIO = actual_V / calculated_V");
  Serial.println("  4. Update VDIV_RATIO in this sketch and re-flash");
  Serial.println();
}

void loop() {
  server.handleClient();

  uint32_t now = millis();

  // Sample ADC periodically
  if (now - lastAdcSampleMs >= ADC_SAMPLE_INTERVAL_MS) {
    lastAdcSampleMs = now;
    uint16_t raw = readAdcAveraged();
    float v = adcToVoltage(raw);
    lastVoltage = v;
    updateShutdownState(v);
  }

  // Print report to serial periodically
  if (now - lastSerialReportMs >= SERIAL_REPORT_INTERVAL_MS) {
    lastSerialReportMs = now;
    
    if (lastVoltage >= 0.0f) {
      uint16_t raw = readAdcAveraged();
      float v = adcToVoltage(raw);
      
      Serial.printf("[%6lu] ADC=%4u  V_rail=%.2f V  Status=%s  Uptime=%lu s\n",
                    now,
                    raw,
                    v,
                    isInShutdown ? "SHUTDOWN" : "OK     ",
                    now / 1000);
    }
  }

  delay(5);  // Yield to WiFi stack
}
