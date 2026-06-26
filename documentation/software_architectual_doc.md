# Software Architecture Document
## Shelly Plug S MTR Gen3 + ESP32 Power Logger

| | |
|---|---|
| **Document version** | 2.0 |
| **Firmware version** | v4 (Shelly + OTA + Power-loss + RTC + timing fixes) |
| **Previous version** | 1.1 (firmware v1) |
| **Organisation** | Braunhousehold · jan.pfrang@delonghigroup.com |
| **Status** | Major update — new units (PowerMonitor, RTC), WiFi mode change, watchdog changes, loop reorder, SD flush guard, download grace, all known issue statuses refreshed |
| **Purpose** | Reference for future maintenance, debugging, and extension of the firmware without requiring full re-read of all source files |

---

## Changelog from v1.1

| Area | What changed |
|---|---|
| New unit | `PowerMonitor.h` — 9 V rail ADC monitor, SD flush on mains loss (Req 13) |
| New hardware | DS3231 RTC module on I²C — wall-clock timestamps in CSV |
| `shelly_push.js` | Push interval 1000 → **1200 ms**; HTTP timeout 1 → **2 s**; rationale: eliminates `_pushPending` zero-margin race |
| `Config.h` | `SHELLY_ERROR_THRESHOLD` 3 → **5**; `SHELLY_STARTUP_GRACE_MS` added; `PIN_RTC_SDA/SCL` added; `SD_FLUSH_TIMEOUT_MS` added; `LOG_FILE_HEADER` datetime column added; full `POWER_MONITOR_*` block added; `POWER_STARTUP_GRACE_MS` 3000 → 10000 ms |
| `ShellyClient.h` | `beginStartupGrace()` added — called from both `setup()` and `handleDownload()`; startup grace (15 s) suppresses watchdog during boot re-association and during file downloads |
| `Logger.h` | `RTC_DS3231*` injected via constructor (optional, `nullptr` = no RTC); `unix_ts` added to `Sample` struct; `setOtaInProgress()` / `isOtaInProgress()` added; `flushToSD()` gains SD_FLUSH_TIMEOUT_MS timing guard (400 ms); `reopenAfterRead()` added |
| `WebPortal.cpp` | WiFi mode `WIFI_AP_STA` → **`WIFI_AP`** + `WiFi.setSleep(false)`; DNS catch-all disabled; mDNS disabled; new routes `/update` (OTA), `/histogram`, `/liveplot`; `/api/live` adds `ota_active` field; `handleDownload()` calls `beginStartupGrace()` before `streamFile()` |
| `.ino` | `RTC_DS3231 rtc` + `PowerMonitor powerMonitor` added as globals; `Wire.begin()` + `rtc.begin()` in `setup()`; `handlePowerLoss()` terminal shutdown function; **loop order reversed**: `webPortal.update()` now runs first (before `pollIfDue()`) to eliminate one-loop timestamp lag |
| Known issues | #4 (watchdog too tight) **fixed** — threshold now 5; #6 (delay(50) in tryRecoverSD) still present |
| Requirements | Req 13 now **⚠ Partial** (HW+code ready, `POWER_MONITOR_ENABLED=0`); U9 **✅ fulfilled** (DS3231 RTC); OTA (Req 29) **✅ fulfilled** |

---

## Table of contents

1. [System overview](#1-system-overview)
2. [Software units](#2-software-units)
   - 2.1 [shelly_push.js — Shelly firmware script](#21-shelly_pushjs--shelly-firmware-script)
   - 2.2 [Config.h — ESP32 configuration header](#22-configh--esp32-configuration-header)
   - 2.3 [ShellyClient.h — Push receiver, watchdog, and startup grace](#23-shellyclienth--push-receiver-watchdog-and-startup-grace)
   - 2.4 [Logger.h — Ring buffer, SD writer, threshold gate, RTC, OTA gate](#24-loggerh--ring-buffer-sd-writer-threshold-gate-rtc-ota-gate)
   - 2.5 [StatusLed.h — LED blink driver](#25-statusledh--led-blink-driver)
   - 2.6 [PowerMonitor.h — 9 V rail ADC monitor](#26-powermonitorh--9-v-rail-adc-monitor)
   - 2.7 [WebPortal.h / WebPortal.cpp — Wi-Fi AP and HTTP server](#27-webportalh--webportalcpp--wi-fi-ap-and-http-server)
   - 2.8 [Shelly_ESP32_Logger.ino — Sketch entry point](#28-shelly_esp32_loggerino--sketch-entry-point)
3. [Unit interaction map](#3-unit-interaction-map)
4. [Data flow — measurement to SD](#4-data-flow--measurement-to-sd)
5. [How to make a specific change](#5-how-to-make-a-specific-change)
6. [File dependency map](#6-file-dependency-map)
7. [Known issues and limitations](#7-known-issues-and-limitations)
8. [Open requirements](#8-open-requirements)

---

## 1. System overview

The logger measures and records the power consumption of small kitchen appliances (hand mixers, blenders) in real day-use conditions. All 230 V handling is delegated to a certified Shelly Plug S MTR Gen3 smart plug. The user-built portion operates exclusively on 5 V DC, powered from a 9 V rail through a TSR-1-2450 DC-DC converter.

| | |
|---|---|
| **Hardware** | Shelly Plug S MTR Gen3 (CE-marked, 16 A EU socket), ESP32-WROOM-32 module, DS3231 ZS042 RTC module (I²C), MicroSD card on SPI shield, Certified 230 V → 9 V DC PSU + TSR-1-2450 (9 V → 5 V), 2 × 0.47 F supercapacitors + Schottky diodes (power-loss hold-up circuit) |
| **Firmware split** | Shelly: mJS script (`shelly_push.js`) running on-device. ESP32: Arduino C++ firmware (8 source files) |
| **Network** | ESP32 is the Wi-Fi Access Point (`WIFI_AP` mode, AP only — no STA interface active). Shelly and user phone are both STA clients. DNS catch-all and mDNS disabled in v4. |
| **Data direction** | Shelly → ESP32 (push, every **1.2 s**) · ESP32 → Phone (pull, browser polls `/api/live` every 1 s) · ESP32 → SD (internal write, flush every 10 s) |
| **No internet** | Cloud disabled on Shelly. All traffic stays on `192.168.4.0/24` |

### 1.1 Key architectural decision: push, not poll

In the predecessor PZEM/ESP32 v4 firmware, the ESP32 owned the sensor and polled it synchronously over UART. In this firmware the data flow is reversed: the Shelly pushes measurements to the ESP32 via HTTP POST. This means:

- The ESP32 never initiates a connection to the Shelly (no HTTP client code on the ESP32 side)
- `ShellyClient.h` acts as the adapter: it looks like a sensor to `Logger.h` but is actually a push receiver
- The ESP32 data path (ring buffer, SD flush, web API) is unchanged from the PZEM version — only the data source changes

### 1.2 Push timing design (v4 change)

The Shelly push interval was increased from 1000 ms to **1200 ms** in firmware v4 to eliminate a zero-margin race condition. The mJS timer is non-compensating: each `doPush()` fires 1200 ms after the *previous callback completes*, not after it was scheduled. With the 2 s HTTP timeout, this gives a guaranteed 200 ms gap between the HTTP callback clearing `_pushPending` and the next timer fire. The ESP32 watchdog window of 5 s (`SHELLY_ERROR_THRESHOLD 5 × INTERVAL_SHELLY_POLL_MS 1000 ms`) comfortably accommodates 4 full pushes before triggering.

### 1.3 Loop order (v4 change)

`webPortal.update()` was moved to run **first** in `loop()`, before `logger.pollIfDue()`. Previously the push arrived via `webPortal.update()` but was not read by `pollIfDue()` until the *next* loop iteration, creating a one-sample timestamp lag. With the new order, a push ingested in step 1 is immediately available to `pollIfDue()` in step 2 of the same iteration.

---

## 2. Software units

The firmware consists of eight software units. One runs on the Shelly, seven run on the ESP32.

| Unit | Runs on | Language | Purpose |
|---|---|---|---|
| `shelly_push.js` | Shelly device | mJS script | Measurement source, push transmitter |
| `Config.h` | ESP32 | C++ header | All compile-time constants. Single source of truth |
| `ShellyClient.h` | ESP32 | C++ class | Push receiver, JSON parser, watchdog, startup grace |
| `Logger.h` | ESP32 | C++ class | Ring buffer, SD writer, threshold filter, RTC timestamping, OTA gate |
| `StatusLed.h` | ESP32 | C++ class | LED blink pattern driver |
| `PowerMonitor.h` | ESP32 | C++ class | 9 V rail ADC monitor, mains-loss detection, graceful shutdown trigger |
| `WebPortal.h/.cpp` | ESP32 | C++ class | Wi-Fi AP, HTTP server, all web routes, OTA flash handler |
| `Shelly_ESP32_Logger.ino` | ESP32 | Arduino sketch | Object wiring, `setup()`, `loop()`, `handlePowerLoss()` |

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
- HTTP POST to `http://192.168.4.1/api/shelly_push` every `PUSH_INTERVAL_MS` (default **1200 ms**)
- Guard against request stacking: if previous HTTP call is still pending, skip this tick (`_pushPending` flag)
- Force relay ON at startup (`Switch.Set`) to prevent accidental load disconnection

#### Key constants — only section that should be edited

| Constant | Value | Notes |
|---|---|---|
| `PUSH_INTERVAL_MS` | `1200` | Push cadence in ms. **Was 1000 ms in v1–v3.** Increased to guarantee 200 ms margin between HTTP callback and next timer fire. Must not be decreased below ~1100 ms with a 2 s timeout. |
| `timeout` (in HTTP.Request) | `2` | HTTP timeout in seconds. **Was 1 s in v1–v3.** With `PUSH_INTERVAL_MS = 1200 ms`, the callback clears `_pushPending` and the next tick fires 1200 ms later — guaranteed gap. |
| `ESP32_IP` | `"192.168.4.1"` | ESP32 softAP fixed IP. Never changes in Option B topology |
| `SWITCH_ID` | `0` | Shelly channel index. Plug S always has one channel: 0 |

#### Timing rationale

The mJS `Timer.set(PUSH_INTERVAL_MS, true, doPush, null)` is non-compensating: the next fire is always `PUSH_INTERVAL_MS` ms after the *callback returns*, not after the previous fire. With the previous values (interval=1000, timeout=1):

```
t=0:      doPush() fires, HTTP.Request sent, _pushPending=true
t=~1000:  HTTP timeout fires (if ESP32 slow), callback clears _pushPending
t=~1000:  next Timer fires immediately (0 ms gap) → sees _pushPending=false → ok, but races
```

With current values (interval=1200, timeout=2):

```
t=0:      doPush() fires, HTTP.Request sent, _pushPending=true
t≤2000:   callback fires (success or timeout), _pushPending=false
t=Tcb+1200: next doPush() fires — guaranteed ≥200 ms after callback
```

#### Known limitations

- Push rate is fixed at install-time in JS. Changing the logging rate in the ESP32 Settings UI does **not** slow down the Shelly push — the Shelly always pushes at 1.2 s. Extra pushes are silently discarded by `Logger::pollIfDue()`.
- The `ts` field (Shelly uptime ms) in the JSON payload is received by the ESP32 but currently discarded — the CSV uses the ESP32's own `millis()` and the DS3231 wall-clock time instead.

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
| `PIN_SD_CS/MOSI/CLK/MISO` | `25 / 14 / 27 / 26` | SPI VSPI pins for SD card |
| `PIN_LED` | `32` | Status LED output (active HIGH) |
| `PIN_VSUPPLY` | `35` | ADC1_CH7 — 9 V rail monitor via resistor divider. Input only, no internal pull |
| `PIN_RTC_SDA` | `21` | DS3231 I²C SDA (Wire bus 0 default) |
| `PIN_RTC_SCL` | `22` | DS3231 I²C SCL (Wire bus 0 default) |
| `SHELLY_HOST` | `"192.168.4.2"` | Dead code — ESP32 never contacts Shelly in push architecture. Reserved for future RPC use |
| `SHELLY_PUSH_ENDPOINT` | `"/api/shelly_push"` | Route registered in WebPortal. Must match `PUSH_URL` in `shelly_push.js` |
| `OTA_ENDPOINT` | `"/update"` | OTA upload route. Must match WebPortal route registration |
| `SHELLY_ERROR_THRESHOLD` | `5` | **Was 3 in v1–v3.** Watchdog: silence for 5 × `INTERVAL_SHELLY_POLL_MS` = 5 s triggers `LED_ERROR`. Increased to accommodate 1.2 s push cadence and SD flush blocking |
| `SHELLY_STARTUP_GRACE_MS` | `15000` | **New in v3.** Grace window during which `shellyOk()` returns `true` even with no data. Suppresses false watchdog trips on boot and during file downloads |
| `INTERVAL_SHELLY_POLL_MS` | `1000 ms` | Default logging cadence. Also used as the watchdog multiplier. Minimum meaningful: 1000 ms. Note: Shelly push cadence (1200 ms) is deliberately faster than this is used |
| `INTERVAL_SD_FLUSH_MS` | `10000 ms` | How often Logger flushes the RAM ring buffer to SD |
| `INTERVAL_LED_OK_MS` | `500 ms` | LED toggle period in OK state = 1 Hz blink |
| `INTERVAL_LED_ERR_MS` | `100 ms` | LED toggle period in ERROR state = 5 Hz blink |
| `DEFAULT_POWER_THRESHOLD_W` | `0.0 W` | Log all samples by default. Configurable at runtime via Settings UI |
| `RAM_BUFFER_SIZE` | `64 entries` | Ring buffer capacity. 64 × ~20 bytes ≈ 1.3 KB. At 1 s cadence: 64 s reserve |
| `API_BUFFER_SIZE` | `320 bytes` | Stack-allocated `char[]` for JSON responses. Worst-case `/api/live` ≈ 146 bytes (174 byte margin) |
| `LOG_FILE_PATH` | `"/log.csv"` | SD card file path. Fixed |
| `LOG_FILE_HEADER` | `"datetime,time_ms,..."` | **Updated in v3:** `datetime` (ISO-8601 from DS3231) prepended as first column. Full: `datetime,time_ms,voltage_V,power_W,pf_apparent` |
| `SD_FLUSH_TIMEOUT_MS` | `400` | **New in v4.** If `flushToSD()` blocks longer than this, SD is declared failed and `tryRecoverSD()` is scheduled. Prevents repeated blocking on marginal cards |
| `POWER_MONITOR_ENABLED` | `0` | **New in v3.** Set to `1` when the 9 V supercap + divider circuit is bench-validated. At `0`, `PowerMonitor::update()` and `isPowerLost()` are compile-time no-ops |
| `DIVIDER_R_TOP_OHM` | `180000` | R1 of the GPIO 35 voltage divider |
| `DIVIDER_R_BOTTOM_OHM` | `47000` | R2 of the GPIO 35 voltage divider |
| `POWER_THRESHOLD_LOW_MV` | `7350` | Trigger SD flush + shutdown below this rail voltage (mV) |
| `POWER_THRESHOLD_HIGH_MV` | `7750` | Hysteresis: clear shutdown flag above this voltage (mV) |
| `POWER_STARTUP_GRACE_MS` | `10000` | **Was 3000 ms.** Ignore rail readings for this long after boot while supercaps charge |
| `POWER_CHECK_INTERVAL_MS` | `200` | How often `PowerMonitor::update()` samples the rail |
| `POWER_ADC_SAMPLES` | `16` | Oversampling count per reading. Reduces WiFi-induced ADC noise by ~4× |
| `POWER_ADC_SAMPLE_GAP_US` | `200` | Spacing between oversamples (µs) |
| `POWER_MAJORITY_COUNT` | `3` | Consecutive below-threshold readings required to latch `_powerLost` |
| `POWER_RECOVER_COUNT` | `10` | Consecutive above-threshold readings (~1 s) required to trigger clean reboot after recovery |
| `WIFI_AP_SSID / PASSWORD` | `"PZEM_Logger" / "logger1234"` | AP credentials. Change here and re-provision Shelly |
| `WIFI_AP_HOSTNAME` | `"braun_PZEM"` | mDNS hostname constant (kept in Config.h, but mDNS is disabled in WebPortal v4) |
| `DNS_PORT` | `53` | DNS server port (kept in Config.h, but DNS catch-all is disabled in WebPortal v4) |

---

### 2.3 `ShellyClient.h` — Push receiver, watchdog, and startup grace

| | |
|---|---|
| **Language** | C++ header-only class |
| **Purpose** | Receive, validate, and cache measurements pushed by `shelly_push.js`. Expose them as synchronous getters to `Logger.h`. Run a watchdog that detects Shelly dropout. Provide a startup grace window to suppress false watchdog trips during boot and file downloads. |
| **File** | `ShellyClient.h` |
| **Instantiated in** | `Shelly_ESP32_Logger.ino` (global: `ShellyClient shelly`) |
| **Depends on** | `Config.h`, ArduinoJson library |
| **Used by** | `WebPortal.cpp` (calls `ingest()`, `beginStartupGrace()`), `Logger.h` (calls getters + `shellyOk()`) |

#### Internal data

| Member | Description |
|---|---|
| `ShellyMeasurement _latest` | Cached last measurement: voltage, power, current, pf_apparent, valid flag |
| `uint32_t _lastPushMs` | `millis()` timestamp of last successful `ingest()`. Used by watchdog |
| `uint8_t _errorCount` | Consecutive parse/validation failure counter. Resets on success. Wraps at 255 (see issue #8) |
| `bool _graceActive` | True while startup grace is suppressing watchdog. Cleared by first successful push or grace timeout |
| `uint32_t _graceStartMs` | `millis()` at which `beginStartupGrace()` was called |

#### Public interface

| Method | Description |
|---|---|
| `beginStartupGrace()` | **New in v3.** Sets `_graceActive=true` and resets the grace timer to `millis()`. Called from `setup()` after `webPortal.begin()`, and from `WebPortal::handleDownload()` before `streamFile()`. While grace is active, `shellyOk()` returns `true` regardless of push silence. Grace clears automatically when the first successful push arrives (immediately) or after `SHELLY_STARTUP_GRACE_MS` (15 s), whichever is first. |
| `ingest(const String& body)` | Called by `WebPortal::handleShellyPush()` with raw HTTP POST body. Parses JSON, validates bounds, caches result, clears grace. Returns `true` on success |
| `getVoltage()` | Returns `_latest.voltage` (V). `NAN` until first successful ingest |
| `getPower()` | Returns `_latest.power` (W). `NAN` until first successful ingest |
| `getPfApparent()` | Returns `_latest.pf_apparent`. `NAN` until first successful ingest |
| `hasData()` | Returns `true` once at least one valid push has been received |
| `shellyOk()` | Watchdog check. Returns `true` while grace is active. After grace: returns `false` if no push in `SHELLY_ERROR_THRESHOLD (5) × INTERVAL_SHELLY_POLL_MS (1000 ms)` = **5 s**, or if no data has ever arrived |
| `getErrorCount()` | Returns consecutive error count (diagnostic only) |

#### Startup grace — two use cases

**Boot-order grace:** If the ESP32 reboots while the Shelly is already running, the Shelly's WiFi client needs 3–10 s to re-associate with the freshly-started AP. During this window no pushes arrive, which would trip the 5 s watchdog. `beginStartupGrace()` in `setup()` suppresses the watchdog for up to 15 s.

**Download grace:** `streamFile()` in `handleDownload()` blocks `loop()` for the entire file transfer (5–50+ s for a large log). During this time the Shelly keeps pushing but receives no responses — all pushes time out. Without re-arming the grace, the 5 s watchdog would trip mid-download, trigger LED error and log gap. `beginStartupGrace()` is called immediately before `streamFile()` so the watchdog stays suppressed for up to 15 s after the download completes. The first successful push after the download clears the grace flag immediately.

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

### 2.4 `Logger.h` — Ring buffer, SD writer, threshold gate, RTC, OTA gate

| | |
|---|---|
| **Language** | C++ header-only class |
| **Purpose** | Consume measurements from `ShellyClient`, apply power threshold filter, buffer samples in RAM, flush periodically to SD card with RTC wall-clock timestamps, pause during OTA flash, recover from SD failures |
| **File** | `Logger.h` |
| **Instantiated in** | `Shelly_ESP32_Logger.ino` (global: `Logger logger(shelly, &rtc)`) |
| **Depends on** | `Config.h`, `ShellyClient.h`, `RTClib.h` (Adafruit), Arduino SD library, SPI |
| **Used by** | `Shelly_ESP32_Logger.ino` (calls `pollIfDue`, `flushIfDue`, `flushToSD`), `WebPortal.cpp` (calls getters, `flushToSD`, `setOtaInProgress`, `resetSDFile`, `openLogFileForRead`, `reopenAfterRead`) |

#### Key design: Logger does NOT own the sensor

`ShellyClient` is injected via constructor reference. `Logger` calls `_shelly.getVoltage()` etc. in `pollIfDue()`. The RTC is injected as an optional pointer — `nullptr` means no RTC, and the CSV writes `RTC_NOT_SET` for the `datetime` column instead of a timestamp.

#### Internal state

| Member | Description |
|---|---|
| `Sample _buffer[64]` | RAM ring buffer. `Sample = { uint32_t millis_ts, uint32_t unix_ts, float voltage_V, float power_W, float pf }`. `unix_ts` is UTC epoch from DS3231 (0 = RTC absent/not set) |
| `size_t _bufferCount` | Number of valid entries currently in the buffer. 0–64 |
| `uint32_t _droppedSamples` | Cumulative count of samples dropped due to buffer full + SD unavailable |
| `uint32_t _lastPollMs` | Timestamp of last `pollIfDue()` execution |
| `uint32_t _lastFlushMs` | Timestamp of last `flushIfDue()` execution |
| `uint32_t _lastSdRetryMs` | Timestamp of last SD re-init attempt (retry gate: 30 s cooldown) |
| `float _last{Voltage,Power,Pf}` | Live display cache. Updated every `pollIfDue()` regardless of threshold |
| `bool _sdOk` | SD card health flag |
| `bool _otaInProgress` | **New in v4.** True while WebPortal is writing OTA flash. Gates out `pollIfDue` and `flushIfDue` |
| `uint32_t _pollIntervalMs` | Runtime-adjustable logging cadence. Default `INTERVAL_SHELLY_POLL_MS` (1000 ms) |
| `float _powerThresholdW` | Runtime-adjustable power threshold. Default 0.0 W |
| `RTC_DS3231* _rtc` | **New in v3.** Optional injected RTC pointer. `nullptr` = no RTC |

#### Public interface

| Method | Description |
|---|---|
| `begin()` | Initialise SPI + SD card. Returns `true` if SD ready |
| `pollIfDue()` | Called every `loop()`. If `_pollIntervalMs` elapsed and OTA not active: read ShellyClient cache, update live display, optionally push sample to ring buffer. Reads DS3231 for `unix_ts` at each sample |
| `flushIfDue()` | Called every `loop()`. If `INTERVAL_SD_FLUSH_MS` elapsed and OTA not active: flush ring buffer to SD, or attempt SD recovery if `sdOk=false` |
| `flushToSD()` | Public: write all buffered samples to `/log.csv` (`FILE_APPEND`), clear buffer. Each row: `datetime,millis_ts,voltage,power,pf`. **New in v4:** times the `_logFile.flush()` call; if > `SD_FLUSH_TIMEOUT_MS` (400 ms), sets `_sdOk=false` to prevent repeated blocking |
| `setOtaInProgress(bool)` | **New in v4.** Called by WebPortal OTA handlers. On `true`: immediately flushes SD buffer and pauses logging. On `false`: resumes (called on OTA abort) |
| `isOtaInProgress()` | Returns `_otaInProgress`. Read by WebPortal for `/api/live` response |
| `resetSDFile()` | Delete `/log.csv` and write fresh header. Resets `_bufferCount` to 0 **without flushing first** (see known issue #1) |
| `openLogFileForRead()` | Flush and close write handle, open for reading. Returns `File` handle. Caller must close and then call `reopenAfterRead()` |
| `reopenAfterRead()` | **New in v4.** Reopens `/log.csv` in `FILE_APPEND` mode after download completes |
| `setPollInterval(ms)` | Set `_pollIntervalMs` at runtime. Rejects values < 1000 ms |
| `setPowerThreshold(W)` | Set `_powerThresholdW` at runtime |
| `shellyOk() / sdOk() / ok()` | Status flags. `ok() = shellyOk() && sdOk()`. Drives LED |
| `getLastVoltage/Power/Pf()` | Live display values for `/api/live`. May be `NAN` if Shelly not yet connected |
| `getBufferCount() / getDroppedSamples()` | Diagnostics shown in web UI |

#### SD flush timeout guard (new in v4)

`flushToSD()` times the entire `_logFile.flush()` call using `millis()`. If the call takes longer than `SD_FLUSH_TIMEOUT_MS` (400 ms), the SD is declared failed (`_sdOk=false`) and `_bufferCount` is cleared. The data already written to the file handle's buffer before the slow `flush()` is not lost — it may have partially committed to the card. The next `flushIfDue()` call will invoke `tryRecoverSD()` (30 s rate-limited) instead of calling `flushToSD()` again, preventing a cascade of blocking calls on a marginal card.

#### Ring buffer overflow behaviour (`pushSample`)

- Normal: `_bufferCount < 64` → append and increment
- Buffer full + SD available: emergency `flushToSD()`, then append
- Buffer full + SD failed: drop oldest entry (FIFO), write newest at tail, increment `_droppedSamples`
- Drop is logged to Serial on first occurrence and every 100 drops thereafter

#### SD recovery (`tryRecoverSD`)

Rate-limited to one attempt every 30 s. Calls `SD.end()`, `delay(50)` *(known issue #6 — blocks loop for 50 ms)*, then `SD.begin()`. On success, recreates log header if file is missing, then reopens for append.

---

### 2.5 `StatusLed.h` — LED blink driver

| | |
|---|---|
| **Language** | C++ header-only class |
| **Purpose** | Non-blocking LED blink at two rates: 1 Hz (OK) or 5 Hz (ERROR) |
| **File** | `StatusLed.h` |
| **Unchanged from** | PZEM/ESP32 v4 in logic. Only change: `logger.ok()` now evaluates `shellyOk() && sdOk()` (in Logger.h, not here) |
| **Depends on** | `Config.h` |
| **Used by** | `Shelly_ESP32_Logger.ino` |

#### States

| State | Behaviour | Trigger condition |
|---|---|---|
| `LED_OK` | 1 Hz blink (500 ms toggle) | `logger.ok() = shellyOk() && sdOk()` — both true |
| `LED_ERROR` | 5 Hz blink (100 ms toggle) | SD error OR Shelly watchdog timeout (5 s silence after grace expires) OR OTA upload in progress |

#### Public interface

| Method | Description |
|---|---|
| `begin()` | Configure `PIN_LED` as OUTPUT, set initial state `LED_OK`, reset timer |
| `setState(s)` | Change state. Only resets blink phase if state actually changes (idempotent) |
| `setOk(bool ok)` | Convenience wrapper: `setState(ok ? LED_OK : LED_ERROR)` |
| `update()` | Must be called every `loop()`. Checks elapsed time; if ≥ interval, toggles LED pin |

The LED is the primary offline health indicator. **1 Hz green blink = Shelly pushing data AND SD writing correctly. 5 Hz red blink = at least one has failed or OTA is in progress.**

---

### 2.6 `PowerMonitor.h` — 9 V rail ADC monitor

> **New unit in firmware v3.** Replaces the bare `pinMode(PIN_VSUPPLY, INPUT)` placeholder from v1–v2.

| | |
|---|---|
| **Language** | C++ header-only class |
| **Purpose** | Detect mains power loss by monitoring the 9 V supply rail via ADC on GPIO 35. When a sustained drop below the threshold is detected, set `_powerLost` latch. The sketch (`handlePowerLoss()`) responds by flushing SD and shutting down. |
| **File** | `PowerMonitor.h` |
| **Instantiated in** | `Shelly_ESP32_Logger.ino` (global: `PowerMonitor powerMonitor`) |
| **Depends on** | `Config.h`, Arduino ADC |
| **Used by** | `Shelly_ESP32_Logger.ino` (calls `begin()`, `update()`, `isPowerLost()`, `readRailMilliVolts()` in terminal idle loop) |

#### Design split

`PowerMonitor` is a **detector only**. It does not touch the SD card, Wi-Fi, or LED — all orchestration lives in `handlePowerLoss()` in the `.ino`. This mirrors the Logger/ShellyClient/WebPortal single-responsibility split and keeps the threshold logic independently testable.

#### Enable flag

`POWER_MONITOR_ENABLED` in `Config.h` controls whether the unit does real work:
- `= 0` (default): `update()` and `isPowerLost()` are compile-time no-ops. Use this when the 9 V + supercap hardware circuit is not yet populated or validated. (Root cause of an earlier bug: GPIO 35 floated to 0 V without the divider resistors, which read as 0 mV — always below the 7350 mV threshold — causing `handlePowerLoss()` to fire ~600 ms after boot and turn off WiFi before the AP was visible.)
- `= 1`: full ADC sampling, threshold comparison, and shutdown signalling active.

#### Internal state

| Member | Description |
|---|---|
| `bool _graceActive` | True during the post-boot startup window. Prevents false trips while supercaps charge |
| `uint32_t _startupMs` | `millis()` at `begin()`. Grace expires after `POWER_STARTUP_GRACE_MS` (10 s) |
| `uint32_t _lastCheckMs` | Timestamp of last rail sample |
| `uint8_t _belowCount` | Consecutive below-threshold readings. Must reach `POWER_MAJORITY_COUNT` (3) to latch |
| `bool _powerLost` | Latched when sustained under-voltage confirmed. Only cleared by reboot |

#### Public interface

| Method | Description |
|---|---|
| `begin()` | Configure ADC (12-bit, 11 dB attenuation on GPIO 35). Start startup grace clock. Print threshold/grace values to Serial |
| `update()` | Call every `loop()`. No-op during grace window or between `POWER_CHECK_INTERVAL_MS` (200 ms) intervals. Reads rail, applies majority voting. No-op if `POWER_MONITOR_ENABLED=0` |
| `isPowerLost()` | Returns `_powerLost` latch. Always `false` if `POWER_MONITOR_ENABLED=0` |
| `readRailMilliVolts()` | Public: 16× oversampled read of GPIO 35, converted back to 9 V rail mV via divider formula. Also called from the terminal idle loop in `handlePowerLoss()` to detect mains recovery |

#### ADC design decisions

The 9 V rail is divided to the ESP32 ADC range via R1 (180 kΩ) + R2 (47 kΩ) giving a GPIO voltage of `V_rail × 0.207`. At nominal 9 V, GPIO 35 reads ~1.86 V — within the ADC's most linear region (0.8–2.5 V). The 7.35 V trigger threshold corresponds to ~1.52 V at the pin — safely above the TSR-1-2450's 6.5 V dropout voltage where the ADC reference would begin to sag.

16× oversampling with `delayMicroseconds(200)` between samples reduces WiFi-induced ADC noise by approximately 4× (1/√16). Majority voting (3 consecutive reads) prevents a single supply transient or SD write spike from triggering shutdown.

---

### 2.7 `WebPortal.h` / `WebPortal.cpp` — Wi-Fi AP and HTTP server

| | |
|---|---|
| **Language** | C++ class (declaration in `.h`, implementation in `.cpp`) |
| **Purpose** | Create and manage the Wi-Fi Access Point. Register and serve all HTTP routes. Bridge the Shelly push endpoint to `ShellyClient`. Handle OTA firmware uploads. Serve all web UI pages. |
| **Files** | `WebPortal.h`, `WebPortal.cpp` |
| **Instantiated in** | `Shelly_ESP32_Logger.ino` (global: `WebPortal webPortal(logger, shelly)`) |
| **Depends on** | `Config.h`, `Logger.h`, `ShellyClient.h`, WiFi, WebServer, DNSServer, ESPmDNS, Update.h |
| **Used by** | `Shelly_ESP32_Logger.ino` (calls `begin()`, `update()`) |

#### Wi-Fi configuration (v4 changes)

| Parameter | Value | Notes |
|---|---|---|
| **Mode** | `WIFI_AP` | **Changed from `WIFI_AP_STA` in v4.** AP only — no STA interface active. `WIFI_AP_STA` caused background channel scanning (~400 ms deaf periods) when STA had no association, causing random Shelly push timeouts. |
| **Power saving** | `WiFi.setSleep(false)` | **New in v4.** Disables radio sleep between beacon intervals (~100 ms). Default power-save mode dropped incoming packets during sleep. |
| **AP SSID** | `PZEM_Logger` | Shelly and phone join this network |
| **AP Password** | `logger1234` | ≥ 8 chars |
| **AP Gateway** | `192.168.4.1` | Fixed — always this for ESP32 softAP |
| **DHCP** | Managed by ESP32 AP stack | Shelly gets `.2` (first lease), phone gets `.3+` |
| **DNS catch-all** | **Disabled** | **Disabled in v4.** The catch-all `DNSServer` redirect triggered iOS and Windows "no internet" detection, causing these OSes to aggressively drop the AP connection in the background. Direct IP access required: `http://192.168.4.1` |
| **mDNS** | **Disabled** | **Disabled in v4.** `ESPmDNS` background UDP processing caused 100–500 ms stalls in `loop()`, blocking HTTP responses. `http://braun_PZEM.local` does not resolve. |

#### HTTP routes

| Route | Handler | Response | Description |
|---|---|---|---|
| `GET /` | `handleRoot()` | PROGMEM HTML | Live dashboard: power, voltage, pf_apparent, Shelly/SD status, OTA banner |
| `GET /api/live` | `handleApiLive()` | JSON (stack buf 320 B) | `{ power, voltage, pf, buffer, dropped, uptime, shelly_ok, sd_ok, ota_active }`. **`ota_active` new in v4** |
| `GET /api/settings` | `handleApiSettings()` | JSON | `{ poll_ms, power_threshold }` |
| `POST /api/settings` | `handleApiSettingsSave()` | JSON | Update poll_ms and/or power_threshold. Whitelisted values only |
| `POST /api/shelly_push` | `handleShellyPush()` | JSON | Receives Shelly measurement push. Calls `ShellyClient::ingest()` |
| `GET /update` | `handleOtaForm()` | PROGMEM HTML | **New in v4.** OTA upload form with drag-and-drop and progress bar |
| `POST /update` | `handleOtaUpload()` + `handleOtaChunk()` | text/plain | **New in v4.** Streams `.bin` to `Update.h` flash writer. Pre-flushes SD via `setOtaInProgress(true)`. Reboots on success. |
| `GET /download` | `handleDownload()` | CSV stream | Flush RAM buffer, call `beginStartupGrace()`, stream `/log.csv` as attachment, then `reopenAfterRead()` |
| `POST /reset` | `handleReset()` | text/plain | Delete and recreate `/log.csv` |
| `GET /settings` | `handleSettings()` | PROGMEM HTML | Rate selector + threshold selector UI |
| `GET /liveplot` | `handleLivePlot()` | PROGMEM HTML | **New in v4.** Canvas oscilloscope, auto-scaled Y, 60-sample rolling window |
| `GET /histogram` | `handleHistogram()` | PROGMEM HTML | **New in v4.** Live power distribution histogram, 20 bins |
| `GET /readme` | `handleReadme()` | PROGMEM HTML | Built-in user manual |
| `GET /generate_204` + 7 more | `handleCaptivePortal()` | 302 redirect | Android/iOS/Windows captive portal probes → redirect to `192.168.4.1` |
| `*` (not found) | `handleNotFound()` | 302 redirect | Unknown URLs → captive portal redirect |

#### OTA upload flow (`POST /update`)

The `POST /update` route uses two registered callbacks: a body completion handler (`handleOtaUpload`) and a per-chunk upload callback (`handleOtaChunk`):

1. `UPLOAD_FILE_START`: call `_logger.setOtaInProgress(true)` — flushes SD buffer and pauses logging. Call `Update.begin(UPDATE_SIZE_UNKNOWN)` to start flash writer.
2. `UPLOAD_FILE_WRITE`: call `Update.write(buf, size)` for each chunk.
3. `UPLOAD_FILE_END`: call `Update.end(true)` — verifies MD5 if present, marks new partition bootable.
4. `UPLOAD_FILE_ABORTED`: call `Update.abort()` and `setOtaInProgress(false)` — logging resumes.
5. After last chunk, `handleOtaUpload()` checks `Update.hasError()`: sends 200 + `ESP.restart()` on success, 500 on failure.

During OTA, the Shelly watchdog will trip (expected — OTA takes 5–15 s). The LED blinks 5 Hz. After reboot, logging resumes automatically.

#### `update()` — per-loop HTTP servicing

```
webPortal.update():
  - Loop-block detection: warn to Serial if > 100 ms since last call
  - _server.handleClient() × 3   // serve up to 3 requests per loop iteration
```

Three `handleClient()` calls per loop reduce response latency compared to one, at the cost of potentially serving 3 back-to-back requests (up to ~150 ms total block) before returning to `pollIfDue()`. With the 5 s watchdog window this is safe.

#### Settings whitelist (`handleApiSettingsSave`)

| Parameter | Allowed values |
|---|---|
| `poll_ms` | `1000, 2000, 5000, 10000, 30000` ms |
| `power_threshold` | `0, 1, 2, 5, 10, 20, 50` W (0 = log everything) |
| Invalid value | HTTP 400 with JSON error body |

---

### 2.8 `Shelly_ESP32_Logger.ino` — Sketch entry point

| | |
|---|---|
| **Language** | Arduino C++ sketch |
| **Purpose** | Declare all global objects in correct dependency order. Wire them together. Implement `setup()`, `loop()`, and `handlePowerLoss()` |
| **File** | `Shelly_ESP32_Logger.ino` |
| **Depends on** | All other headers, plus `<WiFi.h>`, `<Wire.h>`, `<RTClib.h>` |

#### Object instantiation order

Order matters because `Logger` takes references to both `ShellyClient` and `RTC_DS3231`, and `WebPortal` takes references to both `Logger` and `ShellyClient`:

```cpp
ShellyClient shelly;                       // no dependencies
RTC_DS3231   rtc;                          // no dependencies
Logger       logger(shelly, &rtc);         // needs shelly + rtc
WebPortal    webPortal(logger, shelly);    // needs logger + shelly
StatusLed    statusLed;                    // no dependencies
PowerMonitor powerMonitor;                 // no dependencies
```

#### `setup()` sequence

| Step | What it does |
|---|---|
| `Serial.begin(115200)` | Debug output |
| `powerMonitor.begin()` | Configure ADC on GPIO 35, start 10 s startup grace for power monitor |
| `Wire.begin(PIN_RTC_SDA, PIN_RTC_SCL)` | Start I²C bus 0 |
| `rtc.begin(&Wire)` | Init DS3231. If missing or battery dead: log warning, continue (CSV shows `RTC_NOT_SET`) |
| `statusLed.begin()` | LED output, set to `LED_ERROR` (red) until init complete |
| `logger.begin()` | SD SPI init. Sets `_sdOk`. Prints OK/FEHLER to Serial |
| `ensureLogHeader()` | If `/log.csv` does not exist on SD, create with header row |
| `webPortal.begin()` | Start AP (`WIFI_AP`, sleep disabled), register all routes, start HTTP server. DNS and mDNS NOT started. |
| `shelly.beginStartupGrace()` | Arm 15 s watchdog grace — suppresses false Shelly FEHLER while Shelly re-associates with the freshly-started AP |
| `statusLed.setOk(false)` | Stay red — Shelly not yet connected |

#### `loop()` sequence — called continuously

| Step | Call | Description |
|---|---|---|
| **0** | `powerMonitor.update()` | Sample 9 V rail every 200 ms (no-op if `POWER_MONITOR_ENABLED=0`). If latch set: call `handlePowerLoss()` (never returns) |
| **1** | `webPortal.update()` | Serve HTTP requests (**runs FIRST** — changed from v1–v3 where it ran third). Shelly push arrives here → `ShellyClient::ingest()` updates cache |
| **2** | `logger.pollIfDue()` | Every `_pollIntervalMs` (1000 ms): read freshly-ingested ShellyClient cache, update live display, conditionally push sample to ring buffer |
| **3** | `logger.flushIfDue()` | Every 10 s: write ring buffer to SD. Calls `tryRecoverSD()` if `_sdOk=false`. No-op during OTA. |
| **4** | `statusLed.setOk(logger.ok())` | Update LED state: `ok() = shellyOk() && sdOk()`. Idempotent |
| **5** | `statusLed.update()` | Toggle LED pin if blink interval has elapsed. ~0 ms |

**Loop order rationale (v4 change):** Moving `webPortal.update()` before `pollIfDue()` ensures that a Shelly push arriving in this loop iteration is ingested into the `ShellyClient` cache *before* `pollIfDue()` reads it, eliminating the one-sample timestamp lag that existed when the order was poll → flush → update.

**Loop timing:** typically 1–5 ms. When `webPortal.update()` processes a request: 5–20 ms for small API responses; up to 80 ms for a full HTML page send. During SD flush: 5–100 ms (healthy card) up to 400 ms (marginal card, triggers timeout guard). During SD recovery: +50 ms (`delay(50)`, known issue #6). During OTA upload: effectively unlimited (entire binary streams inside `handleClient()`). No RTOS tasks or interrupts are used.

#### `handlePowerLoss()` — terminal shutdown

Called from `loop()` when `powerMonitor.isPowerLost()` latches. Never returns under normal operation:

1. **Flush SD:** `logger.flushToSD()` — best effort, typically < 100 ms
2. **Shed load:** `WiFi.softAPdisconnect(true)` + `WiFi.mode(WIFI_OFF)` + LED off. Saves 80–120 mA, extending supercap hold-up by several seconds.
3. **Terminal idle:** poll `powerMonitor.readRailMilliVolts()` every 100 ms. If rail recovers above `POWER_THRESHOLD_HIGH_MV` for `POWER_RECOVER_COUNT` (10) consecutive reads (~1 s): `ESP.restart()`. Otherwise wait for hardware brownout reset.

---

## 3. Unit interaction map

Every runtime interaction between units.

| From | To | Mechanism | Payload |
|---|---|---|---|
| `shelly_push.js` | `WebPortal` (ESP32) | HTTP POST `/api/shelly_push` every 1.2 s | JSON: `{ ts, v, p, i, pf }` |
| `WebPortal` | `ShellyClient` | `handleShellyPush()` → `ingest(body)` | Raw HTTP body string |
| `WebPortal` | `ShellyClient` | `handleDownload()` → `beginStartupGrace()` | (arms 15 s grace before streamFile) |
| `ShellyClient` | `ShellyClient` | `ingest()` validates, caches, updates `_lastPushMs`, clears grace | Internal |
| `Logger` (loop) | `ShellyClient` | `pollIfDue()` → `getVoltage/Power/PfApparent()`, `hasData()`, `shellyOk()` | float values |
| `Logger` | `RTC_DS3231` | `pollIfDue()` → `rtc->now().unixtime()` | UTC epoch (uint32_t) |
| `Logger` | SD card | `flushToSD()` appends CSV rows via `SD.open(FILE_APPEND)` | CSV bytes |
| `WebPortal` | `Logger` | `handleApiLive()` → `getLastPower/Voltage/Pf()`, `shellyOk()`, `sdOk()`, `isOtaInProgress()` | float / bool |
| `WebPortal` | `Logger` | `handleDownload()` → `flushToSD()`, `openLogFileForRead()`, `reopenAfterRead()` | File handle |
| `WebPortal` | `Logger` | `handleReset()` → `resetSDFile()` | bool result |
| `WebPortal` | `Logger` | `handleOtaChunk()` / `handleOtaUpload()` → `setOtaInProgress(true/false)` | bool |
| `WebPortal` | `Logger` | `handleApiSettingsSave()` → `setPollInterval()`, `setPowerThreshold()` | uint32 / float |
| `PowerMonitor` | `.ino` | `isPowerLost()` → triggers `handlePowerLoss()` | bool latch |
| `.ino` | `PowerMonitor` | `powerMonitor.update()` every loop | void |
| `.ino` | `PowerMonitor` | `powerMonitor.readRailMilliVolts()` in terminal idle | uint32_t mV |
| Browser | `WebPortal` | `GET /api/live` every 1 s | HTTP |
| Browser | `WebPortal` | `GET /api/settings`, `POST /api/settings` | HTTP |
| Browser | `WebPortal` | `GET /download`, `POST /reset` | HTTP |
| Browser | `WebPortal` | `GET /update` (form), `POST /update` (binary) | HTTP / multipart |
| Browser | `WebPortal` | `GET /liveplot`, `GET /histogram` | HTTP |
| `.ino loop()` | `StatusLed` | `setOk(logger.ok())` every iteration | bool |
| `.ino loop()` | `Logger` | `pollIfDue()`, `flushIfDue()` every iteration | void |
| `.ino loop()` | `WebPortal` | `update()` every iteration (runs first) | void |

---

## 4. Data flow — measurement to SD

A single measurement traced from the Shelly meter chip to `/log.csv`.

| Step | Description |
|---|---|
| **1 — Shelly meter chip** | Measures V, I, P at hardware level (~1 Hz update rate). Stored in Switch component 0 internal state |
| **2 — `shelly_push.js`** | Reads component 0 via `Shelly.getComponentStatus()`. Derives `pf_apparent`. Calls `buildPayload()` → JSON string. Sets `_pushPending=true`. |
| **3 — HTTP POST** | Shelly sends `POST http://192.168.4.1/api/shelly_push` every 1200 ms. Body: `{ ts, v, p, i, pf }`. HTTP timeout: 2 s. |
| **4 — `WebPortal::handleShellyPush()`** | Reads body via `_server.arg("plain")`. Calls `ShellyClient::ingest(body)`. Returns `200 OK` or `400`. Runs inside `webPortal.update()` which is the **first** call in `loop()`. |
| **5 — `ShellyClient::ingest()`** | `StaticJsonDocument<192>` parses body on stack. Validates bounds. Writes to `_latest` struct. Updates `_lastPushMs = millis()`. Clears `_graceActive`. |
| **6 — `Logger::pollIfDue()`** | Fires every 1000 ms (default). Reads `_shelly.getVoltage/Power/PfApparent()`. Reads `_rtc->now().unixtime()` from DS3231. Updates live display cache. If P ≥ threshold: calls `pushSample()`. |
| **7 — `Logger::pushSample()`** | Appends `Sample { millis_ts, unix_ts, voltage_V, power_W, pf }` to `_buffer[_bufferCount++]` |
| **8 — `Logger::flushIfDue()`** | Fires every 10 s. Calls `flushToSD()`. |
| **9 — `Logger::flushToSD()`** | Opens `/log.csv` in `FILE_APPEND`. For each buffered sample: if `unix_ts > 0`, formats ISO-8601 datetime from DS3231 epoch; else writes `"RTC_NOT_SET"`. Writes CSV line: `datetime,millis_ts,voltage,power,pf`. Calls `_logFile.flush()` (timed — if > 400 ms, declares SD failed). Resets `_bufferCount=0`. |
| **10 — `/log.csv` on SD** | Permanent record. Downloaded via `GET /download`. Format: `datetime,time_ms,voltage_V,power_W,pf_apparent` |

### Timestamps

Two timestamp columns exist in the CSV:

| Column | Source | Notes |
|---|---|---|
| `datetime` | DS3231 RTC `unix_ts` at `pollIfDue()` time | ISO-8601 wall-clock time (`2025-06-01T14:23:01`). `RTC_NOT_SET` if RTC battery dead or not calibrated. Use this for absolute time analysis. |
| `time_ms` | `millis()` at `pollIfDue()` time | ESP32 uptime in ms. Monotonic within a power cycle. Resets to 0 on reboot. Use this to detect gaps and measure inter-sample intervals precisely. |

The Shelly's `ts` field (Shelly uptime ms in the JSON push) is received but not stored. At 1–20 ms push transit time the difference is negligible for kitchen appliance power analysis.

---

## 5. How to make a specific change

| Change | Files and actions |
|---|---|
| **Change a hardware pin** | `Config.h` only |
| **Change the Shelly push interval** | `shelly_push.js`: change `PUSH_INTERVAL_MS`. Keep `timeout` < `PUSH_INTERVAL_MS / 1000` and maintain ≥ 200 ms margin. No need to change `INTERVAL_SHELLY_POLL_MS` in `Config.h` — the ESP32 logging cadence is independent of push cadence |
| **Change default logging rate or SD flush rate** | `Config.h` only: `INTERVAL_SHELLY_POLL_MS` or `INTERVAL_SD_FLUSH_MS` |
| **Change Wi-Fi SSID or password** | `Config.h`: `WIFI_AP_SSID` / `WIFI_AP_PASSWORD`. Also re-provision Shelly Wi-Fi settings to match |
| **Enable power-loss monitoring** | `Config.h`: set `POWER_MONITOR_ENABLED = 1`. Requires 9 V + supercap circuit to be physically present and validated |
| **Add a new allowed poll rate in Settings UI** | `WebPortal.cpp`: add to `allowed_ms[]` in `handleApiSettingsSave()`. Add button in `PAGE_SETTINGS` HTML (`data-ms` attribute). Add to `msToLabel()` JS map |
| **Add a new power threshold option** | `WebPortal.cpp`: add to `allowed_w[]` in `handleApiSettingsSave()`. Add button in `PAGE_SETTINGS` HTML (`data-w` attribute) |
| **Add a new CSV column** | `Config.h`: update `LOG_FILE_HEADER`. `Logger.h`: add field to `Sample` struct, update `_logFile.printf()` in `flushToSD()`, populate new field in `pollIfDue()` |
| **Add a new web page** | `WebPortal.cpp`: add `PAGE_XXX` PROGMEM string, add route in `begin()`, add handler method. `WebPortal.h`: declare handler method |
| **Persist settings across reboots (NVS)** | `Logger.h`: add `#include <Preferences.h>`, add `Preferences _prefs` member, load stored values in `begin()`, call `_prefs.putUInt/putFloat` in setters |
| **Add IP check on `/api/shelly_push`** | `WebPortal.cpp`: in `handleShellyPush()`, add `_server.client().remoteIP() == IPAddress(192,168,4,2)` check before `ingest()` |
| **Fix stale live display at slow poll rates (issue #3)** | `ShellyClient.h`: update `_lastVoltage/Power/Pf` in `ingest()`. `Logger.h`: read from `ShellyClient` getters in `/api/live` handler instead of the `pollIfDue()`-gated cache |
| **Implement button reset (Req 25/26)** | `Shelly_ESP32_Logger.ino`: add `digitalRead(PIN_LED=33)` in `loop()` (note: GPIO 33 reserved, not yet configured). On LOW with debounce: call `logger.resetSDFile()` |
| **Fix `resetSDFile()` data loss (issue #1)** | `Logger.h`: add `flushToSD()` call at the top of `resetSDFile()` before `_bufferCount=0` |
| **Remove 50 ms block in SD recovery (issue #6)** | `Logger.h`: delete `delay(50)` in `tryRecoverSD()` |
| **Re-enable mDNS** | `WebPortal.cpp`: uncomment `MDNS.begin(WIFI_AP_HOSTNAME)` — but be aware this caused 100–500 ms `loop()` stalls in testing. Only re-enable if the loop stall issue has been resolved |
| **Add LED 3rd state (Req 16b)** | `StatusLed.h`: add `LED_OK_WIFI` state with 2000 ms interval. `Shelly_ESP32_Logger.ino`: check `WiFi.softAPgetStationNum() > 0` in `loop()` and call `statusLed.setState(LED_OK_WIFI)` when client connected and system healthy |

---

## 6. File dependency map

An arrow means "requires at compile time".

| File | Depends on |
|---|---|
| `shelly_push.js` | No dependencies (standalone mJS script on Shelly) |
| `Config.h` | No dependencies — included by all others |
| `ShellyClient.h` | `Config.h`, ArduinoJson |
| `StatusLed.h` | `Config.h` |
| `PowerMonitor.h` | `Config.h`, Arduino ADC |
| `Logger.h` | `Config.h`, `ShellyClient.h`, RTClib (Adafruit), Arduino SD, SPI |
| `WebPortal.h` | `Config.h`, `Logger.h`, `ShellyClient.h`, WiFi, WebServer, DNSServer, ESPmDNS, Update.h |
| `WebPortal.cpp` | `WebPortal.h` (and transitively all above) |
| `Shelly_ESP32_Logger.ino` | `Config.h`, `StatusLed.h`, `ShellyClient.h`, `Logger.h`, `WebPortal.h`, `PowerMonitor.h`, `<WiFi.h>`, `<Wire.h>`, `<RTClib.h>` |

### External library dependencies (Arduino Library Manager)

| Library | Version | Used in |
|---|---|---|
| ArduinoJson by Benoit Blanchon | ≥ v6.x | JSON parsing in `ShellyClient::ingest()`. `StaticJsonDocument<192>` avoids heap allocation |
| RTClib by Adafruit | any | DS3231 driver in `Logger.h` and `.ino` |
| SD (built-in ESP32 core) | any | File I/O in `Logger.h` |
| WiFi (built-in) | any | softAP in `WebPortal.cpp` |
| WebServer (built-in) | any | HTTP server in `WebPortal.cpp` |
| Update.h (built-in ESP32) | any | OTA flash writer in `WebPortal.cpp` |
| DNSServer (built-in) | any | Included but DNS catch-all disabled in v4 |
| ESPmDNS (built-in) | any | Included but mDNS disabled in v4 |
| Wire (built-in) | any | I²C bus for DS3231 in `.ino` |

---

## 7. Known issues and limitations

| # | Severity | Issue | Description | Fix |
|---|---|---|---|---|
| **#1** | 🔴 | `resetSDFile()` discards RAM buffer | `Logger.h`: `resetSDFile()` sets `_bufferCount=0` without flushing first. Up to 10 s of samples are lost silently on reset | Add `flushToSD()` before `_bufferCount=0`. One line |
| **#2** | 🟡 | Push/log cadence decoupled | Shelly always pushes at 1.2 s. At 30 s logging rate, most pushes are discarded. "Sampling Rate" label in UI is misleading — it is actually "Logging Rate" | Rename UI label to "Logging Rate" |
| **#3** | 🟡 | Live display stale at slow poll rates | `getLastVoltage/Power/Pf` only update in `pollIfDue()`. At 30 s logging rate, web UI numbers freeze for up to 30 s after Shelly reconnects | Update live cache in `ShellyClient::ingest()` or separate fast-poll path |
| **#4** | ✅ | ~~Watchdog too tight under ESP32 load~~ | **Fixed in v4.** `SHELLY_ERROR_THRESHOLD` raised from 3 to 5 (3 s → 5 s window). Push cadence changed to 1.2 s with 200 ms margin. | Done |
| **#5** | 🟡 | `handleDownload` ignores `flushToSD` return value | If pre-download flush fails, download proceeds with incomplete data and no warning | Check return value; log warning to Serial if flush failed and buffer non-empty |
| **#6** | 🟡 | `delay(50)` in `tryRecoverSD()` blocks `loop()` | 50 ms blocking delay during SD recovery (every 30 s when SD is failed) stalls HTTP serving | Remove `delay(50)`. `SD.end()` + `SD.begin()` do not require it |
| **#7** | 🟠 | `SHELLY_HOST` is dead code | `Config.h` defines `SHELLY_HOST = "192.168.4.2"` but no code references it | Remove define or add clarifying comment |
| **#8** | 🟠 | `_errorCount` wraps at 255 | `ShellyClient._errorCount` is `uint8_t`. After 255 consecutive errors it silently resets to 0 | Change to `uint16_t` |
| **#9** | 🟠 | No IP check on `/api/shelly_push` | Any device on `PZEM_Logger` AP can POST fake data and corrupt the log | Add `remoteIP() == 192.168.4.2` check in `handleShellyPush()` |
| **#10** | 🟠 | Settings not persisted across reboots | `poll_ms` and `power_threshold` revert to defaults on power cycle | Use ESP32 `Preferences` (NVS) in `Logger.h` setters and `begin()` |
| **#11** | 🟡 | mDNS disabled — `braun_PZEM.local` does not resolve | ESPmDNS caused 100–500 ms `loop()` stalls. Disabled in v4. Housing label still references this URL. | Re-evaluate lightweight mDNS alternative; update housing label to remove mDNS reference |
| **#12** | 🟡 | Stale text in web UI | `/readme` page says "Shelly silent for >3 s" (should be 5 s) and "pushes every 1 s" (should be 1.2 s). Home page `PAGE_INDEX` inline badge text not updated. | Update string literals in `WebPortal.cpp` PROGMEM pages |

---

## 8. Open requirements

| Requirement | Status | Feature | Implementation path |
|---|---|---|---|
| **Req 13** | ⚠ Partial | Power-loss SD flush | Hardware circuit present. `PowerMonitor.h` written and complete. **Action:** set `POWER_MONITOR_ENABLED = 1` in `Config.h` after bench-validating the 9 V + supercap circuit |
| **Req 16b** | ❌ | LED 0.5 Hz blink when client connected | `StatusLed.h`: add `LED_OK_WIFI` state at 2000 ms interval. `.ino loop()`: check `WiFi.softAPgetStationNum() > 0` |
| **Req 25/26** | ❌ | GPIO 33 manual reset button | `Shelly_ESP32_Logger.ino`: configure GPIO 33 `INPUT_PULLUP`, add `digitalRead()` in `loop()` with debounce. On LOW: call `logger.resetSDFile()` |
| **Req 28b** | ❌ | `braun_PZEM.local` mDNS hostname | mDNS disabled in v4 due to loop stalls. Housing label references this — update label. Re-evaluate lightweight mDNS implementation. |
| **Req 29 (OTA)** | ✅ | OTA firmware updates | Fulfilled in v4. `GET /update` form + `POST /update` binary upload with progress bar and auto-reboot. |
| **U 9 (RTC)** | ✅ | Date/time logging | Fulfilled in v3. DS3231 on I²C, `datetime` column in CSV. |
| **Settings NVS** | Open | Persist settings across reboots | `Logger.h`: add `#include <Preferences.h>`, load in `begin()`, save in setters. ~30 min effort |
| **Issue #1 fix** | Open | `resetSDFile()` data loss | Add `flushToSD()` before `_bufferCount=0` in `Logger.h::resetSDFile()`. One line. |

---

*Braunhousehold · jan.pfrang@delonghigroup.com · Firmware v4 · Architecture v2.0*
