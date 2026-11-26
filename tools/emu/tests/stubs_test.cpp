// The emulator's stand-ins must offer what the firmware is free to call.
//
// Arduino_GFX's canvas has drawPixel() and Arduino has constrain(), and the
// firmware build accepts both. The emulator's stand-ins had neither, so a
// change that drew one pixel or clamped a value compiled for the board and
// failed for the emulator -- the change could be flashed but not checked here
// first. Found while smoothing the pet sprite, which wanted a per-pixel draw.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
uint32_t g_seed = 1;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}
#include <cstdio>
static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

int main() {
  Arduino_Canvas c(466, 466, nullptr);
  c.fillScreen(0x0000);
  c.drawPixel(10, 20, 0xF800);
  ck(c.buffer()[20 * 466 + 10] == 0xF800, "drawPixel colours the pixel it names");
  ck(c.buffer()[20 * 466 + 11] == 0x0000 && c.buffer()[21 * 466 + 10] == 0x0000, "and only that pixel");
  // the same result as the 1x1 fillRect the sketch had to use instead
  c.fillRect(30, 40, 1, 1, 0x07E0);
  c.drawPixel(31, 40, 0x07E0);
  ck(c.buffer()[40 * 466 + 30] == c.buffer()[40 * 466 + 31], "drawPixel and a 1x1 fillRect agree");
  // off the panel is ignored, as on the board, rather than written out of bounds
  c.drawPixel(-1, 0, 0xFFFF); c.drawPixel(466, 0, 0xFFFF); c.drawPixel(0, 466, 0xFFFF);
  ck(c.buffer()[0] == 0x0000, "a pixel off the panel is ignored");

  ck(constrain(5, 0, 10) == 5, "constrain leaves a value inside the range alone");
  ck(constrain(-3, 0, 10) == 0, "constrain raises a value below the range to its floor");
  ck(constrain(42, 0, 10) == 10, "constrain lowers a value above the range to its ceiling");
  ck(constrain(1.5f, 0.0f, 1.0f) == 1.0f, "constrain works on floats");

  if (bad) { printf("FAILURES\n"); return 1; }
  return 0;
}
