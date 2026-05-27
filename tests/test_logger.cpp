/*
 * test_logger.cpp — Logger power threshold and buffer overflow tests
 */

#include "Arduino.h"
#include "ArduinoJson.h"
#define CONFIG_H
#define SHELLY_CLIENT_H

#include <cassert>
#include <cstdio>
#include <cmath>

static int _tests_run = 0, _tests_failed = 0;
#define CHECK(expr) do { \
  _tests_run++; \
  if (!(expr)) { fprintf(stderr, "  FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); _tests_failed++; } \
} while(0)
#define TEST(name) static void name()

// ── ShellyClient double ───────────────────────────────────────────────────────
class ShellyClient {
public:
  float _v=230.0f, _p=100.0f, _pf=0.99f;
  bool _ok=true, _hasData=true;
  void set(float v, float p, float pf) { _v=v; _p=p; _pf=pf; _hasData=true; }
  void setOk(bool ok) { _ok=ok; }
  float getVoltage()    const { return _v; }
  float getPower()      const { return _p; }
  float getPfApparent() const { return _pf; }
  bool  shellyOk()      const { return _ok; }
  bool  hasData()       const { return _hasData; }
  uint8_t getErrorCount() const { return 0; }
};

// ── File stub (all methods no-ops; always evaluates to false) ─────────────────
struct File {
  explicit operator bool() const { return false; }
  template<typename... A> void printf(const char*, A...) {}
  template<typename T>    void println(T) {}
  void flush() {}
  void close() {}
};

// ── SPIClass stub (takes optional int channel in constructor) ─────────────────
struct SPIClass {
  explicit SPIClass(int = 0) {}
  void begin(int,int,int,int) {}
  void end() {}
};
#define VSPI 0

// ── SD stub — always reports not initialised ──────────────────────────────────
struct _SDClass {
  bool begin(int, SPIClass&, int) { return false; }
  void end() {}
  bool exists(const char*) { return false; }
  bool remove(const char*) { return false; }
  File open(const char*, int) { return File{}; }
} SD;
#define FILE_APPEND 0
#define FILE_WRITE  1
#define FILE_READ   2

// ── Config constants ──────────────────────────────────────────────────────────
#define LOG_FILE_PATH    "/log.csv"
#define LOG_FILE_HEADER  "time_ms,voltage_V,power_W,pf_apparent"
#define RAM_BUFFER_SIZE  64
#define INTERVAL_SD_FLUSH_MS    10000
#define DEFAULT_POWER_THRESHOLD_W  0.0f
#define INTERVAL_SHELLY_POLL_MS    1000
#define SHELLY_ERROR_THRESHOLD     3
#define PIN_SD_CS   25
#define PIN_SD_MOSI 14
#define PIN_SD_CLK  27
#define PIN_SD_MISO 26

#include "../Logger/Logger.h"

static void advance_past_interval(Logger& l) {
  stub_advance_millis(l.getPollInterval() + 1);
}

// ─────────────────────────────────────────────────────────────────────────────
TEST(test_threshold_zero_logs_everything) {
  stub_set_millis(0);
  ShellyClient sc; Logger logger(sc);
  logger.setPowerThreshold(0.0f);
  sc.set(230.0f, 0.1f, 0.99f);
  advance_past_interval(logger); logger.pollIfDue();
  CHECK(logger.getBufferCount() == 1);
}

TEST(test_below_threshold_not_logged) {
  stub_set_millis(0);
  ShellyClient sc; Logger logger(sc);
  logger.setPowerThreshold(10.0f);
  sc.set(230.0f, 9.9f, 0.99f);
  advance_past_interval(logger); logger.pollIfDue();
  CHECK(logger.getBufferCount() == 0);
}

TEST(test_exactly_at_threshold_logged) {
  stub_set_millis(0);
  ShellyClient sc; Logger logger(sc);
  logger.setPowerThreshold(10.0f);
  sc.set(230.0f, 10.0f, 0.99f);
  advance_past_interval(logger); logger.pollIfDue();
  CHECK(logger.getBufferCount() == 1);
}

TEST(test_live_cache_updated_even_below_threshold) {
  stub_set_millis(0);
  ShellyClient sc; Logger logger(sc);
  logger.setPowerThreshold(50.0f);
  sc.set(230.0f, 5.0f, 0.95f);
  advance_past_interval(logger); logger.pollIfDue();
  CHECK(logger.getBufferCount() == 0);
  CHECK(fabsf(logger.getLastPower() - 5.0f) < 0.1f);
}

TEST(test_no_sample_when_shelly_not_ok) {
  stub_set_millis(0);
  ShellyClient sc; sc.setOk(false);
  Logger logger(sc);
  advance_past_interval(logger); logger.pollIfDue();
  CHECK(logger.getBufferCount() == 0);
  CHECK(std::isnan(logger.getLastPower()));
}

TEST(test_buffer_fills_to_capacity) {
  stub_set_millis(0);
  ShellyClient sc; Logger logger(sc);
  logger.setPowerThreshold(0.0f);
  sc.set(230.0f, 100.0f, 0.99f);
  for (size_t k = 0; k < RAM_BUFFER_SIZE; k++) {
    stub_advance_millis(logger.getPollInterval() + 1);
    logger.pollIfDue();
  }
  CHECK(logger.getBufferCount() == RAM_BUFFER_SIZE);
  CHECK(logger.getDroppedSamples() == 0);
}

TEST(test_overflow_increments_dropped_counter) {
  stub_set_millis(0);
  ShellyClient sc; Logger logger(sc);
  logger.setPowerThreshold(0.0f);
  sc.set(230.0f, 100.0f, 0.99f);
  for (size_t k = 0; k < RAM_BUFFER_SIZE; k++) {
    stub_advance_millis(logger.getPollInterval() + 1);
    logger.pollIfDue();
  }
  stub_advance_millis(logger.getPollInterval() + 1);
  logger.pollIfDue();
  CHECK(logger.getDroppedSamples() == 1);
  CHECK(logger.getBufferCount() == RAM_BUFFER_SIZE);
}

TEST(test_multiple_overflows_counted) {
  stub_set_millis(0);
  ShellyClient sc; Logger logger(sc);
  logger.setPowerThreshold(0.0f);
  sc.set(230.0f, 100.0f, 0.99f);
  size_t extra = 5;
  for (size_t k = 0; k < RAM_BUFFER_SIZE + extra; k++) {
    stub_advance_millis(logger.getPollInterval() + 1);
    logger.pollIfDue();
  }
  CHECK(logger.getDroppedSamples() == extra);
  CHECK(logger.getBufferCount() == RAM_BUFFER_SIZE);
}

TEST(test_set_poll_interval_valid) {
  ShellyClient sc; Logger logger(sc);
  logger.setPollInterval(2000);
  CHECK(logger.getPollInterval() == 2000);
}

TEST(test_set_poll_interval_below_minimum_rejected) {
  ShellyClient sc; Logger logger(sc);
  uint32_t original = logger.getPollInterval();
  logger.setPollInterval(500);
  CHECK(logger.getPollInterval() == original);
}

TEST(test_set_power_threshold_roundtrip) {
  ShellyClient sc; Logger logger(sc);
  logger.setPowerThreshold(25.5f);
  CHECK(fabsf(logger.getPowerThreshold() - 25.5f) < 0.01f);
}

int main() {
  printf("=== Logger unit tests ===\n");
  test_threshold_zero_logs_everything();
  test_below_threshold_not_logged();
  test_exactly_at_threshold_logged();
  test_live_cache_updated_even_below_threshold();
  test_no_sample_when_shelly_not_ok();
  test_buffer_fills_to_capacity();
  test_overflow_increments_dropped_counter();
  test_multiple_overflows_counted();
  test_set_poll_interval_valid();
  test_set_poll_interval_below_minimum_rejected();
  test_set_power_threshold_roundtrip();
  printf("\n%d tests run, %d failed\n", _tests_run, _tests_failed);
  return _tests_failed == 0 ? 0 : 1;
}
