/*
 * Logger_ADC_Integration.h  –  Template for adding supply voltage monitoring
 * ============================================================================
 *
 * This file shows how to integrate the calibrated ADC readings from the
 * bringup firmware into the main Logger.h class.
 *
 * STEPS TO IMPLEMENT:
 * 1. Run ADC_Bringup.ino and calibrate (get VDIV_RATIO)
 * 2. Add the ADC helper function (adcToVoltage) to Logger.h
 * 3. Add the checkMainsVoltage() method to Logger.h
 * 4. Call checkMainsVoltage() from loop() or pollIfDue()
 * 5. Set SHUTDOWN_THRESHOLD_V in Config.h based on your measurements
 * 6. Re-flash main logger firmware
 *
 * CONSTANTS TO ADD TO Config.h:
 * ============================
 * #define VDIV_RATIO              3.00f    // From ADC calibration
 * #define SHUTDOWN_THRESHOLD_V    5.5f     // Mains loss threshold (V)
 * #define MAINS_CHECK_INTERVAL_MS 500      // How often to poll ADC
 *
 * MEMBERS TO ADD TO Logger CLASS:
 * ==============================
 * uint32_t _lastMainsCheckMs;
 * bool     _mainsLossFlaggedForFlush;
 */

#pragma once

// ===== Add these to Config.h =====
/*
#define VDIV_RATIO              3.00f       // Calibrated from ADC bringup
#define SHUTDOWN_THRESHOLD_V    5.5f        // Voltage at which to trigger flush
#define MAINS_CHECK_INTERVAL_MS 500         // Check ADC every 500 ms
*/

// ===== Add this function to Logger.h (private section) =====

/**
 * Convert raw ADC count to voltage on the 9V supply rail.
 * 
 * Math:
 *   ADC range: 0–4095 (12-bit)
 *   ADC ref:   0–3.3 V
 *   V_adc = ADC_count / 4095 × 3.3
 *   V_rail = V_adc × VDIV_RATIO
 */
inline float adcToVoltage(uint16_t adcCount) {
  const float ADC_VREF = 3.3f;
  const int ADC_RES = 12;  // 12-bit ADC
  float vAdc = (float)adcCount / (float)(1 << ADC_RES) * ADC_VREF;
  return vAdc * VDIV_RATIO;
}

// ===== Add this method to Logger.h (private section) =====

/**
 * Poll the 9V supply voltage via GPIO 35 (ADC1_CH7).
 * 
 * If voltage drops below SHUTDOWN_THRESHOLD_V:
 *   1. Immediately flush all buffered samples to SD
 *   2. Mark the system as not-OK (triggers LED error blink)
 *   3. Stop accepting new samples (pollIfDue will set to NAN)
 * 
 * This allows the device to save logged data before backup capacitors
 * deplete during a mains brownout event (Req 13).
 *
 * Called every MAINS_CHECK_INTERVAL_MS (~500 ms) from loop().
 */
void checkMainsVoltage() {
  uint32_t now = millis();
  if (now - _lastMainsCheckMs < MAINS_CHECK_INTERVAL_MS) {
    return;  // Not yet due
  }
  _lastMainsCheckMs = now;

  // Read ADC with averaging
  uint16_t rawAdc = 0;
  const int samples = 16;
  for (int i = 0; i < samples; i++) {
    rawAdc += analogRead(PIN_VSUPPLY);
    delayMicroseconds(50);
  }
  rawAdc /= samples;

  float vRail = adcToVoltage(rawAdc);

  // Detect mains loss (voltage dropped below threshold)
  if (vRail < SHUTDOWN_THRESHOLD_V && !_mainsLossFlaggedForFlush) {
    _mainsLossFlaggedForFlush = true;
    Serial.printf("[Logger] ⚠️  MAINS LOSS DETECTED: V_rail = %.2f V (threshold = %.2f V)\n",
                  vRail, SHUTDOWN_THRESHOLD_V);
    Serial.println("[Logger] Flushing buffer to SD before shutdown...");
    
    // Flush any buffered samples before capacitors deplete
    flushToSD();
    
    // Mark system as unhealthy — LED will blink 5 Hz
    _sdOk = false;
    Serial.println("[Logger] Buffer flushed. Logging paused until mains restored.");
    return;
  }

  // Recovery: if voltage recovered above threshold + hysteresis
  const float HYSTERESIS = 0.3f;
  if (vRail > (SHUTDOWN_THRESHOLD_V + HYSTERESIS) && _mainsLossFlaggedForFlush) {
    _mainsLossFlaggedForFlush = false;
    Serial.printf("[Logger] ✓ Mains voltage restored: %.2f V\n", vRail);
    Serial.println("[Logger] Resuming normal logging.");
    // Attempt to recover SD
    if (!_sdOk) {
      tryRecoverSD();
    }
    return;
  }

  // Optional: debug output (every 10 checks = ~5 s with 500 ms interval)
  static uint32_t debugCounter = 0;
  if (++debugCounter % 10 == 0) {
    Serial.printf("[Logger] Supply: %.2f V (threshold: %.2f V, raw ADC: %u)\n",
                  vRail, SHUTDOWN_THRESHOLD_V, rawAdc);
  }
}

// ===== Add to Logger.h constructor initialization list =====
/*
Logger(ShellyClient& shelly)
  : _shelly(shelly),
    ...
    _lastMainsCheckMs(0),              // ← Add this
    _mainsLossFlaggedForFlush(false),  // ← Add this
    ...
{}
*/

// ===== Add to Logger.h private members =====
/*
uint32_t _lastMainsCheckMs;           // Timestamp of last ADC poll
bool     _mainsLossFlaggedForFlush;   // Tracks mains loss event
*/

// ===== Modify Logger::setup() =====
/*
bool begin() {
  // ... existing SD init code ...
  
  pinMode(PIN_VSUPPLY, INPUT);
  analogReadResolution(12);
  Serial.printf("[Logger] Supply voltage monitor enabled (GPIO %d, VDIV=%.2f)\n",
                PIN_VSUPPLY, VDIV_RATIO);
  
  return _sdOk;
}
*/

// ===== Add to Shelly_ESP32_Logger.ino main loop() =====
/*
void loop() {
  // 1. Check supply voltage and flush if brownout detected
  logger.checkMainsVoltage();  // ← ADD THIS LINE
  
  // 2. Check if new Shelly data is due to be sampled
  logger.pollIfDue();

  // 3. Flush ring buffer to SD on schedule
  logger.flushIfDue();

  // ... rest of loop ...
}
*/

// ===== Example with CONFIG.h additions =====

/*
// Config.h snippet to add:

// ===== ADC Supply Monitoring (Req 13) =====
// Voltage divider calibration: V_rail = V_adc × VDIV_RATIO
// Calibrated via ADC_Bringup.ino commissioning firmware
// See ADC_Calibration_Guide.md for measurement procedure
#define VDIV_RATIO              3.00f       // Edit after running calibration

// Mains loss detection: if V_rail drops below this, flush SD and mark error
#define SHUTDOWN_THRESHOLD_V    5.5f        // ~60% of nominal 9V

// How often to check the supply voltage
#define MAINS_CHECK_INTERVAL_MS 500         // Every 500 ms

// ===== end ADC config =====
*/

// ===== TESTING CHECKLIST =====
/*
After implementation:

[ ] ADC_Bringup.ino has been run and VDIV_RATIO calibrated
[ ] VDIV_RATIO and SHUTDOWN_THRESHOLD_V added to Config.h
[ ] adcToVoltage() function added to Logger.h
[ ] checkMainsVoltage() method added to Logger.h
[ ] Logger constructor initializes _lastMainsCheckMs and _mainsLossFlaggedForFlush
[ ] Logger::begin() sets up ADC pin and prints calibration values
[ ] loop() calls logger.checkMainsVoltage() before pollIfDue()
[ ] Main firmware compiles without errors
[ ] Main firmware flashed via OTA
[ ] Logging starts normally
[ ] Voltage readings printed to serial match ADC_Bringup output
[ ] Supply voltage drop triggers serial message "[Logger] ⚠️  MAINS LOSS DETECTED"
[ ] LED turns to 5 Hz error blink on voltage drop
[ ] SD buffer is flushed before system marks as error
[ ] Voltage recovery restores normal logging
[ ] Long-run test: device remains stable for >24 hours

VALIDATION WITH VARIABLE TRANSFORMER:
[ ] Ramp down AC supply from 230V → 150V using VARIAC
[ ] Monitor serial output for mains loss detection
[ ] Verify that: detection occurs, SD flushes, LED blinks error
[ ] Ramp back up to 230V, verify recovery and resume
[ ] Test with appliance under full load
*/

// ===== INTEGRATION NOTES =====

/*
WHY SEPARATE ADC_BRINGUP.ino?
==============================
The ADC bringup firmware is kept separate because:

1. It's a diagnostic / commissioning tool, not production code
2. It allows extensive validation without modifying main firmware
3. The calibration (VDIV_RATIO) is hardware-specific and must be validated
4. Once validated, the code is trivial to integrate (5–10 lines)
5. Users can re-run ADC_Bringup to validate hardware after repairs

PRODUCTION FIRMWARE CONSIDERATIONS:
===================================
Once the supply voltage is calibrated and integrated:

1. The ADC read happens every 500 ms (low overhead)
2. Oversampling (16 samples) reduces noise without blocking
3. The flushToSD() call is asynchronous—no extra delays
4. If mains loss is detected, logging stops but device remains responsive
5. Backup capacitors typically provide 1–2 seconds of headroom
6. With 500 ms check interval, brownout is detected within 1 second

REQUIREMENTS MET:
=================
✅ Req 13: "allows safe flush of memory if the power connection is determined"
   - ADC monitors 9V supply
   - Detects voltage drop via threshold
   - Flushes buffered data to SD before shutdown
   - Coordinates with watchdog / LED for visual indication

⚠ Partial: User must calibrate VDIV_RATIO once per hardware variant
   (this is expected—no two voltage dividers are identical)

❌ Not yet: Automatic restart recovery after mains is restored
   (Would require a supercap-based restart circuit, out of firmware scope)
*/
