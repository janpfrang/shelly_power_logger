# OTA Firmware Deployment Guide
## Shelly ESP32 Logger
 
---
 
## Building the firmware
 
### Option A — GitHub Actions (recommended, no USB needed)
1. Go to your repo on GitHub → **Actions** → **Build Firmware (manual)**
2. Click **Run workflow**, select your branch and board (`esp32:esp32:esp32` for WROOM)
3. Download the `.bin` artifact from the finished run — you need `Shelly_ESP32_Logger.ino.bin`
### Option B — Arduino IDE
Sketch → **Export Compiled Binary** → grab the `.bin` file (not the `.elf`)
 
---
 
## Flashing over the air
 
1. Connect your laptop/phone to the **`PZEM_Logger`** WiFi (password: `logger1234`)
2. Open a browser and go to **`http://192.168.4.1/update`** or **`http://braun_PZEM.local/update`**
3. Click **Choose File**, select the `.bin`
4. Click **Upload & Flash**
5. Watch the progress bar — the ESP32 reboots automatically on success and the page redirects back to `/` after 8 seconds
> The firmware flushes the SD log buffer before writing flash, so no logged data is lost during the update.
 
---
 
## What happens if it goes wrong
 
| Symptom | Cause | Action |
|---|---|---|
| Upload fails mid-way | Network dropout or wrong file | Old firmware stays in place — logging resumes automatically. Try again. |
| Page doesn't redirect after 8 s | Reboot cut the TCP connection before the response arrived — this is normal | Navigate manually to `http://192.168.4.1` |
| Can't reach the page at all | WiFi AP not up | Connect via USB, open Serial Monitor at 115200 baud, check boot log |
 
