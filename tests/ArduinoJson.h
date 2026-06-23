#pragma once
/*
 * ArduinoJson.h stub  (updated for ShellyClient batch support)
 * ─────────────────────────────────────────────────────────────
 * Covers the ArduinoJson v6 API surface used by ShellyClient.
 * Uses nlohmann/json under the hood (installed via apt in CI).
 *
 * API surface supported:
 *   StaticJsonDocument<N> doc
 *   deserializeJson(doc, str)        → parse string
 *   doc.containsKey("x")            → field existence check
 *   doc["x"].as<T>()                → scalar field access
 *   doc["x"].isArray()              → array type check
 *   doc["x"].size()                 → array length
 *   doc["x"][i].containsKey("k")    → nested object field check
 *   doc["x"][i]["k"].as<T>()        → nested object field access
 *   DeserializationError            → thin error type
 *
 * No DynamicJsonDocument, JsonArray, or JsonObject needed —
 * ShellyClient uses the proxy chain doc["batch"][i]["v"].as<float>().
 */

#include <nlohmann/json.hpp>
#include <string>

struct DeserializationError {
  bool        _ok;
  std::string _msg;
  explicit operator bool() const { return !_ok; }   // true = error
  const char* c_str()      const { return _msg.c_str(); }
};

// Type tags used with is<T>() — only JsonArray needed by ShellyClient.
// These are empty structs; their identity is used purely as template arguments.
struct JsonArray  {};
struct JsonObject {};

// ─── Proxy returned by operator[] ────────────────────────────────────────────
// Wraps a pointer to a nlohmann::json node (null if key absent / out-of-range).
// Supports: .as<T>(), .containsKey(), .isArray(), .size(), operator[](size_t),
//           operator[](const char*) for chained access.

struct JsonProxy {
  const nlohmann::json* _v;   // nullptr means "missing"

  // Scalar extraction
  template<typename T>
  T as() const {
    if (!_v || _v->is_null()) return T{};
    return _v->get<T>();
  }

  // Field existence (for object nodes)
  bool containsKey(const char* k) const {
    return _v && _v->is_object() && _v->contains(k);
  }

  // Array type check — legacy name kept for clarity
  bool isArray() const {
    return _v && _v->is_array();
  }

  // ArduinoJson v7 API: doc["key"].is<JsonArray>()
  // The stub only needs to handle JsonArray in practice.
  template<typename T>
  bool is() const {
    return isArray();   // only called with JsonArray in ShellyClient
  }

  // Array / object size
  size_t size() const {
    if (!_v) return 0;
    return _v->size();
  }

  // Index into array — returns a nested proxy
  JsonProxy operator[](size_t idx) const {
    if (!_v || !_v->is_array() || idx >= _v->size()) return {nullptr};
    return {&(*_v)[idx]};
  }

  // Key into object — returns a nested proxy
  JsonProxy operator[](const char* k) const {
    if (!_v || !_v->is_object() || !_v->contains(k)) return {nullptr};
    return {&_v->at(k)};
  }
};

// ─── StaticJsonDocument ───────────────────────────────────────────────────────

template<size_t N>
class StaticJsonDocument {
public:
  nlohmann::json _j;
  bool           _valid = false;

  bool containsKey(const char* k) const {
    return _valid && _j.is_object() && _j.contains(k);
  }

  JsonProxy operator[](const char* k) const {
    if (!_valid || !_j.is_object() || !_j.contains(k)) return {nullptr};
    return {&_j.at(k)};
  }
};

// ─── deserializeJson ─────────────────────────────────────────────────────────

template<size_t N>
DeserializationError deserializeJson(StaticJsonDocument<N>& doc,
                                     const std::string& input) {
  try {
    doc._j     = nlohmann::json::parse(input);
    doc._valid = true;
    return {true, ""};
  } catch (const nlohmann::json::exception& e) {
    doc._valid = false;
    return {false, e.what()};
  }
}
