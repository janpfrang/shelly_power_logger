# Software Architecture Document
## Shelly Plug S MTR Gen3 + ESP32 Power Logger

| | |
|---|---|
| **Document version** | 1.1 |
| **Firmware version** | v1 (Shelly + ESP32) |
| **Based on** | PZEM/ESP32 firmware v4 |
| **Organisation** | Braunhousehold · jan.pfrang@delonghigroup.com |
| **Status** | v1.1 — branding update, U9 + OTA added as open requirements, Req 27 fulfilled |
| **Purpose** | Reference for future maintenance, debugging, and extension of the firmware without requiring full re-read of all source files |

---

## Table of contents

1. [System overview](#1-system-overview)
2. [Software units](#2-software-units)
   - 2.1 [shelly_push.js — Shelly firmware script](#21-shelly_pushjs--shelly-firmware-script)
   - 2.2 [Config.h — ESP32 configuration header](#22-configh--esp32-configuration-header)
   - 2.3 [ShellyClient.h — Push receiver and watchdog](#23-shellyclienth--push-receiver-and-watchdog)
   - 2.4 [Logger.h — Ring buffer, SD writer, threshold gate](#24-loggerh--ring-buffer-sd-writer-threshold-gate)
   - 2.5 [StatusLed.h — LED blink driver](#25-statusledh--led-blink-driver)
   - 2.6 [WebPortal.h / WebPortal.cpp — Wi-Fi AP and HTTP server](#26-webportalh--webportalcpp--wi-fi-ap-and-http-server)
   - 2.7 [Shelly_ESP32_Logger.ino — Sketch entry point](#27-shelly_esp32_loggerino--sketch-entry-point)
3. [Unit interaction map](#3-unit-interaction-map)
4. [Data flow — measurement to SD](#4-data-flow--measurement-to-sd)
5. [How to make a specific change](#5-how-to-make-a-specific-change)
6. [File dependency map](#6-file-dependency-map)
7. [Known issues and limitations](#7-known-issues-and-limitations)
8. [Open requirements](#8-open-requirements)

---

## 1. System overview

The logger measures and records the power consumption of small kitchen appliances (hand mixers, blenders) in real day-use conditions. All 230 V handling is delegated to a certified Shelly Plug S MTR Gen3 smart plug. The user-built portion operates exclusively on 5 V DC.

| | |
|---|---|
| **Hardware** | Shelly Plug S MTR Gen3 (CE-marked, 16 A EU socket), ESP32-WROOM-32 module, MicroSD card on SPI shield, Certified 230 V → 5 V DC PSU |
| **Firmware split** | Shelly: mJS script (`shelly_push.js`) running on-device. ESP32: Arduino C++ firmware (7 source files) |
| **Network** | ESP32 is the Wi-Fi Access Point (SSID: `PZEM_Logger`). Shelly and user phone are both STA clients |
| **Data direction** | Shelly → ESP32 (push, every 1 s) · ESP32 → Phone (pull, browser polls `/api/live` every 1 s) · ESP32 → SD (internal write, every 10 s) |
| **No internet** | Cloud disabled on Shelly. All traffic stays on `192.168.4.0/24` |

### 1.1 Key architectural decision: push, not poll

In the predecessor PZEM/ESP32 v4 firmware, the ESP32 owned the sensor and polled it synchronously over UART. In this firmware the data flow is reversed: the Shelly pushes measurements to the ESP32 via HTTP POST. This means:

- The ESP32 never initiates a connection to the Shelly (no HTTP client code on the ESP32 side)
- Measurement timestamps are driven by the Shelly's internal timer, not by ESP32 polling jitter
- The ESP32 data path (ring buffer, SD flush, web API) is unchanged from v4 — only the data source changes
- `ShellyClient.h` acts as the adapter: it looks like a sensor to `Logger.h` but is actually a push receiver

---

## 2. Software units

The firmware consists of seven software units. One runs on the Shelly, six run on the ESP32.

| Unit | Runs on | Language | Purpose |
|---|---|---|---|
| `shelly_push.js` | Shelly device | mJS script | Measurement source, push transmitter |
| `Config.h` | ESP32 | C++ header | All compile-time constants. Single source of truth |
| `ShellyClient.h` | ESP32 | C++ class | Push receiver, JSON parser, watchdog |
| `Logger.h` | ESP32 | C++ class | Ring buffer, SD writer, threshold filter |
| `StatusLed.h` | ESP32 | C++ class | LED blink pattern driver |
| `WebPortal.h/.cpp` | ESP32 | C++ class | Wi-Fi AP, HTTP server, all web routes |
| `Shelly_ESP32_Logger.ino` | ESP32 | Arduino sketch | Object wiring, `setup()`, `loop()` orchestration |

---

### 2.1 `shelly_push.js` — Shelly firmware script

> **This unit runs on the Shelly device, NOT on the ESP32.** It is installed once via the Shelly web UI (Scripts → Create script) and runs autonomously on every power-on.

| | |
|---|---|
| **Language** | mJS (minimal JavaScript, Shelly built-in runtime) |
| **Runs on** | Shelly Plug S MTR Gen3 internal processor |
| **Start trigger** | "Run on startup" toggle — auto-starts on every power-on |
| **File** | `shelly_push.js` |

#### Responsibilities

- Read Switch component 0 status via `Shelly.getComponentStatus()` — zero-cost local call, no network
- Derive `pf_apparent = apower / (voltage × current)`, clamped to `[0, 1]`
- Serialise measurement as compact JSON: `{ ts, v, p, i, pf }`
- HTTP POST to `http://192.168.4.1/api/shelly_push` every `PUSH_INTERVAL_MS` (default 1000 ms)
- Guard against request stacking: if previous HTTP call is still pending, skip this tick (`_pushPending` flag)
- Force relay ON at startup (`Switch.Set`) to prevent accidental load disconnection

#### Key constants — only section that should be edited

| Constant | Default | Notes |
|---|---|---|
| `PUSH_INTERVAL_MS` | `1000` | Push cadence in ms. Must match `INTERVAL_SHELLY_POLL_MS` in `Config.h` |
| `ESP32_IP` | `"192.168.4.1"` | ESP32 softAP fixed IP. Never changes in Option B topology |
| `SWITCH_ID` | `0` | Shelly channel index. Plug S always has one channel: 0 |

#### Known limitations

- Push rate is fixed at compile-time in JS. Changing the logging rate in the ESP32 Settings UI does **not** slow down the Shelly push — the Shelly always pushes at 1 s. Extra pushes are silently discarded by `Logger.h`.
- HTTP timeout = 2 s. If the ESP32 takes > 1 s to respond, the next push is delayed, which can trigger the watchdog under sustained ESP32 load. See Section 7, issue #4.

---

### 2.2 `Config.h` — ESP32 configuration header

| | |
|---|---|
| **Language** | C++ preprocessor defines |
| **Purpose** | Single source of truth for ALL compile-time constants. No magic numbers anywhere else in the firmware |
| **File** | `Config.h` |
| **Depends on** | Nothing — included by all other units |

#### All constants

| Constant | Value | Notes |
|---|---|---|
| `PIN_SD_CS/MOSI/CLK/MISO` | `25 / 14 / 27 / 26` | SPI pins for SD card shield. GPIO 14 has a boot-time pull-up — see Section 7 |
| `PIN_LED` | `32` | Status LED output (active HIGH) |
| `PIN_BUTTON` | `33` | Future use: manual reset trigger (`INPUT_PULLUP`, currently unused) |
| `PIN_VSUPPLY` | `35` | Future use: ADC supply monitoring for power-loss detection |
| `SHELLY_HOST` | `"192.168.4.2"` | **Dead code in v1** — leftover from poll design. ESP32 never contacts Shelly in push architecture |
| `SHELLY_PUSH_ENDPOINT` | `"/api/shelly_push"` | Route registered in WebPortal. Must match `PUSH_URL` in `shelly_push.js` |
| `SHELLY_ERROR_THRESHOLD` | `3` | Watchdog: silence for 3 × `INTERVAL_SHELLY_POLL_MS` triggers `LED_ERROR` |
| `INTERVAL_SHELLY_POLL_MS` | `1000 ms` | Default logging cadence. Also watchdog multiplier. Minimum meaningful: 1000 ms |
| `INTERVAL_SD_FLUSH_MS` | `10000 ms` | How often Logger flushes the RAM ring buffer to SD |
| `INTERVAL_LED_OK_MS` | `500 ms` | LED toggle period in OK state = 1 Hz blink |
| `INTERVAL_LED_ERR_MS` | `100 ms` | LED toggle period in ERROR state = 5 Hz blink |
| `DEFAULT_POWER_THRESHOLD_W` | `0.0 W` | Log all samples by default. Configurable at runtime via Settings UI |
| `RAM_BUFFER_SIZE` | `64 entries` | Ring buffer capacity. 64 × 16 bytes = 1024 bytes. At 1 s cadence: 64 s reserve |
| `API_BUFFER_SIZE` | `320 bytes` | Stack-allocated `char[]` for JSON responses. Worst-case `/api/live` = ~125 bytes (195 byte margin) |
| `LOG_FILE_PATH` | `"/log.csv"` | SD card file path. Fixed |
| `LOG_FILE_HEADER` | `time_ms,...` | CSV header written on first use or after reset. Column 4 is `pf_apparent` (derived, not hardware PF) |

---

### 2.3 `ShellyClient.h` — Push receiver and watchdog

| | |
|---|---|
| **Language** | C++ header-only class |
| **Purpose** | Receive, validate, and cache measurements pushed by `shelly_push.js`. Expose them as synchronous getters to `Logger.h`. Run a watchdog that detects Shelly dropout |
| **File** | `ShellyClient.h` |
| **Instantiated in** | `Shelly_ESP32_Logger.ino` (global: `ShellyClient shelly`) |
| **Depends on** | `Config.h`, ArduinoJson library |
| **Used by** | `WebPortal.cpp` (calls `ingest()`), `Logger.h` (calls getters + `shellyOk()`) |

#### Internal data

| Member | Description |
|---|---|
| `ShellyMeasurement _latest` | Cached last measurement: voltage, power, current, pf_apparent, valid flag |
| `uint32_t _lastPushMs` | `millis()` timestamp of last successful `ingest()`. Used by watchdog |
| `uint8_t _errorCount` | Consecutive parse/validation failure counter. Resets on success. Wraps at 255 (see issue #8) |

#### Public interface

| Method | Description |
|---|---|
| `ingest(const String& body)` | Called by `WebPortal::handleShellyPush()` with raw HTTP POST body. Parses JSON, validates bounds, caches result. Returns `true` on success |
| `getVoltage()` | Returns `_latest.voltage` (V). `NAN` until first successful ingest |
| `getPower()` | Returns `_latest.power` (W). `NAN` until first successful ingest |
| `getPfApparent()` | Returns `_latest.pf_apparent`. `NAN` until first successful ingest |
| `hasData()` | Returns `true` once at least one valid push has been received |
| `shellyOk()` | Watchdog check: returns `false` if no push in `SHELLY_ERROR_THRESHOLD × INTERVAL_SHELLY_POLL_MS` ms, or if no data has ever arrived |
| `getErrorCount()` | Returns consecutive error count (diagnostic only, not used in logic) |

#### Validation bounds in `ingest()`

| Field | Range |
|---|---|
| Voltage `v` | 0.0 – 300.0 V |
| Power `p` | 0.0 – 3680.0 W (16 A × 230 V = Shelly rated maximum) |
| Current `i` | 0.0 – 16.0 A |
| PF `pf` | 0.0 – 1.01 (0.01 tolerance for float rounding) |

#### Thread safety

The ESP32 Arduino WebServer runs single-threaded in the same `loop()` context. `WebServer::handleClient()` is called from `loop()` and executes POST handlers synchronously before returning. No RTOS mutex is needed — `ingest()` and the Logger getters never run concurrently.

---

### 2.4 `Logger.h` — Ring buffer, SD writer, threshold gate

| | |
|---|---|
| **Language** | C++ header-only class |
| **Purpose** | Consume measurements from `ShellyClient`, apply power threshold filter, buffer samples in RAM, flush periodically to SD card, recover from SD failures |
| **File** | `Logger.h` |
| **Instantiated in** | `Shelly_ESP32_Logger.ino` (global: `Logger logger(shelly)`) |
| **Depends on** | `Config.h`, `ShellyClient.h`, Arduino SD library, SPI |
| **Used by** | `Shelly_ESP32_Logger.ino` (calls `pollIfDue`, `flushIfDue`), `WebPortal.cpp` (calls getters, `flushToSD`, `resetSDFile`, `openLogFileForRead`) |

#### Key design: Logger does NOT own the sensor

`ShellyClient` is injected via constructor reference. `Logger` calls `_shelly.getVoltage()` etc. in `pollIfDue()` — the same call pattern as the old `_pzem.voltage()`. This means `Logger.h` required only minimal changes from v4: the PZEM calls were replaced line-for-line with ShellyClient calls.

#### Internal state

| Member | Description |
|---|---|
| `Sample _buffer[64]` | RAM ring buffer. `Sample = { uint32_t millis_ts, float voltage_V, float power_W, float pf }` |
| `size_t _bufferCount` | Number of valid entries currently in the buffer. 0 – 64 |
| `uint32_t _droppedSamples` | Cumulative count of samples dropped due to buffer full + SD unavailable |
| `uint32_t _lastPollMs` | Timestamp of last `pollIfDue()` execution (not last push) |
| `uint32_t _lastFlushMs` | Timestamp of last `flushIfDue()` execution |
| `uint32_t _lastSdRetryMs` | Timestamp of last SD re-init attempt (retry gate: 30 s cooldown) |
| `float _last{Voltage,Power,Pf}` | Live display cache. Updated every `pollIfDue()` regardless of threshold |
| `bool _sdOk` | SD card health flag. Set `false` on `SD.open()` failure, recovered by `tryRecoverSD()` |
| `uint32_t _pollIntervalMs` | Runtime-adjustable. Default `INTERVAL_SHELLY_POLL_MS`. Range: 1000 – 30000 ms |
| `float _powerThresholdW` | Runtime-adjustable. Default 0.0 W (log all). Sample only stored if P ≥ threshold |

#### Public interface

| Method | Description |
|---|---|
| `begin()` | Initialise SPI + SD card. Returns `true` if SD ready |
| `pollIfDue()` | Called every `loop()`. If `_pollIntervalMs` elapsed: read ShellyClient cache, update live display, conditionally push sample to ring buffer |
| `flushIfDue()` | Called every `loop()`. If `INTERVAL_SD_FLUSH_MS` elapsed: flush ring buffer to SD, or attempt SD recovery if `sdOk=false` |
| `flushToSD()` | Public: write all buffered samples to `/log.csv` (`FILE_APPEND`), clear buffer. Called by `flushIfDue()` and `handleDownload()` |
| `resetSDFile()` | Delete `/log.csv` and write fresh header. Resets `_bufferCount` to 0 without flushing first (see known issue #1) |
| `openLogFileForRead()` | Return SD `File` handle for streaming. Caller must close. Returns `File()` if `sdOk=false` |
| `setPollInterval(ms)` | Set `_pollIntervalMs` at runtime. Rejects values < 1000 ms |
| `setPowerThreshold(W)` | Set `_powerThresholdW` at runtime. Accepts 0.0 (no limit) |
| `shellyOk() / sdOk() / ok()` | Status flags. `ok() = shellyOk() && sdOk()`. Drives LED via `loop()` |
| `getLastVoltage/Power/Pf()` | Live display values for `/api/live`. May be `NAN` if Shelly not yet connected |
| `getBufferCount() / getDroppedSamples()` | Diagnostics shown in web UI |

#### Ring buffer overflow behaviour (`pushSample`)

- Normal: `_bufferCount < 64` → append and increment
- Buffer full + SD available: emergency `flushToSD()`, then append
- Buffer full + SD failed: drop oldest entry (FIFO), write newest at tail, increment `_droppedSamples`
- Drop is logged to Serial on first occurrence and every 100 drops thereafter

#### SD recovery (`tryRecoverSD`)

Triggered by `flushIfDue()` when `_sdOk = false`. Rate-limited to one attempt every 30 s. Calls `SD.end()`, then `delay(50)` *(known issue #6 — blocks loop)*, then `SD.begin()`. On success, recreates log header if file is missing.

---

### 2.5 `StatusLed.h` — LED blink driver

| | |
|---|---|
| **Language** | C++ header-only class |
| **Purpose** | Non-blocking LED blink at two rates: 1 Hz (OK) or 5 Hz (ERROR) |
| **File** | `StatusLed.h` |
| **Unchanged from** | PZEM/ESP32 v4 — not a single line changed |
| **Depends on** | `Config.h` |
| **Used by** | `Shelly_ESP32_Logger.ino` |

#### States

| State | Behaviour |
|---|---|
| `LED_OK` | Blink at `INTERVAL_LED_OK_MS` = 500 ms toggle → 1 Hz. System healthy |
| `LED_ERROR` | Blink at `INTERVAL_LED_ERR_MS` = 100 ms toggle → 5 Hz. SD error or Shelly watchdog fired |

#### Public interface

| Method | Description |
|---|---|
| `begin()` | Configure `PIN_LED` as OUTPUT, set initial state `LED_OK`, reset timer |
| `setState(s)` | Change state. Only resets blink phase if state actually changes (idempotent when called every loop iteration) |
| `setOk(bool ok)` | Convenience wrapper: `setState(ok ? LED_OK : LED_ERROR)` |
| `update()` | Must be called every `loop()`. Checks elapsed time; if ≥ interval, toggles LED pin |

The LED is the primary offline health indicator. When no phone is connected, the LED is the only way to know if the system is healthy. **Green blink = data flowing from Shelly AND SD writing. Red blink = at least one has failed.**

---

### 2.6 `WebPortal.h` / `WebPortal.cpp` — Wi-Fi AP and HTTP server

| | |
|---|---|
| **Language** | C++ class (declaration in `.h`, implementation in `.cpp`) |
| **Purpose** | Create and manage the Wi-Fi Access Point. Register and serve all HTTP routes. Bridge the Shelly push endpoint to `ShellyClient`. Serve the web UI pages |
| **Files** | `WebPortal.h`, `WebPortal.cpp` |
| **Instantiated in** | `Shelly_ESP32_Logger.ino` (global: `WebPortal webPortal(logger, shelly)`) |
| **Depends on** | `Config.h`, `Logger.h`, `ShellyClient.h`, WiFi, WebServer, DNSServer, ESPmDNS |
| **Used by** | `Shelly_ESP32_Logger.ino` (calls `begin()`, `update()`) |

#### Wi-Fi configuration

| Parameter | Value |
|---|---|
| **Mode** | `WIFI_AP_STA` — ESP32 is both an Access Point and has a STA interface (STA unused in v1, reserved for future NTP etc.) |
| **AP SSID** | `PZEM_Logger` (`WIFI_AP_SSID`) |
| **AP Password** | `logger1234` (`WIFI_AP_PASSWORD`, ≥ 8 chars) |
| **AP Gateway** | `192.168.4.1` (fixed, always this for ESP32 softAP) |
| **DHCP** | Managed by ESP32 AP stack. Shelly gets `.2` (first lease), phone gets `.3+` |
| **DNS** | Catch-all DNS server (`DNSServer`) redirects all queries to `192.168.4.1` (captive portal) |
| **mDNS** | `braun_PZEM.local` (`WIFI_AP_HOSTNAME`). Service: `_http._tcp` port 80 |

#### HTTP routes

| Route | Handler | Response | Description |
|---|---|---|---|
| `GET /` | `handleRoot()` | PROGMEM HTML | Live dashboard: power, voltage, pf_apparent |
| `GET /api/live` | `handleApiLive()` | JSON (stack buf 320 B) | `{ power, voltage, pf, buffer, dropped, uptime, shelly_ok, sd_ok }` |
| `GET /api/settings` | `handleApiSettings()` | JSON | `{ poll_ms, power_threshold }` |
| `POST /api/settings` | `handleApiSettingsSave()` | JSON | Update poll_ms and/or power_threshold. Whitelisted values only |
| `POST /api/shelly_push` | `handleShellyPush()` | JSON | **New in v1.** Receives Shelly measurement push. Calls `ShellyClient::ingest()` |
| `GET /download` | `handleDownload()` | CSV stream | Flush RAM buffer, stream `/log.csv` as attachment |
| `POST /reset` | `handleReset()` | text/plain | Delete and recreate `/log.csv` |
| `GET /settings` | `handleSettings()` | PROGMEM HTML | Rate selector + threshold selector UI |
| `GET /liveplot` | `handleLivePlot()` | PROGMEM HTML | Canvas oscilloscope, auto-scaled, 60-sample rolling window |
| `GET /readme` | `handleReadme()` | PROGMEM HTML | Built-in user manual stub |
| `GET /generate_204` + 7 more | `handleCaptivePortal()` | 302 redirect | Android/iOS/Windows captive portal probes → redirect to `192.168.4.1` |
| `*` (not found) | `handleNotFound()` | 302 redirect | Unknown URLs → same captive portal redirect |

#### Settings whitelist (`handleApiSettingsSave`)

| Parameter | Allowed values |
|---|---|
| `poll_ms` | `1000, 2000, 5000, 10000, 30000` ms. (200 ms and 500 ms removed vs. v4 — Shelly meter updates at ~1 Hz) |
| `power_threshold` | `0, 1, 2, 5, 10, 20, 50` W. (0 = log everything) |
| Invalid value | HTTP 400 with JSON error body |

#### PROGMEM HTML pages

All HTML pages are stored in flash memory (`PROGMEM`) as C-string literals using `R"HTML(...)HTML"` raw strings. They are served using `server.send_P()` which streams directly from flash without copying to heap. This avoids heap fragmentation on long-running sessions.

---

### 2.7 `Shelly_ESP32_Logger.ino` — Sketch entry point

| | |
|---|---|
| **Language** | Arduino C++ sketch |
| **Purpose** | Declare all global objects in correct dependency order. Wire them together. Implement `setup()` and `loop()` |
| **File** | `Shelly_ESP32_Logger.ino` |
| **Depends on** | All other headers |

#### Object instantiation order

Order matters because `Logger` takes a reference to `ShellyClient`, and `WebPortal` takes references to both:

```cpp
ShellyClient shelly;                    // no dependencies
Logger       logger(shelly);            // needs shelly
WebPortal    webPortal(logger, shelly); // needs both
StatusLed    statusLed;                 // no dependencies
```

#### `setup()` sequence

| Step | What it does |
|---|---|
| `Serial.begin(115200)` | Debug output |
| `pinMode(PIN_BUTTON/VSUPPLY)` | Configure future-use pins |
| `statusLed.begin()` | LED output, set to `LED_ERROR` (red) until init complete |
| `logger.begin()` | SD SPI init. Sets `_sdOk`. Prints OK/FEHLER to Serial |
| `ensureLogHeader()` | If `/log.csv` does not exist on SD, create with header row |
| `webPortal.begin()` | Start AP, DNS, mDNS, HTTP server, register all routes |
| `statusLed.setOk(false)` | Stay red — Shelly not yet connected |

#### `loop()` sequence — called continuously

| Step | Description |
|---|---|
| `1. logger.pollIfDue()` | Every `_pollIntervalMs`: read ShellyClient cache, gate on threshold, push to ring buffer. ~0 ms when not due |
| `2. logger.flushIfDue()` | Every 10 s: write ring buffer to SD. May call `tryRecoverSD()` with 50 ms blocking delay |
| `3. webPortal.update()` | Service one pending HTTP request. Shelly push arrives here → calls `ShellyClient::ingest()`. ~1–50 ms when active |
| `4. statusLed.setOk(logger.ok())` | Update LED state: `ok() = shellyOk() && sdOk()`. Idempotent |
| `5. statusLed.update()` | Toggle LED pin if blink interval has elapsed. ~0 ms |

> **Loop timing:** typically 1–5 ms. When `webPortal.update()` processes a Shelly push: 5–20 ms. During SD flush: up to 100–300 ms (`SD.write()` is blocking). During SD recovery: +50 ms (`delay(50)`). No RTOS tasks or interrupts are used.

---

## 3. Unit interaction map

Every runtime interaction between units. Each row is one directional call or data flow.

| From | To | Mechanism | Payload |
|---|---|---|---|
| `shelly_push.js` | WebPortal (ESP32) | HTTP POST `/api/shelly_push` every 1 s | JSON: `{ ts, v, p, i, pf }` |
| `WebPortal` | `ShellyClient` | `handleShellyPush()` calls `ingest(body)` | Raw HTTP body string |
| `ShellyClient` | `ShellyClient` | Validates, parses, caches measurement, updates `_lastPushMs` | Internal |
| `Logger` (loop) | `ShellyClient` | `pollIfDue()` calls `getVoltage/Power/PfApparent`, `hasData`, `shellyOk` | float values |
| `Logger` | SD card | `flushToSD()` appends CSV rows via `SD.open(FILE_APPEND)` | CSV bytes |
| `WebPortal` | `Logger` | `handleApiLive()` reads `getLastPower/Voltage/Pf`, `shellyOk`, `sdOk` | float / bool |
| `WebPortal` | `Logger` | `handleDownload()` calls `flushToSD()` then `openLogFileForRead()` | File handle |
| `WebPortal` | `Logger` | `handleReset()` calls `resetSDFile()` | bool result |
| `WebPortal` | `Logger` | `handleApiSettingsSave()` calls `setPollInterval`, `setPowerThreshold` | uint32 / float |
| Browser | `WebPortal` | `GET /api/live` every 1 s | HTTP request |
| Browser | `WebPortal` | `GET /api/settings`, `POST /api/settings` | HTTP request |
| Browser | `WebPortal` | `GET /download`, `POST /reset` | HTTP request |
| `.ino loop()` | `StatusLed` | `setOk(logger.ok())` every iteration | bool |
| `.ino loop()` | `Logger` | `pollIfDue()`, `flushIfDue()` every iteration | void |
| `.ino loop()` | `WebPortal` | `update()` every iteration | void |

---

## 4. Data flow — measurement to SD

A single measurement traced from the Shelly meter chip to `/log.csv`.

| Step | Description |
|---|---|
| **1 — Shelly meter chip** | Measures V, I, P at hardware level (~1 Hz update rate). Stored in Switch component 0 internal state |
| **2 — `shelly_push.js`** | Reads component 0 via `Shelly.getComponentStatus()`. Derives `pf_apparent`. Calls `buildPayload()` → JSON string |
| **3 — HTTP POST** | Shelly sends `POST http://192.168.4.1/api/shelly_push`. Body: `{ ts, v, p, i, pf }`. ~1–20 ms transit time on local AP |
| **4 — `WebPortal::handleShellyPush()`** | Reads body via `_server.arg("plain")`. Calls `ShellyClient::ingest(body)`. Returns `200 OK` or `400` |
| **5 — `ShellyClient::ingest()`** | `StaticJsonDocument<192>` parses body. Validates bounds. Writes to `_latest` struct. Updates `_lastPushMs = millis()` |
| **6 — `Logger::pollIfDue()`** | Fires every `_pollIntervalMs`. Reads `_shelly.getVoltage/Power/PfApparent()`. Updates live display cache. If P ≥ threshold: calls `pushSample()` |
| **7 — `Logger::pushSample()`** | Appends `Sample { millis_ts, voltage_V, power_W, pf }` to `_buffer[_bufferCount++]` |
| **8 — `Logger::flushIfDue()`** | Fires every 10 s. Calls `flushToSD()` |
| **9 — `Logger::flushToSD()`** | Opens `/log.csv` `FILE_APPEND`. Writes each buffered sample as CSV line: `"%lu,%.1f,%.1f,%.2f\n"`. Flushes and closes. Resets `_bufferCount = 0` |
| **10 — `/log.csv` on SD** | Permanent record. Downloaded via `GET /download`. Format: `time_ms,voltage_V,power_W,pf_apparent` |

### Timestamps

The `millis_ts` in the CSV is the ESP32's `millis()` value at the time `pollIfDue()` fires, **not** the Shelly's `ts` field from the JSON. The Shelly `ts` is received but not stored. At 1 s logging rate with a 1–20 ms push transit time, the timestamp offset is negligible for kitchen appliance power analysis.

---

## 5. How to make a specific change

Use this section to identify exactly which file(s) to edit. Load only those files — not the whole codebase.

| Change | Files and actions |
|---|---|
| **Change a hardware pin** | `Config.h` only. Change the relevant `PIN_*` define |
| **Change default logging rate or SD flush rate** | `Config.h` only. Change `INTERVAL_SHELLY_POLL_MS` or `INTERVAL_SD_FLUSH_MS` |
| **Change Wi-Fi SSID or password** | `Config.h` only. Change `WIFI_AP_SSID` / `WIFI_AP_PASSWORD`. Also re-provision Shelly Wi-Fi settings to match |
| **Change the Shelly push interval** | `shelly_push.js`: change `PUSH_INTERVAL_MS` at top of file. Also update `INTERVAL_SHELLY_POLL_MS` in `Config.h` to match |
| **Add a new allowed poll rate in Settings UI** | `WebPortal.cpp`: add to `allowed_ms[]` in `handleApiSettingsSave()`. Add button in `PAGE_SETTINGS` HTML (`data-ms` attribute). Add to `msToLabel()` JS map |
| **Add a new power threshold option** | `WebPortal.cpp`: add to `allowed_w[]` in `handleApiSettingsSave()`. Add button in `PAGE_SETTINGS` HTML (`data-w` attribute) |
| **Add a new CSV column** | `Config.h`: update `LOG_FILE_HEADER`. `Logger.h`: add field to `Sample` struct, update `f.printf()` in `flushToSD()`, populate new field in `pollIfDue()` |
| **Add a new web page** | `WebPortal.cpp`: add `PAGE_XXX` PROGMEM string, add route in `begin()` and handler method. `WebPortal.h`: declare handler method |
| **Persist settings across reboots (NVS)** | `Logger.h`: add `#include <Preferences.h>`, add `Preferences _prefs` member, load stored values in `begin()`, call `_prefs.putUInt/putFloat` in setters |
| **Add IP check on `/api/shelly_push`** | `WebPortal.cpp`: in `handleShellyPush()`, add `remoteIP() == 192.168.4.2` check before `ingest()` |
| **Fix stale live display at slow poll rates** | `ShellyClient.h`: add public getters for live values. `Logger.h`: update `getLastVoltage/Power/Pf` to read from `ShellyClient` directly, bypassing `pollIfDue()` gate |
| **Fix watchdog false alarms under load** | `Config.h`: increase `SHELLY_ERROR_THRESHOLD` from `3` to `5` |
| **Implement button reset (Req 25/26)** | `Shelly_ESP32_Logger.ino`: add `digitalRead(PIN_BUTTON)` in `loop()`. On LOW: call `logger.resetSDFile()` |
| **Implement supply monitor (Req 13)** | `Shelly_ESP32_Logger.ino`: add `analogRead(PIN_VSUPPLY)` in `loop()`. On low voltage: call `logger.flushToSD()` |
| **Add date/time to log (U 9)** | Hardware decision required first (RTC or NTP). **With DS3231 RTC:** `Logger.h`: add `#include <RTClib.h>`, read `DateTime` in `pollIfDue()`, write ISO timestamp instead of `millis_ts`. **With NTP:** `WebPortal.cpp`: sync via `configTime()` in `begin()`, read `time_t` in `Logger::pollIfDue()`. Both: `Config.h`: add datetime column to `LOG_FILE_HEADER` |
| **Add OTA firmware updates** | `WebPortal.cpp`: `#include <ArduinoOTA.h>`, call `ArduinoOTA.begin()` in `begin()` and `ArduinoOTA.handle()` in `update()`. `Config.h`: add `OTA_PASSWORD` define. Arduino IDE: select OTA-capable partition scheme |

---

## 6. File dependency map

An arrow means "requires at compile time".

| File | Depends on |
|---|---|
| `shelly_push.js` | No dependencies (standalone mJS script on Shelly) |
| `Config.h` | No dependencies — included by all others |
| `ShellyClient.h` | `Config.h`, ArduinoJson |
| `StatusLed.h` | `Config.h` |
| `Logger.h` | `Config.h`, `ShellyClient.h`, Arduino SD, SPI |
| `WebPortal.h` | `Config.h`, `Logger.h`, `ShellyClient.h`, WiFi, WebServer, DNSServer, ESPmDNS |
| `WebPortal.cpp` | `WebPortal.h` (and transitively all above) |
| `Shelly_ESP32_Logger.ino` | `Config.h`, `StatusLed.h`, `ShellyClient.h`, `Logger.h`, `WebPortal.h` |

### External library dependencies (Arduino Library Manager)

| Library | Version | Used in |
|---|---|---|
| ArduinoJson by Benoit Blanchon | ≥ v6.x | JSON parsing in `ShellyClient::ingest()`. `StaticJsonDocument<192>` avoids heap allocation |
| SD (built-in ESP32 package) | any | File I/O in `Logger.h` |
| WiFi (built-in) | any | softAP in `WebPortal.cpp` |
| WebServer (built-in) | any | HTTP server in `WebPortal.cpp` |
| DNSServer (built-in) | any | Captive portal in `WebPortal.cpp` |
| ESPmDNS (built-in) | any | `braun_PZEM.local` hostname in `WebPortal.cpp` |

---

## 7. Known issues and limitations

Issues are not blocking for normal lab use. They are documented here to enable targeted future fixes without re-reading all source code.

| # | Issue | Description | Fix |
|---|---|---|---|
| **#1** 🔴 | `resetSDFile()` discards RAM buffer | `Logger.h`: `resetSDFile()` sets `_bufferCount=0` without flushing first. Up to 10 s of samples are lost silently on reset | Add `flushToSD()` call before `_bufferCount=0`. One line |
| **#2** 🟡 | Push/log cadence decoupled | Shelly always pushes at 1 s. At 30 s logging rate, 29 of 30 pushes are discarded. "Sampling Rate" label in UI is misleading — it is actually "Logging Rate" | Rename UI label to "Logging Rate". Full fix: propagate rate change to Shelly via RPC |
| **#3** 🟡 | Live display stale at slow poll rates | `getLastVoltage/Power/Pf` only update in `pollIfDue()`. At 30 s rate, web UI numbers freeze for up to 30 s after Shelly reconnects | Move live cache update into `ShellyClient::ingest()` or a separate 1 s fast-poll path |
| **#4** 🟡 | Watchdog too tight under ESP32 load | Watchdog = 3 × 1000 ms = 3 s. Shelly HTTP timeout = 2 s. Slow ESP32 response can slip push cadence to ~2 s, causing false Shelly FEHLER | `Config.h`: change `SHELLY_ERROR_THRESHOLD` from `3` to `5`. One-line fix |
| **#5** 🟡 | `handleDownload` ignores `flushToSD` return value | If pre-download flush fails, download proceeds with incomplete data and no warning | Check return value; log warning to Serial if flush failed and buffer non-empty |
| **#6** 🟡 | `delay(50)` in `tryRecoverSD()` blocks `loop()` | 50 ms blocking delay during SD recovery stalls HTTP serving. Can interrupt an active file download | Remove `delay(50)`. `SD.end()` + `SD.begin()` do not require a manual delay |
| **#7** 🟠 | `SHELLY_HOST` is dead code | `Config.h` defines `SHELLY_HOST = 192.168.4.2` but no code references it. Push architecture makes it unnecessary | Remove define, or add comment noting it is reserved for future RPC |
| **#8** 🟠 | `_errorCount` wraps at 255 | `ShellyClient._errorCount` is `uint8_t`. After 255 consecutive errors it silently resets to 0 | Change to `uint16_t`. One word |
| **#9** 🟠 | No IP check on `/api/shelly_push` | Any device on `PZEM_Logger` AP can POST fake data to `/api/shelly_push` and corrupt the log | Add `remoteIP() == 192.168.4.2` check in `handleShellyPush()`. Five lines |
| **#10** 🟠 | Settings not persisted across reboots | `poll_ms` and `power_threshold` revert to defaults on power cycle. Unattended long tests revert to 1 s logging | Use ESP32 `Preferences` (NVS). Add load in `begin()`, save in setters. ~30 min effort |

---

## 8. Open requirements

Requirements with status "Not fulfilled" in `Requirements__Shelly_ESP32__v1.xlsx`. Listed here for forward planning.

| Requirement | Feature | Implementation path |
|---|---|---|
| **Req 13** | PIN_VSUPPLY (GPIO 35) monitor | On supply voltage drop: call `logger.flushToSD()` to prevent data loss on power cut. Requires `analogRead()` + threshold comparison in `loop()` |
| **Req 25/26** | PIN_BUTTON (GPIO 33) manual reset | Long-press: call `logger.resetSDFile()`. Short-press TBD. Requires `digitalRead()` + debounce logic in `loop()` |
| **Req 27** | Physical label on device | ✅ Fulfilled in v1_01: label added with SSID, password, `http://192.168.4.1`, and Shelly IP `http://192.168.4.3` |
| **LED 3rd state** | LED blink 0.5 Hz when client connected | `WiFi.softAPgetStationNum() > 0` → new `LED_OK_WIFI` state at 2000 ms interval. `StatusLed.h` change only |
| **Settings NVS** | Persist settings across reboots | See known issue #10. Affects `Logger.h` only |
| **U 9 (new)** | Date and time logging | ESP32 has no RTC. Options: (a) add DS3231 RTC module on I2C, (b) sync NTP via STA Wi-Fi when connected to home network, (c) log relative `time_ms` only and correlate externally. Requires hardware decision before firmware work |
| **OTA (new)** | Over-the-air firmware updates without USB | ESP32 Arduino OTA library (`ArduinoOTA`) or HTTP OTA (`Update` class). Add OTA handler in `WebPortal::begin()` and protect with password. Requires ~320 KB free flash partition for dual-bank OTA |

---

*Braunhousehold · jan.pfrang@delonghigroup.com · Firmware v1 · Architecture v1.1*
