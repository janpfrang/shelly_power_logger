# Shelly + ESP32 Power Logger — Architecture & Data Flow

> **Firmware v1** · Option B topology · Local Wi-Fi only · No internet required

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

The logger consists of three active devices and one passive storage medium:

| Device | Role | Powered by |
|---|---|---|
| **ESP32-WROOM-32** | Logger, web server, Wi-Fi access point | Certified 230 V → 5 V DC PSU |
| **Shelly Plug S MTR Gen3** | Power meter, relay, Wi-Fi client | 230 V mains (internal PSU) |
| **Phone / laptop** | Live display, download, settings | Battery / own PSU |
| **MicroSD card** | Permanent log storage | 3.3 V from ESP32 via SPI |

The **appliance under test** plugs into the Shelly socket. The Shelly measures its power consumption and pushes the data to the ESP32 every second over Wi-Fi. The ESP32 buffers, logs to SD, and serves a web interface. No internet connection is involved at any point.

---

## Who is who

### ESP32 — Access Point (AP)

The ESP32 creates and owns  the Wi-Fi network. It is the equivalent of a router in this mini-system.

- **Broadcasts SSID:** `PZEM_Logger`
- **Password:** `logger1234`
- **Own IP:** `192.168.4.1` — fixed, never changes
- **Assigns IPs to clients** via DHCP starting at `192.168.4.2`
- **Runs:** HTTP web server on port 80, DNS catch-all (captive portal), mDNS (`braun_PZEM.local`)
- **Wi-Fi mode:** `WIFI_AP_STA` — acts as AP and has a STA interface reserved for future use

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
      text-anchor="middle" x="340" y="134">192.168.4.1  ·  DHCP server  ·  HTTP :80</text>

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

<!-- ──────────────────────────────────────────────── -->
<!-- ARROWS — all orthogonal, no diagonals            -->
<!-- ──────────────────────────────────────────────── -->

<!-- A: Shelly → ESP32  (HTTP POST, left channel)
     Goes: up from top-centre of Shelly box (x=153,y=244)
     Then: L-bend right to x=280 at y=190, then up to ESP32 bottom (x=280,y=154) -->
<path d="M153 244 L153 190 L280 190 L280 154"
      fill="none" stroke="#1D9E75" stroke-width="2"
      marker-end="url(#arr)"/>
<!-- Label sits on the horizontal segment, clear of all boxes -->
<rect x="148" y="178" width="124" height="26" rx="5"
      fill="#E1F5EE" stroke="#1D9E75" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="11" font-weight="500" fill="#085041"
      text-anchor="middle" x="210" y="188">POST /api/shelly_push</text>
<text font-family="sans-serif" font-size="10" fill="#0F6E56"
      text-anchor="middle" x="210" y="200">JSON · every 1 s</text>

<!-- B: Phone → ESP32  (HTTP GET request, right channel)
     Goes: up from top-centre of Phone box (x=527,y=244)
     Then: L-bend left to x=400 at y=190, then up to ESP32 bottom (x=400,y=154)
     Dashed = request -->
<path d="M527 244 L527 190 L400 190 L400 154"
      fill="none" stroke="#7F77DD" stroke-width="1.5"
      stroke-dasharray="5 3"
      marker-end="url(#arr)"/>
<!-- Label -->
<rect x="408" y="178" width="112" height="26" rx="5"
      fill="#EEEDFE" stroke="#7F77DD" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="11" font-weight="500" fill="#3C3489"
      text-anchor="middle" x="464" y="188">GET /api/live</text>
<text font-family="sans-serif" font-size="10" fill="#534AB7"
      text-anchor="middle" x="464" y="200">browser polls 1 s</text>

<!-- C: ESP32 → Phone  (HTTP response)
     Goes: straight down from ESP32 right-bottom area (x=430,y=154) to Phone top (x=430,y=244) -->
<path d="M430 154 L430 244"
      fill="none" stroke="#7F77DD" stroke-width="1.5"
      marker-end="url(#arr)"/>
<text font-family="sans-serif" font-size="10" fill="#534AB7" x="435" y="204">response</text>

<!-- D: ESP32 → SD  (internal write)
     Route at x=648 — right of Phone box right edge (632) to avoid overlap.
     Path: right from ESP32 right (460,121) → corner (648,121) → down (648,430) → left to SD (632,430) -->
<path d="M460 121 L648 121 L648 430 L632 430"
      fill="none" stroke="#888780" stroke-width="1.5"
      marker-end="url(#arr)"/>
<!-- Label placed at y=358-394, below Phone box bottom (y=310) — no overlap -->
<rect x="550" y="356" width="96" height="36" rx="5"
      fill="#F1EFE8" stroke="#888780" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="11" font-weight="500" fill="#2C2C2A"
      text-anchor="middle" x="598" y="370">write /log.csv</text>
<text font-family="sans-serif" font-size="10" fill="#5F5E5A"
      text-anchor="middle" x="598" y="384">flush every 10 s</text>

<!-- E: Appliance → Shelly  (230V physical measurement)
     Straight up from appliance top (x=153,y=430) to Shelly bottom (x=153,y=310) -->
<path d="M153 430 L153 310"
      fill="none" stroke="#BA7517" stroke-width="1.5"
      marker-end="url(#arr)"/>
<rect x="62" y="358" width="92" height="30" rx="5"
      fill="#FAEEDA" stroke="#BA7517" stroke-width="0.5"/>
<text font-family="sans-serif" font-size="11" font-weight="500" fill="#633806"
      text-anchor="middle" x="108" y="370">230 V load</text>
<text font-family="sans-serif" font-size="10" fill="#854F0B"
      text-anchor="middle" x="108" y="382">Shelly measures</text>

<!-- ── Wi-Fi association lines (dashed background) ── -->
<!-- Shelly ↔ ESP32 AP association: left side vertical dashed -->
<line x1="100" y1="244" x2="248" y2="154"
      stroke="#1D9E75" stroke-width="0.5" stroke-dasharray="3 3" fill="none" opacity="0.5"/>
<!-- Phone ↔ ESP32 AP association: right side -->
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
5. Waits for the ESP32's `200 OK` response before sending the next push

The ESP32 handler `WebPortal::handleShellyPush()` passes the body to `ShellyClient::ingest()`, which validates, parses, and caches the values.

### Flow 2 — Logging to SD (ESP32 internal)

Every **1 second**, `Logger::pollIfDue()` reads the last cached Shelly values. If the power reading exceeds the configured threshold, it adds a `Sample` to the RAM ring buffer (64 entries).

Every **10 seconds**, `Logger::flushIfDue()` writes all buffered samples to `/log.csv` on the SD card in append mode:

```
time_ms,voltage_V,power_W,pf_apparent
12001,230.1,847.3,0.99
13002,230.2,851.0,0.99
```

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
  "sd_ok": true
}
```

The phone never talks directly to the Shelly — all communication goes through the ESP32.

### Flow 4 — File download (Phone → ESP32 → Phone)

When the user presses **Download** in the web UI, the browser sends `GET /download`. The ESP32 first calls `flushToSD()` to ensure nothing is left in the RAM buffer, then streams `/log.csv` as a file attachment. The entire CSV lands on the phone.

---

## IP address reference

| Device | IP address | Assigned by | Fixed? |
|---|---|---|---|
| ESP32 | `192.168.4.1` | Self (softAP) | Always fixed |
| Shelly Plug S | `192.168.4.2` | ESP32 DHCP | First lease — effectively fixed |
| Phone / laptop | `192.168.4.3` or higher | ESP32 DHCP | Changes each session |

The ESP32's IP `192.168.4.1` is the default for all ESP32 softAP configurations and never changes. The Shelly always gets `.2` because it is the first device to join after the AP starts up (it joins within seconds of power-on due to the pre-configured credentials).

---

## Power-on sequence

```
t=0 s    ESP32 powers on
t=1 s    SD card initialised, log header written if missing
t=2 s    Wi-Fi AP "PZEM_Logger" active at 192.168.4.1
t=2 s    HTTP server, DNS, mDNS all running
t=2 s    ESP32 LED → RED (fast blink, 5 Hz) — waiting for Shelly

t=5–15 s Shelly powers on, boots, runs shelly_push.js
t=10–20s Shelly joins "PZEM_Logger", gets IP 192.168.4.2
t=11–21s First HTTP POST arrives at ESP32 /api/shelly_push
t=11–21s ESP32 LED → GREEN (slow blink, 1 Hz) — system healthy

Any time Phone joins "PZEM_Logger", browses to http://192.168.4.1
         Live data visible immediately
```

The **LED is the system health indicator**:

| LED pattern | Meaning |
|---|---|
| Fast red blink (5 Hz) | SD error, or Shelly not yet connected / watchdog timeout |
| Slow green blink (1 Hz) | All systems nominal — Shelly pushing data, SD healthy |

The Shelly watchdog triggers if no push arrives for `3 × 1000 ms = 3 seconds`. This catches Wi-Fi dropouts, Shelly reboots, or script crashes without requiring any manual intervention — the LED will turn red, and go green again automatically when pushes resume.

---

*Part of the BBRAUN Shelly Power Logger project — firmware v1 · contact: jan.pfrang@delonghigroup.com*
