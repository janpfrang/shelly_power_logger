# ADC Bringup Firmware — Calibration Guide

## Overview

The `ADC_Bringup.ino` firmware is a minimal bring-up tool for characterizing the 9 V supply rail via GPIO 35 (ADC1_CH7). It measures the voltage divider ratio and detects mains loss for shutdown detection (Req 13).

## Hardware Setup

Assuming a standard 9 V → 3.3 V voltage divider:

```
9V Rail ──┬─ R1 ──┬─ GND
          │       │
          R2      └─ GPIO 35 (ADC input, max 3.3 V)
          │
         GND
```

### Example dividers:
- **10 kΩ + 30 kΩ**: V_ADC = V_rail × 1/4 (9 V → 2.25 V)
- **10 kΩ + 20 kΩ**: V_ADC = V_rail × 1/3 (9 V → 3 V)

## Flashing the Firmware

### Option 1: Via Arduino IDE
1. Open `ADC_Bringup.ino` in Arduino IDE
2. Select **Board:** ESP32-WROOM-32 (or your variant)
3. Select **Port:** Your USB/serial port
4. Sketch → Upload

### Option 2: Via OTA (from existing logger)
1. Ensure main logger is running and connected to `PZEM_Logger` AP
2. Compile `ADC_Bringup.ino` → Arduino IDE: Sketch → Export Compiled Binary
3. Navigate to `http://braun_PZEM.local/update` (or `192.168.4.1/update`)
4. Select the `.bin` file and upload
5. Device reboots into ADC monitoring mode

## Calibration Procedure

### Step 1: Initial Reading (VDIV_RATIO = 1.0)

1. Flash the firmware with `VDIV_RATIO = 1.0f` (default)
2. Open serial monitor: **115200 baud**
3. You should see output like:
   ```
   [Setup] ADC configured: PIN=35, resolution=12-bit, ref=3.30 V
   [Setup] VDIV_RATIO=1.00, SHUTDOWN_THRESHOLD=5.50 V
   [Setup] Starting ADC monitoring...
   
   [  1234] ADC=2048  V_rail=2.09 V  Status=OK   Uptime=1 s
   [  2345] ADC=2055  V_rail=2.10 V  Status=OK   Uptime=2 s
   [  3456] ADC=2062  V_rail=2.11 V  Status=OK   Uptime=3 s
   ```

### Step 2: Measure Actual Voltage

Using a **multimeter** (set to 20 V DC range):
1. Measure the voltage between the 9 V rail and GND
2. Note the value: **V_actual_measured = _____ V**
3. Record several readings to check stability:
   - Reading 1: _____ V
   - Reading 2: _____ V
   - Reading 3: _____ V
   - Average: **V_measured = _____ V**

Also note:
- ADC raw count from serial output: **ADC_raw = _____**
- Calculated voltage from firmware: **V_calc = _____ V**

### Step 3: Calculate Correct VDIV_RATIO

```
VDIV_RATIO = V_measured / V_calc

Example:
  V_measured = 8.95 V  (from multimeter)
  V_calc = 2.98 V      (from serial output with VDIV_RATIO=1.0)
  VDIV_RATIO = 8.95 / 2.98 = 3.00
```

### Step 4: Update and Re-flash

1. Edit `ADC_Bringup.ino`:
   ```cpp
   #define VDIV_RATIO  3.00f    // ← Update with your calculated value
   ```
2. Re-compile and flash (Arduino IDE or OTA)
3. Verify that the reported voltage now matches your multimeter reading

### Step 5: Validate Under Load

1. Turn on your test appliance (e.g., 230 V kettle through the Shelly)
2. Monitor voltage variation:
   - Does it drop under load?
   - By how much?
   - At what load level?
3. Record typical operating range:
   - **V_max (idle): _____ V**
   - **V_min (full load): _____ V**
   - **V_droop = V_max − V_min: _____ V**

This data informs the **SHUTDOWN_THRESHOLD_V** setting.

## Threshold Tuning

The firmware detects brownout when:
```
V_rail < SHUTDOWN_THRESHOLD_V  AND  V_rail < (SHUTDOWN_THRESHOLD_V − HYSTERESIS)
```

Typical values:
- **Nominal mains:** 230 V → 9 V supply ≈ **8.5–9.5 V**
- **Brownout detection:** Set threshold **~5.5 V** (requires ~2.5 s to activate with typical capacitors)
- **Hysteresis:** 0.3 V (prevents chatter on marginal supplies)

### Recommended approach:
1. Measure your mains voltage: **V_mains = _____ V**
2. Calculate 9 V output under typical conditions
3. Set SHUTDOWN_THRESHOLD_V to ~60–70% of nominal:
   - If nominal 9 V → set threshold to 5.5–6.0 V
4. Test with a variable transformer if available

## Web Interface Monitoring

Once flashed, you can monitor ADC via:

### Browser (visual dashboard):
```
http://braun_PZEM.local/   or   http://192.168.4.1/
```

### JSON API (programmatic):
```bash
curl http://192.168.4.1/adc
```

Response:
```json
{
  "adc_raw": 2062,
  "voltage_v": 8.95,
  "vdiv_ratio": 3.0,
  "is_shutdown": false,
  "threshold_v": 5.5
}
```

## Debugging

### Voltage reading too high or too low?

Check your voltage divider resistors:
```
Measured voltage × 1000 / ADC raw count ≈ expected VDIV_RATIO
```

### Noisy ADC reading?

- Increase `ADC_SAMPLES` (currently 16) to smooth noise
- Add a 100 nF capacitor across the ADC input and GND
- Reduce `ADC_SAMPLE_INTERVAL_MS` if you're logging too frequently

### Can't flash via OTA?

- Ensure you're connected to `PZEM_Logger` Wi-Fi
- Try `192.168.4.1/update` instead of the mDNS name
- Check that the `.bin` file is from **Sketch → Export Compiled Binary** (not .elf)

## Integration with Main Firmware

Once calibrated, the VDIV_RATIO and SHUTDOWN_THRESHOLD_V values can be:

1. **Copied to Config.h** in the main logger:
   ```cpp
   #define VDIV_RATIO             3.0f    // from calibration
   #define SHUTDOWN_THRESHOLD_V   5.5f
   ```

2. **Implemented in Logger.h** to detect brownout and auto-flush SD before shutdown:
   ```cpp
   void checkMainsVoltage() {
     float v = readAndCalcVoltage();
     if (v < SHUTDOWN_THRESHOLD_V) {
       flushToSD();  // save buffered data before capacitor depletes
       setOk(false); // trigger LED error blink
     }
   }
   ```

3. **Called from loop()** every 100–500 ms (not every 1 s polling interval).

## Reference Values

For a typical **9 V supply with 10 kΩ + 30 kΩ divider**:

| Supply V | ADC count | V_adc | Divider |
|----------|-----------|-------|---------|
| 5.0      | 1016      | 1.03  | 4.85    |
| 5.5      | 1119      | 1.14  | 4.82    |
| 7.5      | 1525      | 1.55  | 4.84    |
| 8.5      | 1730      | 1.76  | 4.83    |
| 9.0      | 1833      | 1.87  | 4.82    |
| 9.5      | 1935      | 1.97  | 4.82    |

*(Assuming 3.3 V ADC ref and 12-bit resolution)*

---

## Next Steps

After validation:
1. Document your measured VDIV_RATIO
2. Update main firmware Config.h
3. Implement mains-loss shutdown in Logger.h (Req 13)
4. Test with appliance under various supply conditions
5. Flash main logger back via OTA using `/update` endpoint
