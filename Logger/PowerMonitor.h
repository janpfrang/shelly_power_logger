/*
 * PowerMonitor.h  -  9V-rail loss detection  (v3)
 * =================================================
 *
 * Changes vs. v2
 * --------------
 *   ADDED  _lastRailMv cached member -- updated every POWER_CHECK_INTERVAL_MS
 *          inside update().  Allows WebPortal and Logger to read the latest
 *          rail voltage without triggering an extra oversampled ADC burst.
 *   ADDED  getLastRailMilliVolts() -- returns _lastRailMv (0 when
 *          POWER_MONITOR_ENABLED == 0 or during startup grace).
 *
 * Changes vs. v1
 * --------------
 *   ADDED  POWER_MONITOR_ENABLED guard in update() and isPowerLost().
 *          When Config.h sets POWER_MONITOR_ENABLED = 0 (hardware circuit
 *          not yet populated), both methods become no-ops so the rest of
 *          the firmware is completely unaffected.
 *          Root cause of the WiFi-not-visible bug: GPIO 35 floated to 0 V
 *          when the divider resistors were absent, readRailMilliVolts()
 *          returned 0 mV (well below the 7350 mV threshold), and
 *          handlePowerLoss() was called within ~600 ms of boot, executing
 *          WiFi.mode(WIFI_OFF) before the softAP was visible to clients.
 *
 * PURPOSE  (Req 13: "allows safe flush of memory if the power connection
 * is determined / lost")
 * ----------------------------------------------------------------------
 * Watches the 9 V supply rail through the resistor divider on GPIO 35.
 * When mains is lost the supercap bank (2 x 1 F in series = 0.5 F) holds
 * the rail up for several seconds.  This module detects the decay early,
 * tells the sketch to flush the SD card, and then the sketch sheds load
 * and idles until the hardware brownout reset finishes the job.
 *
 * DESIGN SPLIT
 * ------------
 * PowerMonitor is a *detector only*.  It does NOT touch the SD card, Wi-Fi
 * or the LED -- that orchestration lives in the .ino (handlePowerLoss()),
 * exactly like the Logger / ShellyClient / WebPortal single-responsibility
 * split.  This keeps the threshold logic testable in isolation.
 *
 *   loop():  powerMonitor.update();
 *            if (powerMonitor.isPowerLost()) handlePowerLoss();   // terminal
 *
 * CIRCUIT
 * -------
 *     9V rail --[ R_TOP 180k ]--+--[ R_BOTTOM 47k ]-- GND
 *                               |
 *                            GPIO 35  (ADC1_CH7)
 *
 *   V_gpio = V_rail * R_BOTTOM / (R_TOP + R_BOTTOM) = V_rail * 0.2070
 *   readRailMilliVolts() measures the pin and multiplies back up so the
 *   rest of the firmware works in real 9V-rail millivolts.
 *
 * THRESHOLD  (7.35 V, not 6.8 V -- deliberate)
 * --------------------------------------------
 * Power chain:  9V rail -> TSR-1-2450 (->5V) -> ESP32 LDO (->3.3V) -> ADC Vref.
 * The TSR-1-2450 buck regulator drops out at ~6.5 V input; below that the
 * 3.3 V rail (and therefore the ADC reference) sag, so the ADC reading
 * becomes meaningless at exactly the moment we would need it.
 * Triggering at 7.35 V keeps the WHOLE worst-case ADC error window
 * (~+/-0.3 V) above the 6.5 V regulator cliff:
 *
 *     8.7 V  nominal
 *     7.75 V  <- POWER_THRESHOLD_HIGH_MV  (hysteresis clear / recover)
 *     7.35 V  <- POWER_THRESHOLD_LOW_MV   (trigger)
 *     7.07 V  worst-case late trigger  (still > cliff)  OK
 *     6.50 V  TSR-1-2450 cliff  -- ADC no longer trustworthy
 *     5.50 V  ESP32 brownout reset
 *
 * Capacitor budget (0.5 F, ~150 mA draw):
 *   mains loss -> 7.35 V : ~4.5 s   (detection window)
 *   7.35 V -> 6.5 V cliff : ~2.8 s   (clean action window, SD flush is <0.5 s)
 *   7.35 V -> 5.5 V brownout : ~6.2 s (total)
 *
 * ADC NON-LINEARITY HANDLING (no temperature sensor, by request)
 * --------------------------------------------------------------
 * Three independent error sources, handled as well as software allows:
 *
 *   1. Offset + INL  -> analogReadMilliVolts() applies the chip's factory
 *      eFuse calibration (per-unit offset + a piecewise curve correction).
 *      This is the single biggest win and needs no extra hardware.
 *      The working point (~1.5 V at the pin) sits in the ADC's most linear
 *      region (0.8-2.5 V), where residual INL is smallest.
 *
 *   2. WiFi noise on ADC1  -> oversampling.  POWER_ADC_SAMPLES reads are
 *      averaged; random noise falls ~1/sqrt(N) (N=16 -> ~4x reduction).
 *
 *   3. Transients / load spikes  -> majority voting.  POWER_MAJORITY_COUNT
 *      consecutive below-threshold checks are required before triggering,
 *      so a single SD-write or Wi-Fi burst dip cannot fire the shutdown.
 *
 * The 0.40 V hysteresis gap (7.35 / 7.75) is wider than the residual
 * error budget, which prevents chatter around the threshold.
 *
 * HARDWARE CAVEAT
 * ---------------
 * The 47 k bottom resistor is a fairly high source impedance for the ESP32
 * ADC sample-and-hold (it prefers < ~10-20 k).  This can add a small
 * settling offset.  The factory calibration + oversampling absorb most of
 * it, but if bench tests show a consistent bias, add a 100 nF capacitor
 * from GPIO 35 to GND -- it lowers the AC source impedance without changing
 * the (slow) mains-loss detection.
 *
 * STARTUP
 * -------
 * The supercaps take ~1-2 s to charge after power-on, during which the rail
 * sits below 7.35 V and would look exactly like a power loss.  begin()
 * starts a POWER_STARTUP_GRACE_MS window during which update() reads nothing
 * and never triggers.  Because the ESP32 bootloader already burned ~1-2 s
 * before setup() runs, the effective grace from cold power-on is ~4-5 s --
 * comfortably longer than the cap charge time.
 */

#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <Arduino.h>
#include "Config.h"

class PowerMonitor {
public:
  PowerMonitor()
    : _graceActive(true),
      _startupMs(0),
      _lastCheckMs(0),
      _belowCount(0),
      _powerLost(false),
      _lastRailMv(0)
  {}

  // Configure the ADC for the GPIO 35 divider tap.
  // Call once from setup() -- this also starts the startup grace clock.
  void begin() {
    analogReadResolution(12);                          // 0..4095
    analogSetPinAttenuation(PIN_VSUPPLY, ADC_11db);    // ~0..3.3 V usable range
    _startupMs   = millis();
    _graceActive = true;
    _belowCount  = 0;
    _powerLost   = false;
    Serial.printf("[Power] Init -- Trigger < %d mV, Recover > %d mV (9V-Rail), "
                  "Grace %d ms\n",
                  POWER_THRESHOLD_LOW_MV, POWER_THRESHOLD_HIGH_MV,
                  POWER_STARTUP_GRACE_MS);
  }

  // Call every loop() iteration.  Does real work only every
  // POWER_CHECK_INTERVAL_MS; cheap to call otherwise.
  // No-op when POWER_MONITOR_ENABLED == 0 (9V circuit not populated).
  void update() {
#if POWER_MONITOR_ENABLED == 0
    return;   // hardware circuit absent -- skip all ADC reads
#endif
    uint32_t now = millis();

    // Hold off while the supercaps charge after power-on.
    if (_graceActive) {
      if (now - _startupMs < POWER_STARTUP_GRACE_MS) return;
      _graceActive = false;
      Serial.println("[Power] Grace-Phase beendet -- Ueberwachung aktiv");
    }

    if (now - _lastCheckMs < POWER_CHECK_INTERVAL_MS) return;
    _lastCheckMs = now;

    uint32_t railMv = readRailMilliVolts();
    _lastRailMv = railMv;   // cache for getLastRailMilliVolts()

    // Hysteresis + majority voting.
    if (railMv < POWER_THRESHOLD_LOW_MV) {
      if (_belowCount < 255) _belowCount++;
      if (_belowCount >= POWER_MAJORITY_COUNT) {
        _powerLost = true;     // latched -- cleared only by reboot
        Serial.printf("[Power] Schwelle unterschritten: %lu mV "
                      "(%u/%u Messungen)\n",
                      (unsigned long)railMv,
                      (unsigned)_belowCount, (unsigned)POWER_MAJORITY_COUNT);
      }
    } else if (railMv > POWER_THRESHOLD_HIGH_MV) {
      _belowCount = 0;         // recovered above the upper band -> reset
    }
    // Dead-zone between LOW and HIGH: leave _belowCount unchanged.
  }

  // Latched: true once a sustained under-voltage has been confirmed.
  // Always returns false when POWER_MONITOR_ENABLED == 0.
  bool isPowerLost() const {
#if POWER_MONITOR_ENABLED == 0
    return false;
#else
    return _powerLost;
#endif
  }

  // Returns the most recently cached rail voltage in millivolts.
  // Updated every POWER_CHECK_INTERVAL_MS by update().
  // Returns 0 when POWER_MONITOR_ENABLED == 0 or during startup grace.
  uint32_t getLastRailMilliVolts() const {
#if POWER_MONITOR_ENABLED == 0
    return 0;
#else
    return _lastRailMv;
#endif
  }

  // Oversampled, factory-calibrated read of the 9V rail in millivolts.
  // Public so the shutdown idle loop can watch for mains recovery.
  uint32_t readRailMilliVolts() {
    uint32_t accMv = 0;
    for (uint8_t i = 0; i < POWER_ADC_SAMPLES; i++) {
      // analogReadMilliVolts() applies the eFuse calibration per sample,
      // linearising before we average -> better than averaging raw counts.
      accMv += analogReadMilliVolts(PIN_VSUPPLY);
      delayMicroseconds(POWER_ADC_SAMPLE_GAP_US);
    }
    uint32_t gpioMv = accMv / POWER_ADC_SAMPLES;       // mV at the pin

    // Undo the divider: V_rail = V_gpio * (R_TOP + R_BOTTOM) / R_BOTTOM.
    // 64-bit intermediate avoids overflow (1800 * 227000 fits in 32-bit,
    // but the cast keeps it safe if the resistors are ever changed bigger).
    uint32_t railMv = (uint32_t)(((uint64_t)gpioMv *
                       (DIVIDER_R_TOP_OHM + DIVIDER_R_BOTTOM_OHM)) /
                        DIVIDER_R_BOTTOM_OHM);
    return railMv;
  }

private:
  bool     _graceActive;   // true during the post-boot startup window
  uint32_t _startupMs;     // millis() at begin()
  uint32_t _lastCheckMs;   // last time the rail was sampled
  uint8_t  _belowCount;    // consecutive below-threshold checks
  bool     _powerLost;     // latched trigger flag
  uint32_t _lastRailMv;    // cached result of last readRailMilliVolts() call
};

#endif  // POWER_MONITOR_H
