cmake_minimum_required(VERSION 3.16)
project(shelly_logger_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Treat warnings as errors — keeps the test code honest
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# ── nlohmann/json (required by our ArduinoJson stub) ─────────────────────────
find_package(nlohmann_json REQUIRED)

# ── Include paths ─────────────────────────────────────────────────────────────
# 1. tests/        — arduino_stub.h, config_stub.h, ArduinoJson.h (stub)
# 2. Logger/       — the real ShellyClient.h, Logger.h, StatusLed.h
# IMPORTANT: tests/ must come FIRST so our stub headers (Arduino.h,
# ArduinoJson.h) shadow the system / ESP32 versions when compiling
# the firmware headers on the host.
include_directories(BEFORE
    ${CMAKE_CURRENT_SOURCE_DIR}          # tests/  ← stubs live here
    ${CMAKE_CURRENT_SOURCE_DIR}/../Logger  # sketch source files
)

# ─────────────────────────────────────────────────────────────────────────────
# Test executables
# ─────────────────────────────────────────────────────────────────────────────

add_executable(test_shelly_client test_shelly_client.cpp)
target_link_libraries(test_shelly_client nlohmann_json::nlohmann_json)

add_executable(test_logger test_logger.cpp)
target_link_libraries(test_logger nlohmann_json::nlohmann_json)

# ── CTest integration ─────────────────────────────────────────────────────────
enable_testing()
add_test(NAME ShellyClient COMMAND test_shelly_client)
add_test(NAME Logger        COMMAND test_logger)
