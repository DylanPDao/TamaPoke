// Touch stub: the SDL layer writes the mouse position/state into these globals
// and the sketch reads them through the same API as the real CST9217.
#pragma once
#include "Arduino.h"
#include <chrono>
#include <thread>

extern volatile int g_touchX, g_touchY;
extern volatile bool g_touchDown;
// How long a point read takes. ZERO by default, which is what hid a bug: on the
// board getPoint() is an I2C transaction (~1-2 ms on the shared bus), so millis()
// has moved on by the time handleTouch() stamps lastInteract -- and loop()'s
// `now`, taken before it, was suddenly in the past. A test that cares sets this.
inline uint32_t g_touchReadUs = 0;

class TouchDrvCST92xx {
public:
  void setPins(int, int) {}
  bool begin(class FakeWire &, uint8_t, int, int) { return true; }
  void reset() {}
  void setMaxCoordinates(int, int) {}
  void setMirrorXY(bool, bool) {}
  uint8_t getPoint(int16_t *x, int16_t *y, uint8_t) {
    if (g_touchReadUs) std::this_thread::sleep_for(std::chrono::microseconds(g_touchReadUs));
    if (!g_touchDown) return 0;
    *x = g_touchX;
    *y = g_touchY;
    return 1;
  }
};
