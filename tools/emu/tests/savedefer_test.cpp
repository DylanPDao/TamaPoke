// Care actions no longer write the save on the spot (savecount_test counts
// that); loop() flushes them. This drives the real loop() to check WHEN:
//   * never while the player is actively using the device,
//   * as soon as the panel dims,
//   * and, so that continuous play cannot hold a change unsaved forever, once
//     it has waited SAVE_MAX_DEFER_MS (2 min) even with the panel lit.
// The clock is the emulator's own, sped up with emuSetTimeScale() so ten
// minutes pass in well under a second.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <chrono>
#include <thread>
uint32_t g_seed=99; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop();
void emuSetTimeScale(uint32_t s);
extern Pet pet;
extern uint8_t dimStage;
extern uint32_t lastInteract;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// the player keeps touching: the panel never dims
static void activeUntil(uint32_t t){
  while (millis() < t) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    lastInteract = millis();
    loop();
    pet.sleeping = false;
  }
}

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(4, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  pet.saveNow();

  // 1. active use: a feed waits
  uint32_t n0 = pet.saveCount;
  pet.feedBerry(0);
  activeUntil(millis() + 400);
  ck(pet.saveCount == n0 && pet.savePending(), "during active use a feed is not written");
  ck(dimStage == 0, "(and the panel really stayed lit)");

  // let the eating animation and the heart finish: updateBrightness() treats
  // them as visible events and keeps the panel lit while they play
  emuSetTimeScale(50);
  activeUntil(millis() + 10000);
  emuSetTimeScale(1);
  ck(pet.saveCount == n0 && pet.savePending(), "ten seconds on, still not written");

  // 2. the panel dims: written at once
  lastInteract = millis() - 100000;
  loop();
  ck(dimStage >= 1, "100 s idle dims the panel");
  ck(pet.saveCount == n0 + 1 && !pet.savePending(), "and the pending save is written right then");

  // 3. continuous play: written once it is SAVE_MAX_DEFER_MS old, not before
  lastInteract = millis(); loop();
  pet.feedBerry(0);
  uint32_t n1 = pet.saveCount;
  emuSetTimeScale(2000);
  uint32_t t0 = millis();
  activeUntil(t0 + 60000);               // 1 virtual minute
  ck(pet.saveCount == n1, "one minute of continuous play: still not written");
  activeUntil(t0 + 150000);              // 2.5 virtual minutes
  emuSetTimeScale(1);
  printf("     2.5 min of continuous play: %u save(s)\n", pet.saveCount - n1);
  ck(pet.saveCount - n1 >= 1 && pet.saveCount - n1 <= 2,
     "after two minutes it is written anyway, panel lit or not");

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
