/*
 * test_shelly_client.cpp
 * ──────────────────────
 * Unit tests for ShellyClient::ingest() and ShellyClient::shellyOk().
 */

// Arduino.h → tests/Arduino.h (stub, found first via CMake BEFORE include path)
// ArduinoJson.h → tests/ArduinoJson.h (nlohmann-backed stub)
// Config.h → tests/Config.h (stub constants)
#include "Arduino.h"
#include "ArduinoJson.h"
#include "Config.h"   // stub version (tests/Config.h) — must come before the guard
#define CONFIG_H      // now block the real Config.h from loading inside ShellyClient.h
#include "../Logger/ShellyClient.h"

#include <cassert>
#include <cstdio>

static int _tests_run = 0, _tests_failed = 0;

#define CHECK(expr) do { \
  _tests_run++; \
  if (!(expr)) { fprintf(stderr, "  FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); _tests_failed++; } \
} while(0)

#define TEST(name) static void name()

static std::string valid_json(float v=230.0f, float p=100.0f, float i=0.44f, float pf=0.99f) {
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"ts\":1000,\"v\":%.2f,\"p\":%.2f,\"i\":%.3f,\"pf\":%.2f}", v, p, i, pf);
  return buf;
}

TEST(test_valid_payload_accepted) {
  ShellyClient sc;
  bool ok = sc.ingest(valid_json(230.0f, 150.0f, 0.65f, 0.98f));
  CHECK(ok == true);
  CHECK(sc.hasData() == true);
  CHECK(sc.getErrorCount() == 0);
  CHECK(fabsf(sc.getVoltage()    - 230.0f) < 0.05f);
  CHECK(fabsf(sc.getPower()      - 150.0f) < 0.05f);
  CHECK(fabsf(sc.getPfApparent() - 0.98f)  < 0.01f);
}

TEST(test_malformed_json_rejected) {
  ShellyClient sc;
  CHECK(sc.ingest("{not valid json") == false);
  CHECK(sc.hasData() == false);
  CHECK(sc.getErrorCount() == 1);
}

TEST(test_missing_field_v_rejected) {
  ShellyClient sc;
  CHECK(sc.ingest("{\"ts\":1,\"p\":100.0,\"i\":0.44,\"pf\":0.99}") == false);
  CHECK(sc.getErrorCount() == 1);
}

TEST(test_missing_field_pf_rejected) {
  ShellyClient sc;
  CHECK(sc.ingest("{\"ts\":1,\"v\":230.0,\"p\":100.0,\"i\":0.44}") == false);
  CHECK(sc.getErrorCount() == 1);
}

TEST(test_voltage_out_of_range_high) {
  ShellyClient sc;
  CHECK(sc.ingest(valid_json(301.0f, 100.0f, 0.44f, 0.99f)) == false);
  CHECK(sc.getErrorCount() == 1);
}

TEST(test_voltage_negative_rejected) {
  ShellyClient sc;
  CHECK(sc.ingest(valid_json(-1.0f, 100.0f, 0.44f, 0.99f)) == false);
}

TEST(test_power_out_of_range_high) {
  ShellyClient sc;
  CHECK(sc.ingest(valid_json(230.0f, 3681.0f, 16.0f, 1.0f)) == false);
  CHECK(sc.getErrorCount() == 1);
}

TEST(test_current_out_of_range_high) {
  ShellyClient sc;
  CHECK(sc.ingest(valid_json(230.0f, 100.0f, 16.1f, 0.99f)) == false);
}

TEST(test_pf_just_above_limit_rejected) {
  ShellyClient sc;
  CHECK(sc.ingest(valid_json(230.0f, 100.0f, 0.44f, 1.02f)) == false);
}

TEST(test_pf_boundary_accepted) {
  ShellyClient sc;
  CHECK(sc.ingest(valid_json(230.0f, 100.0f, 0.44f, 1.00f)) == true);
}

TEST(test_all_zero_standby_accepted) {
  ShellyClient sc;
  bool ok = sc.ingest(valid_json(230.5f, 0.0f, 0.0f, 0.0f));
  CHECK(ok == true);
  CHECK(fabsf(sc.getPower()) < 0.01f);
}

TEST(test_error_count_resets_after_valid) {
  ShellyClient sc;
  sc.ingest("{bad}");
  sc.ingest("{bad}");
  CHECK(sc.getErrorCount() == 2);
  sc.ingest(valid_json());
  CHECK(sc.getErrorCount() == 0);
}

TEST(test_watchdog_false_before_first_push) {
  stub_set_millis(0);
  ShellyClient sc;
  CHECK(sc.shellyOk() == false);
}

TEST(test_watchdog_true_immediately_after_push) {
  stub_set_millis(1000);
  ShellyClient sc;
  sc.ingest(valid_json());
  CHECK(sc.shellyOk() == true);
}

TEST(test_watchdog_false_after_timeout) {
  stub_set_millis(0);
  ShellyClient sc;
  sc.ingest(valid_json());
  uint32_t timeout_ms = (uint32_t)SHELLY_ERROR_THRESHOLD * (uint32_t)INTERVAL_SHELLY_POLL_MS;
  stub_advance_millis(timeout_ms + 1);
  CHECK(sc.shellyOk() == false);
}

TEST(test_watchdog_recovers_after_new_push) {
  stub_set_millis(0);
  ShellyClient sc;
  sc.ingest(valid_json());
  stub_advance_millis(5000);
  CHECK(sc.shellyOk() == false);
  sc.ingest(valid_json());
  CHECK(sc.shellyOk() == true);
}

TEST(test_watchdog_exactly_at_boundary) {
  uint32_t timeout_ms = (uint32_t)SHELLY_ERROR_THRESHOLD * (uint32_t)INTERVAL_SHELLY_POLL_MS;

  stub_set_millis(0);
  ShellyClient sc;
  sc.ingest(valid_json());
  stub_advance_millis(timeout_ms);      // exactly at boundary → false (< not <=)
  CHECK(sc.shellyOk() == false);

  stub_set_millis(0);
  ShellyClient sc2;
  sc2.ingest(valid_json());
  stub_advance_millis(timeout_ms - 1);  // one ms before → true
  CHECK(sc2.shellyOk() == true);
}

int main() {
  printf("=== ShellyClient unit tests ===\n");
  test_valid_payload_accepted();
  test_malformed_json_rejected();
  test_missing_field_v_rejected();
  test_missing_field_pf_rejected();
  test_voltage_out_of_range_high();
  test_voltage_negative_rejected();
  test_power_out_of_range_high();
  test_current_out_of_range_high();
  test_pf_just_above_limit_rejected();
  test_pf_boundary_accepted();
  test_all_zero_standby_accepted();
  test_error_count_resets_after_valid();
  test_watchdog_false_before_first_push();
  test_watchdog_true_immediately_after_push();
  test_watchdog_false_after_timeout();
  test_watchdog_recovers_after_new_push();
  test_watchdog_exactly_at_boundary();
  printf("\n%d tests run, %d failed\n", _tests_run, _tests_failed);
  return _tests_failed == 0 ? 0 : 1;
}
