# Requirements — Shelly ESP32 Power Logger
## Implementation Status Summary

| | |
|---|---|
| **Document version** | v1.03 updated |
| **Firmware version** | v3 (Shelly + OTA + Power-loss + RTC) |
| **Status** | Updated to reflect DS3231 RTC (U9 fulfilled), boot-order watchdog fix, PowerMonitor enable flag |

### Summary

| Status | Count |
|---|---|
| ✅ Fulfilled | 23 |
| ⚠ Partial | 6 |
| ❌ Not fulfilled | 1 |
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
| **U 6** | It logs with appropriate sampling frequency and accuracy for standard small electric appliances | shall | ⚠ Partial | Default 1 s push cadence from Shelly. PF is derived (`apower / V·I`), not hardware-measured — column `pf_apparent` |
| **U 7** | It can be safely used in the kitchen environment | shall | ⚙ Hardware | Depends on enclosure build (IP44, cable glands, fuse, clearance/creepage) |
| **U 8** | It does not cost (materials only) more than 60 € | should | ⚙ Hardware | BOM ≈ 50–55 € in single-unit pricing |
| **U 9** | Date and time logged with each sample | — | ✅ **Fulfilled in v3** | DS3231 RTC on I²C (GPIO 21 SDA / GPIO 22 SCL). ISO-8601 `datetime` column added to CSV. Battery-backed (CR2032) — survives power cycles. `RTC_NOT_SET` written if battery dead or not yet calibrated. `time_ms` (ESP32 uptime) retained alongside `datetime` for correlation |

---

## 3. Technical requirements

| Req. ID | Detail | Priority | Status | Notes |
|---|---|---|---|---|
| **1** | Device polls power per default every 1 s | shall | ✅ Fulfilled | `INTERVAL_SHELLY_POLL_MS = 1000 ms` (Config.h). Shelly pushes at this cadence; `Logger::pollIfDue()` consumes the cached value |
| **2** | Device logs power with affiliated timestep per default every 10 s to memory | shall | ✅ Fulfilled | `INTERVAL_SD_FLUSH_MS = 10000 ms`. `Logger::flushIfDue()` writes RAM buffer to SD every 10 s. Each sample carries its own `millis_ts` and `datetime` (DS3231) |
| **3** | Device does not overwrite existing log data | shall | ✅ Fulfilled | Log file opened with `FILE_APPEND`. Reset only via explicit POST `/reset` |
| **4** | The memory is removable / interchangeable | shall | ⚙ Hardware | Standard SD card on SPI — physically removable |
| **5** | Device uses a format easy to post-process on PC (.CSV or similar) | shall | ✅ Fulfilled | CSV header: `datetime,time_ms,voltage_V,power_W,pf_apparent`. `datetime` column now carries ISO-8601 wall-clock time from DS3231 |
| **6** | Memory resists 100 h which equals 36 000 write cycles at 1/10 s writing frequency | shall | ✅ Fulfilled | SD cards withstand >100 k write cycles per block; 36 000 appends to one file is well within spec. Flush every 10 s — actual write load is 10× lower than the theoretical maximum |
| **7** | It is capable to measure up to 10 A at 230 V | shall | ⚙ Hardware | Shelly Plug S MTR Gen3 rated 16 A / 3680 W max; effective range limited by 8–10 A fuse per BOM |
| **9** | Device transmits logged data via wireless function to host | should | ✅ Fulfilled | `WIFI_AP_STA` — ESP32 runs softAP; Shelly and phone join as STA clients. POST `/api/shelly_push` receives measurements every 1 s. No external network required |
| **10** | Allows to connect both types of electric devices: protective earth devices and EU-plug devices | shall | ⚙ Hardware | Depends on socket/plug wiring (grounded EU socket per BOM) |
| **11** | Fulfils the requirements for clearance and creepage | shall | ⚙ Hardware | Enclosure / PCB layout responsibility |
| **12** | Electric protection is ≥ IP44 | should | ⚙ Hardware | Enclosure responsibility |
| **13** | Allows safe flush of memory if the power connection is lost | should | ⚠ Partial | `PowerMonitor.h` v2 implements 9 V rail monitoring (GPIO 35, 180 kΩ/47 kΩ divider). On sustained drop below 7.35 V: flushes SD, sheds Wi-Fi, idles until brownout or mains recovery. **`POWER_MONITOR_ENABLED=0` in `Config.h`** — must be set to `1` once the supercap/divider circuit is bench-validated. At `0`, `PowerMonitor::update()` and `isPowerLost()` are compile-time no-ops (prevents false shutdown from floating GPIO 35) |
| **14** | User interface section header | N/A | ℹ N/A | — |
| **15** | Blinks 1/s if polling and logging work correctly and no wireless connection is established | shall | ✅ Fulfilled | `INTERVAL_LED_OK_MS = 500 ms` → 1 Hz blink (`StatusLed::LED_OK`) |
| **16** | Blinks 5/s in case polling or logging do not work correctly | shall | ✅ Fulfilled | `INTERVAL_LED_ERR_MS = 100 ms` → 5 Hz blink. Triggered by SD error OR Shelly watchdog timeout (3 s silence). Also blinks 5 Hz during OTA upload — expected, recovers on reboot |
| **16b** | Blinks 0.5/s if polling and logging work correctly and wireless connection is established | can | ❌ Not fulfilled | `StatusLed` has only two states (`LED_OK` / `LED_ERROR`). **ACTION:** add third state `LED_OK_WIFI` at 2000 ms interval; switch on `WiFi.softAPgetStationNum() > 0` |
| **17** | Device comes with an additional on-line user interface once connected to host via wireless | should | ✅ Fulfilled | Web portal at `192.168.4.1` / `http://braun_PZEM.local` |
| **18** | Displays the current live data on the host | should | ✅ Fulfilled | Home page polls `/api/live` every 1 s; shows power (W), voltage (V), `pf_apparent`. JSON includes `shelly_ok`, `sd_ok`, `ota_active` |
| **19** | Allows to download all stored data — "DOWNLOAD DATA" section | should | ✅ Fulfilled | `GET /download` streams `/log.csv` as `Content-Disposition: attachment` |
| **20** | Allows to delete / reset the data storage memory — "DELETE MEMORY" section | should | ✅ Fulfilled | `POST /reset` calls `Logger::resetSDFile()`. Confirmation dialog in JS |
| **21** | Allows to set the poll rate between 1, 2, 5, 10, 30 s — "SETUP" section | can | ⚠ Partial | `/settings` rate grid allows `{1000, 2000, 5000, 10000, 30000}` ms. ⚠ Rates other than 1 s not yet validated end-to-end against fixed 1 Hz Shelly push cadence |
| **22** | Allows to set up the start of log at power consumptions > 1, 2, 5, 10, 20, 50 W | can | ✅ Fulfilled | Threshold grid `{0, 1, 2, 5, 10, 20, 50}` W in `/settings` UI. `Logger::setPowerThreshold()` applies immediately |
| **23** | Allows the user to read a built-in user manual — "MANUAL" | can | ⚠ Partial | `/readme` page (`PAGE_README`) covers Shelly architecture and OTA section. Troubleshooting depth still light |
| **24** | Allows to plot the live data on the host — "LIVE PLOTTER" | can | ✅ Fulfilled | `/liveplot` canvas oscilloscope — last 60 samples, auto-scaled Y axis. `fetchIntervalMs` synced to `/api/settings`, floored at 1000 ms |
| **27** | Device should carry all relevant information for wireless connectivity on the device | should | ✅ Fulfilled | Housing labelled with SSID (`PZEM_Logger`), password (`logger1234`), web UI URL (`http://192.168.4.1`), mDNS (`http://braun_PZEM.local`), and Shelly URL (`http://192.168.4.2`) |
| **28** | Device must not connect to the internet but locally only | should | ✅ Fulfilled | `WIFI_AP_STA` — softAP only. No WAN route. Shelly cloud disabled at factory |
| **28b** | Accessed by a plain-text domain — BRAUN_PZEM — not a hard-to-remember URL | can | ✅ Fulfilled | `MDNS.begin(WIFI_AP_HOSTNAME)` in `WebPortal::begin()`. Reachable at `http://braun_PZEM.local` |
| **29** | Firmware updates can be installed over the air (OTA) without a physical USB connection | can | ✅ Fulfilled | OTA via `Update.h`. `GET /update` serves upload form with live progress bar; `POST /update` streams `.bin` chunk-by-chunk and reboots on success. `Logger::setOtaInProgress(true)` flushes SD buffer before flashing — no log samples lost |

---

## 4. Specifications

| Spec | Status | Notes |
|---|---|---|
| Use existing off-the-shelf components (Shelly, ESP32, certified PSU, SD card, housing, supercaps, DS3231 RTC) | ⚠ Partial | BOM: Shelly Plug S MTR Gen3 + certified 230→5 V PSU (TSR-1-2450 on 9 V rail) + ESP32 + SD shield + IP44 housing + 2×1 F supercaps + 180 kΩ/47 kΩ divider + **DS3231 RTC module** (new in v3) |
| **PIN layout ESP32 (Config.h v9):** | ✅ Fulfilled | |
| GPIO 25 — SD CS | ✅ | |
| GPIO 14 — SD MOSI | ✅ | |
| GPIO 27 — SD CLK | ✅ | |
| GPIO 26 — SD MISO | ✅ | |
| GPIO 32 — LED | ✅ | |
| GPIO 33 — Button (reserved, not yet in code) | ⚠ Reserved | Defined in hardware, not yet in firmware |
| GPIO 35 — ADC supply monitoring (9 V rail, power-loss, Req 13) | ⚠ Partial | Firmware ready; `POWER_MONITOR_ENABLED=0` until HW circuit validated |
| **GPIO 21 — DS3231 SDA (I²C)** | ✅ **New in v3** | Added `PIN_RTC_SDA=21` in `Config.h` |
| **GPIO 22 — DS3231 SCL (I²C)** | ✅ **New in v3** | Added `PIN_RTC_SCL=22` in `Config.h` |
| GPIO 16/17 — free (former PZEM UART2) | ✅ | Freed and unassigned. No 230 V connections on ESP32 side |

---

## Changelog

| Version | Changes |
|---|---|
| v1.00 | Initial requirements |
| v1.01 | Req 27 fulfilled (physical label). U9 + OTA added as open requirements |
| v1.02 | Firmware v7: OTA fulfilled (Req 29). PowerMonitor partial (Req 13). `SHELLY_ERROR_THRESHOLD=3` noted |
| **v1.03** | **Firmware v3 updates:** U9 fulfilled via DS3231 RTC (GPIO 21/22). `datetime` column added to CSV (`LOG_FILE_HEADER`). `SHELLY_STARTUP_GRACE_MS=15000` added — boot-order watchdog false-alarm fix (HTTP timeout also reduced to 1 s in `shelly_push.js`). `POWER_MONITOR_ENABLED=0` flag added to `Config.h` — prevents false shutdown when 9 V circuit not yet soldered. `POWER_STARTUP_GRACE_MS` increased 3000→10000 ms. `RTClib` added to CI library install |
