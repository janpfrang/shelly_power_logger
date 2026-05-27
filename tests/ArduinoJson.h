#pragma once
/*
 * ArduinoJson.h stub
 * ──────────────────
 * Covers only the ArduinoJson v6 API surface used by ShellyClient::ingest().
 * Uses nlohmann/json under the hood (installed via apt in CI).
 *
 * Mapping:
 *   StaticJsonDocument<N> doc     → JsonDoc wrapper
 *   deserializeJson(doc, str)     → parse string
 *   doc.containsKey("x")         → has("x")
 *   doc["x"].as<float>()          → get<float>("x")
 *   DeserializationError          → thin error type
 */

#include <nlohmann/json.hpp>
#include <string>

struct DeserializationError {
  bool    _ok;
  std::string _msg;
  explicit operator bool() const { return !_ok; }   // true = error (matches ArduinoJson)
  const char* c_str() const { return _msg.c_str(); }
};

template<size_t N>
class StaticJsonDocument {
public:
  nlohmann::json _j;
  bool _valid = false;

  bool containsKey(const char* k) const {
    return _valid && _j.contains(k);
  }

  // Proxy for doc["key"].as<T>()
  struct Proxy {
    const nlohmann::json* _v;
    template<typename T> T as() const {
      if (!_v || _v->is_null()) return T{};
      return _v->get<T>();
    }
  };

  Proxy operator[](const char* k) const {
    if (!_valid || !_j.contains(k)) return {nullptr};
    return {&_j.at(k)};
  }
};

template<size_t N>
DeserializationError deserializeJson(StaticJsonDocument<N>& doc, const std::string& input) {
  try {
    doc._j = nlohmann::json::parse(input);
    doc._valid = true;
    return {true, ""};
  } catch (const nlohmann::json::exception& e) {
    doc._valid = false;
    return {false, e.what()};
  }
}
