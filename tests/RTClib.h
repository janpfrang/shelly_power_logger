#pragma once
// RTClib stub for native unit tests.
// Logger.h includes RTClib.h only to declare RTC_DS3231*.
// In tests, Logger is constructed with nullptr, so rtc->now() is never called.
#include <cstdint>
struct DateTime {
  explicit DateTime(uint32_t = 0) {}
  uint16_t year()   const { return 2000; }
  uint8_t  month()  const { return 1; }
  uint8_t  day()    const { return 1; }
  uint8_t  hour()   const { return 0; }
  uint8_t  minute() const { return 0; }
  uint8_t  second() const { return 0; }
  uint32_t unixtime() const { return 0; }
};
class RTC_DS3231 {
public:
  bool     begin(void* = nullptr) { return true; }
  bool     lostPower()            { return false; }
  DateTime now()                  { return DateTime(0); }
  void     adjust(const DateTime&) {}
};
 
