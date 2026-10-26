// Touching the screen must never DIM it.
//
// loop() takes `now = millis()` first, then handleTouch() stamps
// `lastInteract = millis()` -- AFTER an I2C point read, so later than `now`.
// updateBrightness(now) then computed `idle = now - lastInteract` in unsigned
// arithmetic: a few milliseconds negative became ~4.29e9, dimStage went to 2,
// and the panel dropped to brightness 8 for the rest of that frame. On every
// press and every release. That is the "the screen goes black for a moment and
// then carries on" report. dimStage >= 1 also let the deferred NVS flush run in
// the middle of the interaction, and could swallow a quick second tap.
//
// The emulator's touch read took no time, so `now` and `lastInteract` were
// always equal here and nothing could see it. g_touchReadUs gives the stub the
// board's I2C latency.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "TouchDrvCSTXXX.hpp"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <chrono>
#include <thread>
uint32_t g_seed=31337; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop();
extern Pet pet;
extern Arduino_CO5300 *panel;
extern uint8_t dimStage;
extern uint32_t lastInteract;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static uint8_t lowest = 255;
static void pump(int n){
  for(int i=0;i<n;i++){
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    loop();
    if (panel->brightness < lowest) lowest = panel->brightness;
  }
}

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  g_touchReadUs = 2000;                 // the board's I2C point read
  pump(5);
  uint8_t awake = panel->brightness;
  ck(awake >= 100 && dimStage == 0, "the panel starts awake and bright");

  // the mechanism this guards: the touch really is stamped AFTER loop()'s `now`
  lowest = 255;
  for (int k = 0; k < 8; k++) {
    g_touchX = 60; g_touchY = 220; g_touchDown = true; emuFireInterrupt(); pump(3);
    g_touchDown = false; emuFireInterrupt(); pump(3);
  }
  printf("     brightness awake=%u, lowest while being touched=%u\n", awake, lowest);
  ck(lowest == awake, "tapping never dips the panel below its awake brightness");
  ck(dimStage == 0, "and leaves it undimmed");

  // and the dimming itself still works -- the fix must not simply disable it
  lastInteract = millis() - 100000;     // 100 s idle
  pump(2);
  ck(dimStage == 1 && panel->brightness < awake, "100 s without a touch still dims");
  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
