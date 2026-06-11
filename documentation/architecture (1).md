# Shelly + ESP32 Power Logger — Architecture & Data Flow

> **Firmware v3** · Option B topology · Local Wi-Fi only · No internet required · DS3231 RTC

---

## Table of contents

1. [System overview](#system-overview)
2. [Who is who — roles at a glance](#who-is-who)
3. [Network topology](#network-topology)
4. [Data flow — step by step](#data-flow)
5. [IP address reference](#ip-address-reference)
6. [What happens at power-on](#power-on-sequence)

---

## System overview

The logger consists of three active devices, one RTC module, and one passive storage medium:

| Device | Role | Powered by |
|---|---|---|
| **ESP32-WROOM-32** | Logger, web server, Wi-Fi access point | Certified 230 V → 5 V DC PSU |
| **DS3231 RTC** | Real-time clock, battery-backed (CR2032) | 3.3 V from ESP32 via I²C (GPIO 21/22) |
| **Shelly Plug S MTR Gen3** | Power meter, relay, Wi-Fi client | 230 V mains (internal PSU) |
| **Phone / laptop** | Live display, download, settings | Battery / own PSU |
| **MicroSD card** | Permanent log storage | 3.3 V from ESP32 via SPI |

The **appliance under test** plugs into the Shelly socket. The Shelly measures its power consumption and pushes the data to the ESP32 every second over Wi-Fi. The ESP32 buffers, logs to SD with real wall-clock timestamps from the DS3231, and serves a web interface. No internet connection is involved at any point.

---

## Who is who

### ESP32 — Access Point (AP)

The ESP32 creates and owns the Wi-Fi network. It is the equivalent of a router in this mini-system.

- **Broadcasts SSID:** `PZEM_Logger`
- **Password:** `logger1234`
- **Own IP:** `192.168.4.1` — fixed, never changes
- **Assigns IPs to clients** via DHCP starting at `192.168.4.2`
- **Runs:** HTTP web server on port 80, DNS catch-all (captive portal), mDNS (`braun_PZEM.local`)
- **Wi-Fi mode:** `WIFI_AP_STA` — acts as AP and has a STA interface reserved for future use

### DS3231 RTC — Real-time clock module

The DS3231 provides accurate wall-clock time, battery-backed via a CR2032 cell.

- **Interface:** I²C on GPIO 21 (SDA) / GPIO 22 (SCL) — hardware I²C bus 0
- **Used by:** `Logger.h` — `pollIfDue()` reads `rtc->now().unixtime()` at each sample
- **CSV output:** ISO-8601 `datetime` column (`2025-06-01T14:23:01`). Writes `RTC_NOT_SET` if battery is dead or clock has not been calibrated
- **Calibration:** requires a one-shot sketch after first power-on or battery replacement (see OTA deployment guide)

### Shelly Plug S MTR Gen3 — Wi-Fi Client (STA)

The Shelly joins the ESP32's network as a regular client, exactly like a phone would.

- **Joins SSID:** `PZEM_Logger` (pre-configured at manufacture)
- **Assigned IP:** `192.168.4.2` (first DHCP lease)
- **Sends to:** `http://192.168.4.1/api/shelly_push` — every 1 second
- **Measures:** voltage (V), active power (W), current (A) — directly from its internal meter chip
- **Derives on-device:** `pf_apparent = apower / (voltage × current)`
- **Cloud:** disabled — operates fully offline

### Phone / laptop — Wi-Fi Client (STA)

The user's device joins the same network as the Shelly.

- **Joins SSID:** `PZEM_Logger`
- **Assigned IP:** `192.168.4.3` or higher (second+ DHCP lease)
- **Talks to:** `http://192.168.4.1` (the ESP32 web server only — never directly to the Shelly)
- **Access:** `http://braun_PZEM.local` or `http://192.168.4.1`

---

## Network topology

The diagram below shows the three-layer structure: the ESP32 at the top as AP anchor, the two client devices in the middle, and the physical measurement chain at the bottom.

All arrows are **unidirectional data flows** — the Wi-Fi association lines (dashed) show network membership, not data direction.

<!-- NETWORK DIAGRAM -->
<svg width="100%" viewBox="0 0 680 560" role="img">
<title>Network and data flow diagram — Shelly + ESP32 power logger</title>
<desc>ESP32 is the Wi-Fi access point at the top. Shelly and Phone are clients below it on the left and right. SD card and appliance are at the bottom. Arrows show data flow directions.</desc>
<defs>
  <marker id="arr" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
    <path d="M2 1L8 5L2 9" fill="none" stroke="context-stroke" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
  </marker>
</defs>

<!-- ── WLAN boundary ── -->
<rect x="34" y="48" width="612" height="350" rx="16"
      fill="none" stroke="#378ADD" stroke-width="0.5" stroke-dasharray="6 4" opacity="0.6"/>
<text font-family="sans-serif" font-size="11" fill="#185FA5" x="52" y="68">
  Wi-Fi network · SSID: PZEM_Logger · 192.168.4.0/24
</text>

<!-- ══ ROW 1: ESP32 ══ -->
<rect x="220" y="88" width="240" height="66" rx="10"
      fill="#E6F1FB" stroke="#185FA5" stroke-width="0.5"/>
<rect x="290" y="74" width="100" height="18" rx="9"
      fill="#185FA5"/>
<text font-family="sans-serif" font-size="11" font-weight="500" fill="#E6F1FB"
      text-anchor="middle" x="340" y="87">ACCESS POINT</text>
<text font-family="sans-serif" font-size="13" font-weight="500" fill="#0C447C"
      text-anchor="middle" x="340" y="115">ESP32-WROOM-32</text>
<text font-family="sans-serif" font-size="11" fill="#185FA5"
      text-anchor="middle" x="340" y="134">192.168.4.1  ·  DHCP server  ·  HTTP :80  ·  DS3231 RTC</text>

<!-- ══ ROW 2 LEFT: Shelly ══ -->
<rect x="48" y="244" width="210" height="66" rx="10"
      fill="#E1F5EE" stroke="#0F6E56" stroke-width="0.5"/>
<rect x="90" y="230" width="126" height="18" rx="9"
      fill="#0F6E56"/>
<text font-family="sans-serif" font-size="11" font-weight="500" fill="#E1F5EE"
      text-anchor="middle" x="153" y="243">WI-FI CLIENT</text>
<text font-family="sans-serif" font-size="13" font-weight="500" fill="#085041"
      text-anchor="middle" x="153" y="271">Shelly Plug S MTR Gen3</text>
<text font-family="sans-serif" font-size="11" fill="#0F6E56"
      text-anchor="middle" x="153" y="290">192.168.4.2  ·  meter + script</text>

<!-- ══ ROW 2 RIGHT: Phone ══ -->
<rect x="422" y="244" width="210" height="66" rx="10"
      fill="#EEEDFE" stroke="#534AB7" stroke-width="0.5"/>
<rect x="464" y="230" width="126" height="18" rx="9"
      fill="#534AB7"/>
<text font-family="sans-serif" font-size="11" font-weight="500" fill="#EEEDFE"
      text-anchor="middle" x="527" y="243">WI-FI CLIENT</text>
<text font-family="sans-serif" font-size="13" font-weight="500" fill="#3C3489"
      text-anchor="middle" x="527" y="271">Phone / Browser</text>
<text font-family="sans-serif" font-size="11" fill="#534AB7"
      text-anchor="middle" x="527" y="290">192.168.4.x  ·  web UI consumer</text>

<!-- ══ ROW 3 LEFT: Appliance ══ -->
<rect x="48" y="430" width="210" height="56" rx="10"
      fill="#FAEEDA" stroke="#854F0B" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="13" font-weight="500" fill="#633806"
      text-anchor="middle" x="153" y="455">Appliance under test</text>
<text font-family="sans-serif" font-size="11" fill="#854F0B"
      text-anchor="middle" x="153" y="474">230 V  ·  plugged into Shelly</text>

<!-- ══ ROW 3 RIGHT: SD card ══ -->
<rect x="422" y="430" width="210" height="56" rx="10"
      fill="#F1EFE8" stroke="#5F5E5A" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="13" font-weight="500" fill="#2C2C2A"
      text-anchor="middle" x="527" y="455">MicroSD card</text>
<text font-family="sans-serif" font-size="11" fill="#5F5E5A"
      text-anchor="middle" x="527" y="474">SPI · local to ESP32 · /log.csv</text>

<!-- ARROWS -->

<!-- A: Shelly → ESP32 -->
<path d="M153 244 L153 190 L280 190 L280 154"
      fill="none" stroke="#1D9E75" stroke-width="2"
      marker-end="url(#arr)"/>
<rect x="148" y="178" width="124" height="26" rx="5"
      fill="#E1F5EE" stroke="#1D9E75" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="11" font-weight="500" fill="#085041"
      text-anchor="middle" x="210" y="188">POST /api/shelly_push</text>
<text font-family="sans-serif" font-size="10" fill="#0F6E56"
      text-anchor="middle" x="210" y="200">JSON · every 1 s</text>

<!-- B: Phone → ESP32 (request) -->
<path d="M527 244 L527 190 L400 190 L400 154"
      fill="none" stroke="#7F77DD" stroke-width="1.5"
      stroke-dasharray="5 3"
      marker-end="url(#arr)"/>
<rect x="408" y="178" width="112" height="26" rx="5"
      fill="#EEEDFE" stroke="#7F77DD" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="11" font-weight="500" fill="#3C3489"
      text-anchor="middle" x="464" y="188">GET /api/live</text>
<text font-family="sans-serif" font-size="10" fill="#534AB7"
      text-anchor="middle" x="464" y="200">browser polls 1 s</text>

<!-- C: ESP32 → Phone (response) -->
<path d="M430 154 L430 244"
      fill="none" stroke="#7F77DD" stroke-width="1.5"
      marker-end="url(#arr)"/>
<text font-family="sans-serif" font-size="10" fill="#534AB7" x="435" y="204">response</text>

<!-- D: ESP32 → SD -->
<path d="M460 121 L648 121 L648 430 L632 430"
      fill="none" stroke="#888780" stroke-width="1.5"
      marker-end="url(#arr)"/>
<rect x="550" y="356" width="96" height="36" rx="5"
      fill="#F1EFE8" stroke="#888780" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="11" font-weight="500" fill="#2C2C2A"
      text-anchor="middle" x="598" y="370">write /log.csv</text>
<text font-family="sans-serif" font-size="10" fill="#5F5E5A"
      text-anchor="middle" x="598" y="384">flush every 10 s</text>

<!-- E: Appliance → Shelly -->
<path d="M153 430 L153 310"
      fill="none" stroke="#BA7517" stroke-width="1.5"
      marker-end="url(#arr)"/>
<rect x="62" y="358" width="92" height="30" rx="5"
      fill="#FAEEDA" stroke="#BA7517" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="11" font-weight="500" fill="#633806"
      text-anchor="middle" x="108" y="370">230 V load</text>
<text font-family="sans-serif" font-size="10" fill="#854F0B"
      text-anchor="middle" x="108" y="382">Shelly measures</text>

<!-- Wi-Fi association lines -->
<line x1="100" y1="244" x2="248" y2="154"
      stroke="#1D9E75" stroke-width="0.5" stroke-dasharray="3 3" fill="none" opacity="0.5"/>
<line x1="580" y1="244" x2="432" y2="154"
      stroke="#7F77DD" stroke-width="0.5" stroke-dasharray="3 3" fill="none" opacity="0.5"/>

</svg>

---

## Data flow

### Flow 1 — Measurement push (Shelly → ESP32)

Every **1 second**, the script `shelly_push.js` running on the Shelly:

1. Calls `Shelly.getComponentStatus("switch", 0)` — a zero-latency local read of the meter chip
2. Extracts `voltage` (V), `apower` (W), `current` (A)
3. Computes `pf_apparent = apower / (voltage × current)`, clamped to `[0, 1]`
4. Sends an HTTP POST to `http://192.168.4.1/api/shelly_push` with body:
   ```json
   { "ts": 12345, "v": 230.1, "p": 847.3, "i": 3.681, "pf": 0.99 }
   ```
5. HTTP timeout is **1 s** (must be less than the 1 s push interval to ensure `_pushPending` clears before the next tick)

The ESP32 handler `WebPortal::handleShellyPush()` passes the body to `ShellyClient::ingest()`, which validates, parses, and caches the values.

### Flow 2 — Logging to SD (ESP32 internal)

Every **1 second**, `Logger::pollIfDue()` reads the last cached Shelly values and the current time from the DS3231 RTC. If the power reading exceeds the configured threshold, it adds a `Sample` to the RAM ring buffer (64 entries).

Every **10 seconds**, `Logger::flushIfDue()` writes all buffered samples to `/log.csv` on the SD card in append mode:

```
datetime,time_ms,voltage_V,power_W,pf_apparent
2025-06-01T14:23:01,12001,230.1,847.3,0.99
2025-06-01T14:23:02,13002,230.2,851.0,0.99
RTC_NOT_SET,14003,230.1,849.1,0.99
```

`datetime` shows `RTC_NOT_SET` if the DS3231 battery is dead or the clock has not been calibrated.

### Flow 3 — Live display (Phone → ESP32 → Phone)

The browser on the phone polls `GET /api/live` once per second. The ESP32 responds with the current cached values:

```json
{
  "power": 847.3,
  "voltage": 230.1,
  "pf": 0.99,
  "buffer": 3,
  "dropped": 0,
  "uptime": 4821,
  "shelly_ok": true,
  "sd_ok": true,
  "ota_active": false
}
```

### Flow 4 — File download (Phone → ESP32 → Phone)

When the user presses **Download**, `GET /download` causes the ESP32 to flush the RAM buffer to SD, then stream `/log.csv` as a file attachment to the browser.

---

## IP address reference

| Device | IP address | Assigned by | Fixed? |
|---|---|---|---|
| ESP32 | `192.168.4.1` | Self (softAP) | Always fixed |
| Shelly Plug S | `192.168.4.2` | ESP32 DHCP | First lease — effectively fixed |
| Phone / laptop | `192.168.4.3` or higher | ESP32 DHCP | Changes each session |

---

## Power-on sequence

```
t=0 s    ESP32 powers on
t=0 s    PowerMonitor grace window starts (10 s — inactive until POWER_MONITOR_ENABLED=1)
t=0 s    DS3231 RTC initialised via I²C (GPIO 21/22). Time read from battery-backed clock
t=1 s    SD card initialised, log header written if missing
t=2 s    Wi-Fi AP "PZEM_Logger" active at 192.168.4.1
t=2 s    HTTP server, DNS, mDNS all running
t=2 s    Shelly watchdog startup grace armed (15 s)
t=2 s    ESP32 LED → RED (fast blink, 5 Hz) — waiting for Shelly

t=5–15 s Shelly powers on, boots, runs shelly_push.js
t=10–20s Shelly joins "PZEM_Logger", gets IP 192.168.4.2
t=11–21s First HTTP POST arrives at ESP32 /api/shelly_push
         → startup grace clears, watchdog now active
t=11–21s ESP32 LED → GREEN (slow blink, 1 Hz) — system healthy

Any time Phone joins "PZEM_Logger", browses to http://192.168.4.1
         Live data visible immediately
```

**Boot-order note:** If the ESP32 boots after the Shelly is already running, the Shelly's Wi-Fi client needs 3–10 s to re-associate with the newly-appeared AP. The 15 s startup grace in `ShellyClient` prevents the watchdog from firing during this reconnection window, which would otherwise permanently lock the UI to `--`.

The **LED is the system health indicator**:

| LED pattern | Meaning |
|---|---|
| Fast red blink (5 Hz) | SD error, or Shelly not yet connected / watchdog timeout |
| Slow green blink (1 Hz) | All systems nominal — Shelly pushing data, SD healthy |

The Shelly watchdog triggers if no push arrives for `3 × 1000 ms = 3 seconds` (after the startup grace expires). The LED turns red and recovers automatically when pushes resume.

---

*Part of the BBRAUN Shelly Power Logger project — firmware v3 · contact: jan.pfrang@delonghigroup.com*
