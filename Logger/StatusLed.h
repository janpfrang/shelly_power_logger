/*
 * StatusLed.h v5  (Shelly + ESP32)
 * ==================================
 *
 * Functionally IDENTICAL to v4.
 * The only change is that logger.ok() now evaluates
 * shellyOk() && sdOk() instead of pzemOk() && sdOk() —
 * that logic lives in Logger.h, not here.
 *
 * LED blink patterns (Req 15, Req 16):
 *   LED_OK    → 1 Hz  (500 ms toggle) — system healthy
 *   LED_ERROR → 5 Hz  (100 ms toggle) — SD error OR Shelly watchdog timeout
 */

#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <Arduino.h>
#include "Config.h"

enum LedState {
  LED_OK,
  LED_ERROR
};

class StatusLed {
public:
  void begin() {
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
    _state      = LED_OK;
    _lastToggle = millis();
    _ledOn      = false;
  }

  // setState() only resets the blink phase on a *real* state change.
  // Calling it every loop() iteration is safe and idempotent.
  void setState(LedState s) {
    if (s == _state) return;
    _state      = s;
    _lastToggle = millis();
    _ledOn      = true;
    digitalWrite(PIN_LED, HIGH);
  }

  void setOk(bool ok) { setState(ok ? LED_OK : LED_ERROR); }

  LedState getState() const { return _state; }

  void update() {
    uint32_t interval = (_state == LED_OK) ? INTERVAL_LED_OK_MS
                                           : INTERVAL_LED_ERR_MS;
    uint32_t now = millis();
    if (now - _lastToggle >= interval) {
      _lastToggle = now;
      _ledOn      = !_ledOn;
      digitalWrite(PIN_LED, _ledOn ? HIGH : LOW);
    }
  }

private:
  LedState _state;
  uint32_t _lastToggle;
  bool     _ledOn;
};

#endif  // STATUS_LED_H
