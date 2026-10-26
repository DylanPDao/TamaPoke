// Stand-in for SensorLib's PCF85063, for pmu_test only (rtcbat.cpp needs it to
// compile; the test does not exercise the clock).
#pragma once
#include "Arduino.h"
struct RTC_DateTime {
  uint16_t y = 2026; uint8_t mo = 1, d = 1, h = 0, mi = 0, s = 0;
  RTC_DateTime() {}
  RTC_DateTime(uint16_t Y, uint8_t MO, uint8_t D, uint8_t H, uint8_t MI, uint8_t S)
      : y(Y), mo(MO), d(D), h(H), mi(MI), s(S) {}
  uint16_t getYear() const { return y; }
  uint8_t getMonth() const { return mo; }
  uint8_t getDay() const { return d; }
  uint8_t getHour() const { return h; }
  uint8_t getMinute() const { return mi; }
  uint8_t getSecond() const { return s; }
};
class SensorPCF85063 {
public:
  RTC_DateTime t;
  bool begin(FakeWire &, int, int) { return true; }
  RTC_DateTime getDateTime() { return t; }
  void setDateTime(const RTC_DateTime &v) { t = v; }
};
