#include "Arduino.h"
#include "Preferences.h"
// linked against the same core as every other suite, so it needs the same
// hardware stubs even though it only exercises the string table
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
#include "i18n.h"
#include <cstdio>
#include <cstring>
int main(){
  const StrId ids[] = { S_VIN, S_STAT_ATK, S_STAT_DEF, S_STAT_SPE, S_STAT_VIT, S_STAT_WGT };
  const char *ln[] = {"ES","EN","FR","DE","IT","PT"};
  int worst = 0;
  for (int l=0;l<LANG_COUNT;l++){ setLang((Lang)l);
    for (auto id : ids){ int w = 70 + (int)strlen(T(id))*12;
      if (w > 132) printf("COLLIDES %s \"%s\" ends at x=%d (bar starts 132)\n", ln[l], T(id), w);
      if (w > worst) worst = w; } }
  printf("widest label ends at x=%d\n", worst);
  return 0;
}
