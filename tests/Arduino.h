#pragma once
/*
 * arduino_stub.h
 * ──────────────
 * Minimal shim that lets Arduino .h files compile as native C++ on the host.
 * Only covers what ShellyClient.h, Logger.h, and StatusLed.h actually use.
 * Add more stubs here as new headers are pulled in by future unit tests.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

// ── millis() ─────────────────────────────────────────────────────────────────
// Tests control time by calling stub_set_millis() before exercising code.
static uint32_t _stub_millis = 0;
inline uint32_t millis()               { return _stub_millis; }
inline void     stub_set_millis(uint32_t ms) { _stub_millis = ms; }
inline void     stub_advance_millis(uint32_t ms) { _stub_millis += ms; }

// ── String (Arduino String → std::string alias) ───────────────────────────────
using String = std::string;

// ── NAN, isnan ────────────────────────────────────────────────────────────────
// <cmath> provides NAN and isnan() — nothing extra needed.

// ── Serial (no-op) ────────────────────────────────────────────────────────────
struct _SerialStub {
  template<typename... A> void printf(const char*, A...) {}
  template<typename T>    void println(T)                {}
  template<typename T>    void print(T)                  {}
};
static _SerialStub Serial;

// ── GPIO no-ops (StatusLed needs these) ───────────────────────────────────────
#define INPUT_PULLUP   0
#define INPUT          1
#define OUTPUT         2
#define HIGH           1
#define LOW            0
inline void pinMode(uint8_t, uint8_t)     {}
inline void digitalWrite(uint8_t, uint8_t) {}

// ── delay() no-op ─────────────────────────────────────────────────────────────
inline void delay(uint32_t) {}

// ── Arduino.h guard ───────────────────────────────────────────────────────────
#define Arduino_h   // prevent accidental double-include if a header checks this
