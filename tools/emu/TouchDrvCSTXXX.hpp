// Touch stub: the SDL layer writes the mouse position/state into these globals
// and the sketch reads them through the same API as the real CST9217.
#pragma once
#include "Arduino.h"

extern volatile int g_touchX, g_touchY;
extern volatile bool g_touchDown;

class TouchDrvCST92xx {
public:
  void setPins(int, int) {}
  bool begin(class FakeWire &, uint8_t, int, int) { return true; }
  void reset() {}
  void setMaxCoordinates(int, int) {}
  void setMirrorXY(bool, bool) {}
  uint8_t getPoint(int16_t *x, int16_t *y, uint8_t) {
    if (!g_touchDown) return 0;
    *x = g_touchX;
    *y = g_touchY;
    return 1;
  }
};
