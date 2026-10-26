// Deadlines must survive millis() wrapping, and must not come back to life.
//
// millis() is 32 bits of milliseconds: it wraps after 49.7 days of uptime, and
// a device left on USB gets there. Every "until" was compared with a plain
// `millis() < until`, so past the wrap -- millis() small again, the deadline
// still a huge number -- each one read as ACTIVE for weeks: the creature eating
// forever (which also held the panel at full brightness, since eating counts as
// a visible event), the food picker and the "release?" dialog stuck on screen.
//
// timerActive() uses the signed difference, which is right across the wrap but
// only for 24.8 days either side of the deadline -- so Pet::update() also
// clears expired timers to 0, and the second half of this test proves that by
// jumping 25 days past a deadline.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
uint32_t g_seed=49; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop();
void emuAdvanceMs(uint64_t ms);
extern Pet pet;
extern uint32_t feedMenuUntil, confirmUntil;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  setup();
  for (int i = 0; i < 3; i++) loop();
  // Up to 20 s before the wrap. begin() again afterwards so the creature does
  // not "live through" 49 days of catch-up ticks on the way -- this is about
  // the clock, not about ageing.
  uint32_t m0 = millis();
  emuAdvanceMs((uint64_t)0x100000000ULL - m0 - 20000);
  pet.begin();
  pet.dbgHatchAs(25, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  pet.fullness = 40;
  pet.energy = pet.joy = pet.hygiene = 90;
  loop();

  // ---- across the wrap
  pet.feedBerry(0);                         // eatUntil = now + 2.5 s
  feedMenuUntil = millis() + 6000;
  confirmUntil = millis() + 10000;
  ck(pet.eating() && feedMenuUntil, "just before the wrap: eating, picker open");
  uint32_t before = millis();
  emuAdvanceMs(40000);                      // over the top
  uint32_t after = millis();
  ck(after < before && after < 30000, "millis() really wrapped (the conflict arose)");
  for (int i = 0; i < 3; i++) loop();
  ck(!pet.eating(), "after the wrap the meal is OVER, not active for weeks");
  ck(feedMenuUntil == 0, "the food picker closed");
  ck(confirmUntil == 0, "the release dialog closed");
  ck(!pet.showHeart(), "no heart stuck on screen");

  // ---- and a deadline cannot come back 24.8 days later
  pet.feedBerry(0);
  ck(pet.eating(), "eating again");
  emuAdvanceMs(5000);
  pet.update(millis());
  ck(!pet.eating(), "and done");
  emuAdvanceMs(25ULL * 24 * 3600 * 1000);   // past the signed difference's reach
  pet.update(millis());
  ck(!pet.eating(), "25 days later the old meal is still over");

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
