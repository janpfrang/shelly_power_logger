# Requirements — Shelly ESP32 Power Logger
## Implementation Status Summary

| | |
|---|---|
| **Document version** | v1.04 |
| **Firmware version** | v4 (timing fixes, SD flush guard, download watchdog fix) |
| **Status** | Updated to reflect loop-order fix, watchdog threshold change, push cadence change, WiFi mode change, DNS/mDNS disabled, SD flush timeout guard, download watchdog grace |

### Summary

| Status | Count |
|---|---|
| ✅ Fulfilled | 22 |
| ⚠ Partial | 7 |
| ❌ Not fulfilled | 2 |
| ⚙ Hardware / out of firmware scope | 7 |
| ℹ N/A (section header) | 1 |

---

## 1. Introduction and purpose

Device is intended to determine and log mainly the power consumption of small kitchen appliances while in real day use. The data will be analyzed and post-processed to give statements about the power consumption (min, max, average, distribution in histograms) to improve the devices and to evaluate if the lifetime testing represents real use.

---

## 2. User requirements

| Req. ID | Detail | Priority | Status | Notes |
|---|---|---|---|---|
| **U 1** | Device logs the power consumption of an attached slave device with common EU-plugs | shall | ✅ Fulfilled | 230 V switching delegated to certified Shelly Plug S MTR Gen3 (EU F-type, CE-marked). ESP32 receives push from `shelly_push.js` via POST `/api/shelly_push`. `ShellyClient::ingest()` parses & validates |
| **U 2** | It stores the data permanently | shall | ✅ Fulfilled | Samples stored on removable SD card as `/log.csv` in `FILE_APPEND` mode; no overwrite on boot |
| **U 3** | The stored data is post-processed | shall | ⚠ Partial | CSV format suitable for pandas/Excel. No dedicated post-processing scripts shipped yet — web UI provides `/liveplot` and `/histogram` for in-browser analysis |
| **U 4** | The end user does not need to program or to set up the logger and just plugs it in | shall | ⚠ Partial | Plug-and-play after one-time factory pairing of the Shelly to SSID `PZEM_Logger`. Once paired, user only plugs both devices in. RTC requires one-time calibration at manufacture |
| **U 5** | The use period ranges from 5 min to 50 h | shall | ⚠ Partial | Continuous operation supported (`millis()` rollover ~49.7 days). Long-run (>50 h) not yet field-verified |
| **U 6** | It logs with appropriate sampling frequency and accuracy for standard small electric appliances | shall | ⚠ Partial | Shelly pushes at **1.2 s cadence** (changed from 1 s in v4 — see Req 1 notes). PF is derived (`apower / V·I`), not hardware-measured — column `pf_apparent` |
| **U 7** | It can be safely used in the kitchen environment | shall | ⚙ Hardware | Depends on enclosure build (IP44, cable glands, fuse, clearance/creepage) |
| **U 8** | It does not cost (materials only) more than 60 € | should | ⚙ Hardware | BOM ≈ 50–55 € in single-unit pricing |
| **U 9** | Date and time logged with each sample | — | ✅ Fulfilled | DS3231 RTC on I²C (GPIO 21 SDA / GPIO 22 SCL). ISO-8601 `datetime` column in CSV. Battery-backed (CR2032) — survives power cycles. `RTC_NOT_SET` written if battery dead or not yet calibrated. `time_ms` (ESP32 uptime) retained alongside `datetime` for correlation |

---

## 3. Technical requirements

| Req. ID | Detail | Priority | Status | Notes |
|---|---|---|---|---|
| **1** | Device polls power per default every 1 s | shall | ⚠ Partial | `INTERVAL_SHELLY_POLL_MS = 1000 ms` (Config.h) sets the ESP32 logging cadence and watchdog multiplier. However, `shelly_push.js` now pushes at **1200 ms** (changed from 1000 ms in v4) to eliminate the `_pushPending` zero-margin race between HTTP timeout and timer re-fire. The logged sample interval is therefore 1.2 s, not 1.0 s. Measurement accuracy is unaffected — the Shelly reads the latest meter value each tick. |
| **2** | Device logs power with affiliated timestep per default every 10 s to memory | shall | ✅ Fulfilled | `INTERVAL_SD_FLUSH_MS = 10000 ms`. `Logger::flushIfDue()` writes RAM buffer to SD every 10 s. Each sample carries its own `millis_ts` and `datetime` (DS3231). New in v4: `SD_FLUSH_TIMEOUT_MS = 400 ms` guard — if a flush exceeds 400 ms the SD is declared failed and auto-recovery (`tryRecoverSD()`) is triggered on the next cycle instead of repeated blocking |
| **3** | Device does not overwrite existing log data | shall | ✅ Fulfilled | Log file opened with `FILE_APPEND`. Reset only via explicit POST `/reset` |
| **4** | The memory is removable / interchangeable | shall | ⚙ Hardware | Standard SD card on SPI — physically removable |
| **5** | Device uses a format easy to post-process on PC (.CSV or similar) | shall | ✅ Fulfilled | CSV header: `datetime,time_ms,voltage_V,power_W,pf_apparent`. `datetime` column carries ISO-8601 wall-clock time from DS3231 |
| **6** | Memory resists 100 h which equals 36 000 write cycles at 1/10 s writing frequency | shall | ✅ Fulfilled | SD cards withstand >100 k write cycles per block. Flush every 10 s — actual write load is well within spec |
| **7** | It is capable to measure up to 10 A at 230 V | shall | ⚙ Hardware | Shelly Plug S MTR Gen3 rated 16 A / 3680 W max; effective range limited by 8–10 A fuse per BOM |
| **9** | Device transmits logged data via wireless function to host | should | ✅ Fulfilled | ESP32 runs softAP (`WIFI_AP` mode — changed from `WIFI_AP_STA` in v4 to prevent background channel scanning). Shelly and phone join as STA clients. POST `/api/shelly_push` receives measurements every 1.2 s. No external network required |
| **10** | Allows to connect both types of electric devices: protective earth devices and EU-plug devices | shall | ⚙ Hardware | Depends on socket/plug wiring (grounded EU socket per BOM) |
| **11** | Fulfils the requirements for clearance and creepage | shall | ⚙ Hardware | Enclosure / PCB layout responsibility |
| **12** | Electric protection is ≥ IP44 | should | ⚙ Hardware | Enclosure responsibility |
| **13** | Allows safe flush of memory if the power connection is lost | should | ⚠ Partial | `PowerMonitor.h` v2 implements 9 V rail monitoring (GPIO 35, 180 kΩ/47 kΩ divider). On sustained drop below 7.35 V: flushes SD, sheds Wi-Fi, idles until brownout or mains recovery. **`POWER_MONITOR_ENABLED=0` in `Config.h`** — must be set to `1` once the supercap/divider circuit is bench-validated. At `0`, `PowerMonitor::update()` and `isPowerLost()` are compile-time no-ops (prevents false shutdown from floating GPIO 35) |
| **14** | User interface section header | N/A | ℹ N/A | — |
| **15** | Blinks 1/s if polling and logging work correctly and no wireless connection is established | shall | ✅ Fulfilled | `INTERVAL_LED_OK_MS = 500 ms` → 1 Hz blink (`StatusLed::LED_OK`) |
| **16** | Blinks 5/s in case polling or logging do not work correctly | shall | ✅ Fulfilled | `INTERVAL_LED_ERR_MS = 100 ms` → 5 Hz blink. Triggered by SD error OR Shelly watchdog timeout. Watchdog window is now **5 s** (`SHELLY_ERROR_THRESHOLD = 5` × 1000 ms — changed from 3 s in v4) to accommodate the 1.2 s push cadence and SD flush latency. Also blinks 5 Hz during OTA upload — expected, recovers on reboot. **Note:** the `/readme` page in the web UI still states "Shelly silent for >3 s" — this text is stale and should be updated to 5 s |
| **16b** | Blinks 0.5/s if polling and logging work correctly and wireless connection is established | can | ❌ Not fulfilled | `StatusLed` has only two states (`LED_OK` / `LED_ERROR`). **ACTION:** add third state `LED_OK_WIFI` at 2000 ms interval; switch on `WiFi.softAPgetStationNum() > 0` |
| **17** | Device comes with an additional on-line user interface once connected to host via wireless | should | ✅ Fulfilled | Web portal at `http://192.168.4.1` |
| **18** | Displays the current live data on the host | should | ✅ Fulfilled | Home page polls `/api/live` every 1 s; shows power (W), voltage (V), `pf_apparent`. JSON includes `shelly_ok`, `sd_ok`, `ota_active` |
| **19** | Allows to download all stored data — "DOWNLOAD DATA" section | should | ✅ Fulfilled | `GET /download` streams `/log.csv` as `Content-Disposition: attachment`. New in v4: `beginStartupGrace()` called before `streamFile()` so the Shelly watchdog does not trip during the transfer (which blocks `loop()` for the full file duration) |
| **20** | Allows to delete / reset the data storage memory — "DELETE MEMORY" section | should | ✅ Fulfilled | `POST /reset` calls `Logger::resetSDFile()`. Confirmation dialog in JS |
| **21** | Allows to set the poll rate between 1, 2, 5, 10, 30 s — "SETUP" section | can | ⚠ Partial | `/settings` rate grid allows `{1000, 2000, 5000, 10000, 30000}` ms for the **logging rate** (`_pollIntervalMs`). The Shelly push cadence is fixed at 1.2 s regardless of this setting. At logging rates > 1 s, every N-th push is discarded by `pollIfDue()` — this is by design. Rates other than 1 s not yet fully validated |
| **22** | Allows to set up the start of log at power consumptions > 1, 2, 5, 10, 20, 50 W | can | ✅ Fulfilled | Threshold grid `{0, 1, 2, 5, 10, 20, 50}` W in `/settings` UI. `Logger::setPowerThreshold()` applies immediately |
| **23** | Allows the user to read a built-in user manual — "MANUAL" | can | ⚠ Partial | `/readme` page (`PAGE_README`) covers hardware, connectivity, pages and log format. Contains stale text: "Shelly silent for >3 s" should read "5 s"; "pushes data to the ESP32 every 1 s" should read "every 1.2 s". mDNS (`http://braun_PZEM.local`) is listed but is currently **disabled** in firmware |
| **24** | Allows to plot the live data on the host — "LIVE PLOTTER" | can | ✅ Fulfilled | `/liveplot` canvas oscilloscope — last 60 samples, auto-scaled Y axis. `fetchIntervalMs` synced to `/api/settings`, floored at 1000 ms |
| **27** | Device should carry all relevant information for wireless connectivity on the device | should | ✅ Fulfilled | Housing labelled with SSID (`PZEM_Logger`), password (`logger1234`), web UI URL (`http://192.168.4.1`), and Shelly URL (`http://192.168.4.2`). **Note:** mDNS label (`http://braun_PZEM.local`) on the housing is no longer valid — mDNS is disabled in firmware |
| **28** | Device must not connect to the internet but locally only | should | ✅ Fulfilled | `WIFI_AP` mode — softAP only, no STA interface active, no WAN route. Shelly cloud disabled at factory |
| **28b** | Accessed by a plain-text domain — BRAUN_PZEM — not a hard-to-remember URL | can | ❌ Not fulfilled | `MDNS.begin()` is **disabled** in `WebPortal::begin()` because ESPmDNS background UDP processing caused 100–500 ms loop stalls that blocked HTTP responses. `http://braun_PZEM.local` does not resolve. Use `http://192.168.4.1` instead. **ACTION:** re-evaluate whether a lightweight mDNS implementation or a DNS-SD redirect approach can be used without the loop stalls, or formally downgrade this requirement |
| **29** | Firmware updates can be installed over the air (OTA) without a physical USB connection | can | ✅ Fulfilled | OTA via `Update.h`. `GET /update` serves upload form with live progress bar; `POST /update` streams `.bin` chunk-by-chunk and reboots on success. `Logger::setOtaInProgress(true)` flushes SD buffer before flashing — no log samples lost |

---

## 4. Specifications

| Spec | Status | Notes |
|---|---|---|
| Use existing off-the-shelf components (Shelly, ESP32, certified PSU, SD card, housing, supercaps, DS3231 RTC) | ⚠ Partial | BOM: Shelly Plug S MTR Gen3 + certified 230→5 V PSU (TSR-1-2450 on 9 V rail) + ESP32 + SD shield + IP44 housing + 2×1 F supercaps + 180 kΩ/47 kΩ divider + DS3231 RTC module |
| **PIN layout ESP32 (Config.h v9):** | ✅ Fulfilled | |
| GPIO 25 — SD CS | ✅ | |
| GPIO 14 — SD MOSI | ✅ | |
| GPIO 27 — SD CLK | ✅ | |
| GPIO 26 — SD MISO | ✅ | |
| GPIO 32 — LED | ✅ | |
| GPIO 33 — Button (reserved, not yet in code) | ⚠ Reserved | Defined in hardware, not yet in firmware |
| GPIO 35 — ADC supply monitoring (9 V rail, power-loss, Req 13) | ⚠ Partial | Firmware ready; `POWER_MONITOR_ENABLED=0` until HW circuit validated |
| GPIO 21 — DS3231 SDA (I²C) | ✅ | `PIN_RTC_SDA=21` in `Config.h` |
| GPIO 22 — DS3231 SCL (I²C) | ✅ | `PIN_RTC_SCL=22` in `Config.h` |
| GPIO 16/17 — free (former PZEM UART2) | ✅ | Freed and unassigned |

---

## 5. Firmware v4 change summary (delta from v1.03)

The following firmware changes since v1.03 affect requirement status:

| Change | Affected requirements | Impact |
|---|---|---|
| `WIFI_AP_STA` → `WIFI_AP` + `WiFi.setSleep(false)` | Req 9, Req 28 | Eliminates ~400 ms AP deaf periods from background channel scanning. WiFi mode now correctly described as AP-only |
| DNS catch-all disabled | Req 17, Req 23 | Prevents iOS/Windows from classifying AP as "no internet" and disconnecting. Direct IP access required: `http://192.168.4.1` |
| mDNS disabled | Req 28b | `http://braun_PZEM.local` no longer resolves. mDNS caused 100–500 ms loop stalls. **Req 28b now ❌ Not fulfilled** |
| `SHELLY_ERROR_THRESHOLD` 3→5 | Req 16 | Watchdog window extended from 3 s to 5 s. Eliminates false Shelly-error LED triggers caused by SD flush blocking and push cadence drift |
| `PUSH_INTERVAL_MS` 1000→1200 ms, HTTP timeout 1→2 s | Req 1, U 6 | Eliminates `_pushPending` zero-margin race. Effective logging cadence is now 1.2 s, not 1.0 s. **Req 1 now ⚠ Partial** |
| Loop order: `webPortal.update()` moved before `pollIfDue()` | Req 1, Req 18 | Push data is ingested in the same loop iteration before being read by the logger — eliminates one-loop timestamp lag |
| `SD_FLUSH_TIMEOUT_MS = 400` guard in `flushToSD()` | Req 2, Req 13 | Slow SD writes (>400 ms) now flag the card as failed and trigger recovery instead of repeated blocking. Reduces SD-flush gaps in log |
| `beginStartupGrace()` in `handleDownload()` | Req 19 | Watchdog no longer trips during file download. Eliminates 15–18 s post-download logging gap |
| Stale text in `/readme` and `PAGE_INDEX` UI | Req 23 | "Shelly silent for >3 s" and "every 1 s" text in web UI not yet updated to reflect 5 s watchdog and 1.2 s cadence |

---

## Changelog

| Version | Changes |
|---|---|
| v1.00 | Initial requirements |
| v1.01 | Req 27 fulfilled (physical label). U9 + OTA added as open requirements |
| v1.02 | Firmware v7: OTA fulfilled (Req 29). PowerMonitor partial (Req 13). `SHELLY_ERROR_THRESHOLD=3` noted |
| v1.03 | Firmware v3 updates: U9 fulfilled via DS3231 RTC. `datetime` column added to CSV. `SHELLY_STARTUP_GRACE_MS=15000` added. `POWER_MONITOR_ENABLED=0` flag added. `POWER_STARTUP_GRACE_MS` increased 3000→10000 ms |
| **v1.04** | **Firmware v4 updates:** WiFi mode `WIFI_AP_STA`→`WIFI_AP` + `WiFi.setSleep(false)`. DNS catch-all disabled. mDNS disabled → Req 28b now ❌. Push cadence 1000→1200 ms + HTTP timeout 1→2 s → Req 1 now ⚠ Partial. `SHELLY_ERROR_THRESHOLD` 3→5. Loop order fix (webPortal first). `SD_FLUSH_TIMEOUT_MS=400` guard. `beginStartupGrace()` in download handler. Stale UI text noted in Req 16, Req 23, Req 27 |
