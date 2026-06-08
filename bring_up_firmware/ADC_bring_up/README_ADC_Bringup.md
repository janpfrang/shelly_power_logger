# ESP32 ADC Bringup: 9V Supply Voltage Monitoring

Complete toolkit for commissioning and validating ADC-based mains loss detection on the Shelly ESP32 Logger.

## 📋 Contents

This package includes three components:

1. **`ADC_Bringup.ino`** – Minimal firmware for ADC characterization
2. **`ADC_Calibration_Guide.md`** – Step-by-step calibration instructions
3. **`adc_analysis.py`** – Python tool for analyzing measurements and calculating VDIV_RATIO

## 🎯 Purpose

Implements **Requirement 13 (Supply Monitoring)** from the Shelly ESP32 Logger spec:

> *"allows safe flush of memory if the power connection is determined"*

When mains voltage drops below a safe threshold, the ESP32 must:
- Detect the voltage drop via ADC on GPIO 35
- Flush buffered samples to SD card before backup capacitors deplete
- Trigger LED error indication

## 🔧 Quick Start (5 minutes)

### 1. Flash ADC Bringup Firmware

**Via Arduino IDE:**
```bash
# Compile and upload ADC_Bringup.ino
# Board: ESP32-WROOM-32 | Port: COM3 (or your port)
```

**Via OTA (from existing logger):**
```
1. Open http://braun_PZEM.local/update
2. Select ADC_Bringup.ino exported binary
3. Upload → device reboots into ADC monitoring mode
```

### 2. Monitor Serial Output

```bash
# Open serial monitor (115200 baud)
# You should see:
[Setup] ADC configured: PIN=35, resolution=12-bit, ref=3.30 V
[Setup] VDIV_RATIO=1.00, SHUTDOWN_THRESHOLD=5.50 V
...
[  1234] ADC=2048  V_rail=2.09 V  Status=OK   Uptime=1 s
[  2345] ADC=2055  V_rail=2.10 V  Status=OK   Uptime=2 s
```

### 3. Measure & Calibrate

```bash
# With multimeter, measure actual 9V rail voltage
# E.g., measured = 8.95 V
# From serial: V_calc = 2.98 V

# Calculate VDIV_RATIO:
VDIV_RATIO = 8.95 / 2.98 = 3.00

# Use Python tool for automation:
python3 adc_analysis.py --serial-log output.txt --multimeter 8.95
```

### 4. Update Firmware

Edit `ADC_Bringup.ino`:
```cpp
#define VDIV_RATIO  3.00f    // Your calibrated value
```

Re-flash and verify new voltage matches multimeter.

## 📐 Hardware Setup

### Typical 9V → 3.3V Divider

```
┌─────────────────────────────────────┐
│         9V Power Rail               │
└──────────────┬──────────────────────┘
               │
            ┌──R1──┐
            │ 30kΩ │
            └───┬──┘
                │
                ├──── GPIO 35 (ADC1_CH7) ──── [Oscilloscope probe]
                │
            ┌───┴──┐
            │ 10kΩ │  ← R2
            │  R2  │
            └───┬──┘
                │
               GND
```

**Voltage divider formula:**
```
V_adc = V_rail × R2 / (R1 + R2)
V_rail = V_adc × (R1 + R2) / R2

For 30kΩ + 10kΩ:
V_rail = V_adc × 4.0
```

## 📊 Typical ADC Values

For a 12-bit ADC (0–4095 counts mapping to 0–3.3V):

| V_rail | V_adc  | ADC count | Notes                           |
|--------|--------|-----------|----------------------------------|
| 4.5    | 1.125  | 1398      | Early brownout (capacitor droop) |
| 5.5    | 1.375  | 1704      | Shutdown threshold              |
| 7.5    | 1.875  | 2324      | Typical brown-out event         |
| 8.5    | 2.125  | 2632      | Full-load operating condition  |
| 9.0    | 2.25   | 2790      | Nominal supply (no load)        |
| 10.0   | 2.5    | 3096      | Over-voltage (unlikely)         |

*Assumes VDIV_RATIO = 4.0*

## 🔄 Detailed Workflow

### Phase 1: Setup & Verification

```
┌─────────────────────────────────┐
│  ADC_Bringup.ino (VDIV=1.0)    │  ← Flash here
└────────────┬────────────────────┘
             │
             ↓
   ┌──────────────────────┐
   │ Read serial output   │  ← ADC=2048, V_rail=2.09 V
   │ (at least 60 s)      │
   └────────────┬─────────┘
                │
                ↓
   ┌──────────────────────┐
   │ Measure with         │  ← V_actual=8.95 V (multimeter)
   │ multimeter           │
   └────────────┬─────────┘
                │
                ↓
   ┌──────────────────────┐
   │ Calculate VDIV_RATIO │  ← 8.95 / 2.98 = 3.00
   │ = V_actual / V_calc  │
   └──────────┬───────────┘
```

### Phase 2: Calibration

```
┌──────────────────────────────┐
│ Update VDIV_RATIO in code    │  ← #define VDIV_RATIO 3.00f
└────────────┬─────────────────┘
             │
             ↓
┌──────────────────────────────┐
│ Re-flash ADC_Bringup.ino     │
└────────────┬─────────────────┘
             │
             ↓
┌──────────────────────────────┐
│ Verify: V_rail now matches   │  ← ✓ 8.95 V on serial
│ multimeter reading           │
└──────────────────────────────┘
```

### Phase 3: Threshold Tuning

```
┌──────────────────────────────┐
│ Connect variable transformer │  ← Ramp mains 230V → 180V
│ or VARIAC to AC supply       │
└────────────┬─────────────────┘
             │
             ↓
┌──────────────────────────────┐
│ Monitor voltage drop on      │  ← Watch 9V output sag
│ 9V rail (oscilloscope or     │     Note: V_min, time to drop
│ ADC readings)                │
└────────────┬─────────────────┘
             │
             ↓
┌──────────────────────────────┐
│ Set SHUTDOWN_THRESHOLD_V     │  ← E.g., 5.5 V (leaves margin)
│ = 0.6 × V_nominal            │     Shelly watchdog at 6s
└──────────────────────────────┘
```

### Phase 4: Integration

```
┌────────────────────────────────┐
│ Copy calibration values to:    │
│  • Config.h (VDIV_RATIO)       │
│  • Logger.h (checkMains())     │
│  • Implement flushToSD() call  │
└────────────┬───────────────────┘
             │
             ↓
┌────────────────────────────────┐
│ Flash main Shelly_ESP32_Logger │
│ firmware via OTA               │
└────────────┬───────────────────┘
             │
             ↓
┌────────────────────────────────┐
│ Validate:                      │
│  • Normal logging works        │
│  • Mains drop triggers flush   │
│  • LED shows error status      │
└────────────────────────────────┘
```

## 🐍 Python Analysis Tool

### Installation

```bash
# Install matplotlib (optional, for plots)
pip install matplotlib

# Or minimal install (no plots):
pip install adc_analysis.py  # if packaged
```

### Usage

#### Command-line mode:

```bash
# Basic analysis
python3 adc_analysis.py --serial-log serial_output.txt

# With multimeter calibration
python3 adc_analysis.py --serial-log serial_output.txt \
                        --multimeter 8.95 \
                        --current-vdiv 1.0

# Generate plot
python3 adc_analysis.py --serial-log serial_output.txt \
                        --multimeter 8.95 \
                        --plot
```

#### Interactive Python:

```python
from adc_analysis import ADCAnalysis

# Load and parse serial log
adc = ADCAnalysis('serial_output.txt')
adc.set_current_vdiv_ratio(1.0)

# Print statistics
adc.print_stats()

# Calibrate with multimeter reading
vdiv_optimal = adc.calibrate_with_multimeter(8.95)
# Output: ✓ Recommended VDIV_RATIO: 3.00

# Generate plot
adc.plot()
```

### Output Example

```
✓ Parsed 127 ADC readings from serial_output.txt
Current firmware VDIV_RATIO set to 1.0

📈 Statistics:
  Samples: 127
  
  ADC Raw Counts:
    Min: 2045
    Max: 2062
    Mean: 2052.1
    StDev: 4.2
    Range: 17
  
  Calculated Voltage (with VDIV=1.00):
    Min: 2.085 V
    Max: 2.105 V
    Mean: 2.096 V
    StDev: 0.004 V
    Range: 0.020 V

📊 Calibration Results:
  Multimeter reading: 8.950 V
  Firmware V_calc (median): 2.984 V
  ✓ Recommended VDIV_RATIO: 3.00

  Update ADC_Bringup.ino:
  #define VDIV_RATIO 3.00f

✓ Plot saved to adc_calibration.png
```

## 📝 Configuration Reference

### ADC_Bringup.ino

```cpp
// Calibration (adjust after initial measurement)
#define VDIV_RATIO  3.0f        // V_rail = V_adc × VDIV_RATIO

// Shutdown detection thresholds
#define SHUTDOWN_THRESHOLD_V    5.5f    // Mains loss threshold
#define SHUTDOWN_HYSTERESIS     0.3f    // Prevent chatter

// ADC tuning
#define ADC_SAMPLES             16      // Oversample for noise reduction
#define ADC_SAMPLE_INTERVAL_MS  1000    // Sample every 1 s
```

### Main Firmware Integration

Once calibrated, integrate into `Logger.h`:

```cpp
// In Logger.h private members:
uint32_t _lastMainsCheckMs = 0;

// In loop():
void checkMainsVoltage() {
  uint32_t now = millis();
  if (now - _lastMainsCheckMs < 500) return;  // Check every 500 ms
  _lastMainsCheckMs = now;
  
  uint16_t raw = analogRead(PIN_VSUPPLY);
  float v_rail = adcToVoltage(raw);
  
  if (v_rail < SHUTDOWN_THRESHOLD_V) {
    flushToSD();  // Dump buffered data before brownout
    _sdOk = false;  // Mark as error
    return;
  }
}

// Helper:
float adcToVoltage(uint16_t adc) {
  const float ADC_VREF = 3.3f;
  const int ADC_RES = 12;
  float v_adc = adc / (float)(1 << ADC_RES) * ADC_VREF;
  return v_adc * VDIV_RATIO;
}
```

## 🚨 Troubleshooting

### "Voltage reads too high"
- Check voltage divider resistor values with multimeter
- Verify no shorts or cold solder joints
- Try `VDIV_RATIO = measured / calculated` manually

### "Voltage is noisy/unstable"
- Increase `ADC_SAMPLES` (16 → 32)
- Add 100 nF ceramic capacitor (GPIO 35 to GND)
- Ensure good power supply decoupling

### "Can't flash via OTA"
- Use `http://192.168.4.1/update` instead of mDNS name
- Ensure `.bin` file from **Sketch → Export Compiled Binary**
- Check browser console for upload errors

### "Multimeter reads 9V but ADC shows 2.09V"
- This is **normal** with VDIV=1.0
- It means the voltage divider is 1:4
- Calculate: 9 / 4 = 2.25 V (close to 2.09, accounting for slight load)
- Set `VDIV_RATIO = 9 / 2.09 = 4.3` (or use the Python tool)

## 📚 References

- **ESP32 ADC reference:** https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/adc.html
- **GPIO 35 specs:** Analog input pin, 12-bit, 0–3.3 V
- **Req 13 (spec):** ADC_Calibration_Guide.md "Integration with Main Firmware"

## 📄 File Checklist

- [ ] `ADC_Bringup.ino` – Firmware sketch
- [ ] `ADC_Calibration_Guide.md` – Calibration instructions
- [ ] `adc_analysis.py` – Python analysis tool
- [ ] `README.md` – This file
- [ ] Serial output log (captured from device)
- [ ] Multimeter measurement record
- [ ] Calibrated VDIV_RATIO value (saved for main firmware update)

## ✅ Validation Checklist

After calibration, verify:

- [ ] Firmware flashes successfully via OTA
- [ ] Serial output shows stable ADC readings
- [ ] Calculated voltage matches multimeter (±0.1 V)
- [ ] Threshold detection works (ramp down supply, LED blinks 5 Hz)
- [ ] Main firmware compiles with calibrated values
- [ ] Logging continues after re-flashing main firmware
- [ ] SD flush occurs before mains drops below threshold

---

**Version:** 1.0  
**Last Updated:** 2026-06-08  
**Status:** ✅ Ready for commissioning
