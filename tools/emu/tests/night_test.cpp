// A night's sleep must not cost you the creature.
//
// A player left the board running overnight with the pet awake and came back to
// a Dratini that had run away. That was reachable because the LIVE tick is the
// only drain path with no floor: offline floors at 15, asleep floors at 30/35/45
// and skips the neglect check entirely, but a board left RUNNING took an awake
// creature to zero on all four stats in 100 minutes and to the point of running
// away in 160.
//
// The fix is that the creature puts itself to bed, the way a real tamagotchi
// does, so a night is survivable by construction rather than by a special case.
// These tests are the reason that cannot quietly stop working.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
uint32_t g_seed=23; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// an epoch whose hour is exactly h
static uint32_t atHour(int h){ return (uint32_t)h * 3600 + 90; }

// Every case starts awake and unowned. The suite shares one NVS store, so a Pet
// built after another one slept would LOAD that sleep -- which is correct
// behaviour and a wrong starting point for the next test.
static void fresh(Pet &p, int hour){
  p.begin();
  p.dbgHatchAs(147,false);
  p.sleeping = false;
  p.sleepAuto = SLEEP_NONE;
  p.fullness = p.joy = p.energy = p.hygiene = 100;
  p.lastSeenEpoch = atHour(hour);
}

// run n minutes of live ticks, moving the clock with them
static void liveMinutes(Pet &p, int startHour, int n){
  for (int i = 0; i < n; i++) {
    p.lastSeenEpoch = atHour(startHour) + (uint32_t)i * 60;
    p.dbgTick();
  }
}

int main(){
  // --- it goes to bed by itself
  {
    Pet p; fresh(p, 14);
    p.dbgTick();
    ck(!p.sleeping, "awake in the afternoon");
    p.lastSeenEpoch = atHour(21);
    p.dbgTick();
    ck(p.sleeping, "and puts itself to bed at night");
  }

  // --- THE ONE THAT MATTERS: a whole night cannot cost you the creature
  {
    Pet p; fresh(p, 12);
    liveMinutes(p, 21, 9 * 60 - 5);     // 21:00 -> 05:55, still night
    printf("      at 05:55: food=%u joy=%u ene=%u hyg=%u asleep=%d\n",
           p.fullness, p.joy, p.energy, p.hygiene, (int)p.sleeping);
    ck(p.fullness >= 30 && p.joy >= 35 && p.hygiene >= 45,
       "a whole night asleep never drops below the floors");
    ck(p.energy > 50, "and it wakes up rested");
    ck(!p.canRunawayNow(), "nowhere near running away");

    // it gets up at 06:00 and starts draining again, which is the day loop
    // working -- what matters is that an hour of it still costs you nothing
    liveMinutes(p, 6, 60);
    printf("      at 07:00: food=%u joy=%u hyg=%u neglect-ready=%d\n",
           p.fullness, p.joy, p.hygiene, (int)p.canRunawayNow());
    ck(!p.canRunawayNow(), "and it is still yours when you get up");
    ck(p.speciesId == 147, "the creature is still there");
  }

  // --- and it gets up on its own
  {
    Pet p; fresh(p, 12);
    p.lastSeenEpoch = atHour(22); p.dbgTick();
    ck(p.sleeping, "asleep at 22:00");
    p.lastSeenEpoch = atHour(8); p.dbgTick();
    ck(!p.sleeping, "and up again by morning");
  }

  // --- the player's hand beats the clock until morning
  {
    Pet p; fresh(p, 12);
    p.lastSeenEpoch = atHour(23); p.dbgTick();
    ck(p.sleeping, "asleep at 23:00");
    p.toggleLight();                       // the player wants to play
    ck(!p.sleeping, "the light wakes it");
    for (int i = 0; i < 30; i++) { p.lastSeenEpoch = atHour(23) + i * 60; p.dbgTick(); }
    ck(!p.sleeping, "and it stays awake rather than nodding straight off again");
    p.lastSeenEpoch = atHour(9); p.dbgTick();
    ck(!p.sleeping && p.sleepAuto == SLEEP_NONE, "morning clears the override");
  }

  // --- putting it to bed early is respected too
  {
    Pet p; fresh(p, 12);
    p.lastSeenEpoch = atHour(15); p.dbgTick();
    p.toggleLight();
    ck(p.sleeping, "an afternoon nap, by choice");
    for (int i = 0; i < 20; i++) { p.lastSeenEpoch = atHour(15) + i * 60; p.dbgTick(); }
    ck(p.sleeping, "and the clock does not wake it before night is over");
  }

  // --- total neglect while AWAKE still ends badly: the teeth are still there
  {
    Pet p; fresh(p, 12);
    liveMinutes(p, 9, 170);             // a full day of being ignored, awake
    ck(!p.sleeping, "ignored all day, still awake");
    ck(p.canRunawayNow(), "and genuine neglect still makes it ready to leave");
  }

  // --- with no clock at all there is no night, so nothing auto-sleeps
  {
    Pet p; fresh(p, 12);
    p.lastSeenEpoch = 0;
    p.dbgTick();
    ck(!p.sleeping, "a board with no clock never auto-sleeps");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
