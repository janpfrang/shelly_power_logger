/*
 * test_shelly_client.cpp
 * ──────────────────────
 * Unit tests for ShellyClient::ingest(), ShellyClient::shellyOk(),
 * and ShellyClient::ingestBatch() (added v3).
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

// Build a batch JSON string with n identical samples
static std::string make_batch(int n, float v=230.0f, float p=100.0f,
                               float i=0.44f, float pf=0.98f) {
  std::string s = "{\"batch\":[";
  for (int idx = 0; idx < n; idx++) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"ts\":%d,\"v\":%.1f,\"p\":%.1f,\"i\":%.3f,\"pf\":%.2f}",
             (idx + 1) * 1000, v, p, i, pf);
    s += buf;
    if (idx < n - 1) s += ",";
  }
  s += "]}";
  return s;
}

// ─── Existing ingest() tests (unchanged) ─────────────────────────────────────

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

// ─── New ingestBatch() tests ──────────────────────────────────────────────────

// Basic: valid 3-sample batch is accepted, count returned correctly
TEST(test_batch_valid_accepted) {
  ShellyClient sc;
  int n = sc.ingestBatch(make_batch(3));
  CHECK(n == 3);
}

// After a valid batch, hasData() must be true immediately (seeded from newest)
TEST(test_batch_seeds_latest) {
  ShellyClient sc;
  sc.ingestBatch(make_batch(3, 231.0f, 150.0f, 0.65f, 0.97f));
  CHECK(sc.hasData() == true);
  CHECK(fabsf(sc.getVoltage()    - 231.0f) < 0.1f);
  CHECK(fabsf(sc.getPower()      - 150.0f) < 0.1f);
  CHECK(fabsf(sc.getPfApparent() - 0.97f)  < 0.01f);
}

// After a valid batch, shellyOk() must be true (watchdog updated)
TEST(test_batch_clears_watchdog) {
  stub_set_millis(1000);
  ShellyClient sc;
  sc.ingestBatch(make_batch(2));
  CHECK(sc.shellyOk() == true);
  CHECK(sc.getErrorCount() == 0);
}

// hasPendingSamples() true after batch, false once fully drained
TEST(test_batch_pending_queue_drains) {
  ShellyClient sc;
  sc.ingestBatch(make_batch(3));
  CHECK(sc.hasPendingSamples() == true);
  sc.consumePendingSample();
  CHECK(sc.hasPendingSamples() == true);
  sc.consumePendingSample();
  CHECK(sc.hasPendingSamples() == true);
  sc.consumePendingSample();
  CHECK(sc.hasPendingSamples() == false);
}

// consumePendingSample() returns false on an empty queue (no-op)
TEST(test_batch_consume_empty_returns_false) {
  ShellyClient sc;
  CHECK(sc.consumePendingSample() == false);
}

// Samples are consumed oldest-first; msBeforeNow decreases toward 0
TEST(test_batch_chronological_order) {
  ShellyClient sc;
  sc.ingestBatch(make_batch(3));
  // 3 samples: indices 0,1,2 → msBeforeNow = 2000, 1000, 0
  int32_t prev = sc.nextPendingMsAgo();
  sc.consumePendingSample();
  int32_t mid  = sc.nextPendingMsAgo();
  sc.consumePendingSample();
  int32_t last = sc.nextPendingMsAgo();
  CHECK(prev > mid);   // oldest further in the past
  CHECK(mid  > last);  // newest closest to now
  CHECK(last == 0);    // last sample = 0 ms ago (arrived just now)
}

// Batch capped at BATCH_QUEUE_SIZE even if more samples sent
TEST(test_batch_capped_at_max) {
  ShellyClient sc;
  // Send 12 samples — should accept only BATCH_QUEUE_SIZE (10)
  int n = sc.ingestBatch(make_batch(12));
  CHECK(n == BATCH_QUEUE_SIZE);
}

// Single-sample batch works correctly
TEST(test_batch_single_sample) {
  ShellyClient sc;
  int n = sc.ingestBatch(make_batch(1, 229.0f, 80.0f, 0.35f, 0.99f));
  CHECK(n == 1);
  CHECK(sc.hasPendingSamples() == true);
  CHECK(sc.nextPendingMsAgo() == 0);   // only sample → 0 ms ago
  sc.consumePendingSample();
  CHECK(sc.hasPendingSamples() == false);
}

// Malformed JSON returns -1 and increments error count
TEST(test_batch_malformed_json_rejected) {
  ShellyClient sc;
  int n = sc.ingestBatch("{not json}");
  CHECK(n == -1);
  CHECK(sc.getErrorCount() == 1);
  CHECK(sc.hasData() == false);
  CHECK(sc.hasPendingSamples() == false);
}

// Missing "batch" key returns -1
TEST(test_batch_missing_batch_key_rejected) {
  ShellyClient sc;
  int n = sc.ingestBatch("{\"data\":[{\"v\":230.0,\"p\":100.0,\"i\":0.44,\"pf\":0.98}]}");
  CHECK(n == -1);
  CHECK(sc.getErrorCount() == 1);
}

// Out-of-range sample inside batch is skipped; valid ones still queued
TEST(test_batch_skips_out_of_range_sample) {
  ShellyClient sc;
  // 3 samples: first valid, second has v=999 (bad), third valid
  std::string body =
    "{\"batch\":["
      "{\"ts\":1000,\"v\":230.0,\"p\":100.0,\"i\":0.44,\"pf\":0.98},"
      "{\"ts\":2000,\"v\":999.0,\"p\":100.0,\"i\":0.44,\"pf\":0.98},"
      "{\"ts\":3000,\"v\":231.0,\"p\":110.0,\"i\":0.48,\"pf\":0.99}"
    "]}";
  int n = sc.ingestBatch(body);
  CHECK(n == 2);   // only the two valid samples queued
  CHECK(sc.hasPendingSamples() == true);
}

// Sample missing a field inside batch is skipped; others still queued
TEST(test_batch_skips_sample_missing_field) {
  ShellyClient sc;
  std::string body =
    "{\"batch\":["
      "{\"ts\":1000,\"v\":230.0,\"p\":100.0,\"i\":0.44,\"pf\":0.98},"
      "{\"ts\":2000,\"v\":230.0,\"p\":100.0,\"i\":0.44}"
    "]}";          // second entry missing "pf"
  int n = sc.ingestBatch(body);
  CHECK(n == 1);
}

// Second ingestBatch() call resets queue (no stale entries from first batch)
TEST(test_batch_second_call_resets_queue) {
  ShellyClient sc;
  sc.ingestBatch(make_batch(5));
  // Partially drain
  sc.consumePendingSample();
  sc.consumePendingSample();
  // New batch — should replace the remaining 3 pending entries
  int n = sc.ingestBatch(make_batch(2));
  CHECK(n == 2);
  sc.consumePendingSample();
  sc.consumePendingSample();
  CHECK(sc.hasPendingSamples() == false);
}

// After batch is fully drained, normal ingest() continues to work
TEST(test_batch_then_normal_ingest) {
  stub_set_millis(0);
  ShellyClient sc;
  sc.ingestBatch(make_batch(2));
  sc.consumePendingSample();
  sc.consumePendingSample();
  CHECK(sc.hasPendingSamples() == false);
  // Normal single push should still be accepted
  bool ok = sc.ingest(valid_json(230.0f, 200.0f, 0.87f, 0.99f));
  CHECK(ok == true);
  CHECK(fabsf(sc.getPower() - 200.0f) < 0.1f);
  CHECK(sc.shellyOk() == true);
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
  printf("=== ShellyClient unit tests ===\n");

  // -- ingest() tests (existing) --
  printf("\n-- ingest() --\n");
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

  // -- shellyOk() / watchdog tests (existing) --
  printf("\n-- shellyOk() / watchdog --\n");
  test_watchdog_false_before_first_push();
  test_watchdog_true_immediately_after_push();
  test_watchdog_false_after_timeout();
  test_watchdog_recovers_after_new_push();
  test_watchdog_exactly_at_boundary();

  // -- ingestBatch() tests (new) --
  printf("\n-- ingestBatch() --\n");
  test_batch_valid_accepted();
  test_batch_seeds_latest();
  test_batch_clears_watchdog();
  test_batch_pending_queue_drains();
  test_batch_consume_empty_returns_false();
  test_batch_chronological_order();
  test_batch_capped_at_max();
  test_batch_single_sample();
  test_batch_malformed_json_rejected();
  test_batch_missing_batch_key_rejected();
  test_batch_skips_out_of_range_sample();
  test_batch_skips_sample_missing_field();
  test_batch_second_call_resets_queue();
  test_batch_then_normal_ingest();

  printf("\n%d tests run, %d failed\n", _tests_run, _tests_failed);
  return _tests_failed == 0 ? 0 : 1;
}
