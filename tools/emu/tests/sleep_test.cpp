// Putting the device down must not cost you the creature.
//
// A player left the board running overnight with the pet awake and came back to
// a Dratini that had run away. That was reachable because the LIVE tick is the
// only drain path with no floor: offline floors at 15, asleep floors at 30/35/45
// and skips the neglect check entirely, but a board left RUNNING took an awake
// creature to zero on all four stats in 100 minutes and to the point of running
// away in 160.
//
// Auto-sleep needs BOTH the screen off and the night window (22:00-06:00).
// Screen-off alone paused the game whenever the device was put down, and the
// creature is meant to get hungry during the day. The hour alone sent it to bed
// while somebody was still playing with it.
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
  // --- THE DAY: putting it down does NOT pause the game
  {
    Pet p; fresh(p, 13);
    p.setScreenOff(true);
    ck(!p.sleeping, "screen off in the afternoon does not put it to sleep");
    liveMinutes(p, 13, 120);
    printf("      two hours face-down at 13:00: food=%u joy=%u hyg=%u\n",
           p.fullness, p.joy, p.hygiene);
    ck(p.fullness == 0, "it still gets hungry with the screen off during the day");
  }

  // --- THE NIGHT: screen off after 22:00 and it sleeps
  {
    Pet p; fresh(p, 23);
    p.setScreenOff(true);
    ck(p.sleeping, "screen off at 23:00 puts it to sleep");
  }

  // --- put down BEFORE bedtime: it nods off when the hour comes
  {
    Pet p; fresh(p, 21);
    p.setScreenOff(true);
    ck(!p.sleeping, "put down at 21:00, still awake");
    liveMinutes(p, 21, 70);              // through 22:00
    ck(p.sleeping, "and it goes to bed by itself once 22:00 arrives");
  }

  // --- THE ONE THAT MATTERS: the whole night costs nothing
  {
    Pet p; fresh(p, 22);
    p.setScreenOff(true);
    liveMinutes(p, 22, 8 * 60 - 5);      // 22:00 -> 05:55
    printf("      at 05:55: food=%u joy=%u ene=%u hyg=%u ready=%d\n",
           p.fullness, p.joy, p.energy, p.hygiene, (int)p.canRunawayNow());
    ck(p.fullness >= 30 && p.joy >= 35 && p.hygiene >= 45,
       "a night asleep never drops below the floors");
    ck(p.energy > 50, "and it wakes rested");
    ck(!p.canRunawayNow(), "NOWHERE NEAR running away");
    ck(p.speciesId == 147, "the creature is still there");

    // and it gets up at 06:00 even with the screen still off, so the day drains
    liveMinutes(p, 6, 10);
    ck(!p.sleeping, "up at 06:00 even though the screen is still off");
  }

  // --- the light button still beats both
  {
    Pet p; fresh(p, 13);
    p.toggleLight();
    ck(p.sleeping, "the light puts it to bed by hand, in daylight");
    p.setScreenOff(false);
    ck(p.sleeping, "and neither the screen...");
    liveMinutes(p, 13, 30);
    ck(p.sleeping, "...nor the clock overrules that");
    p.toggleLight();
    ck(!p.sleeping, "only the light does");
  }

  // --- waking it at night keeps it awake rather than fighting the rule
  {
    Pet p; fresh(p, 23);
    p.setScreenOff(true);
    ck(p.sleeping, "asleep at 23:00");
    p.toggleLight();                     // the player wants to play
    ck(!p.sleeping, "the light wakes it");
    liveMinutes(p, 23, 30);
    ck(!p.sleeping, "and it stays awake rather than nodding straight off again");
  }

  // --- total neglect while AWAKE still ends badly: the teeth are still there
  {
    Pet p; fresh(p, 9);
    liveMinutes(p, 9, 170);
    ck(!p.sleeping, "ignored all day, still awake");
    ck(p.canRunawayNow(), "and genuine neglect still makes it ready to leave");
  }

  // --- a board whose clock was never set never auto-sleeps, and says so by
  //     still draining rather than by freezing
  {
    Pet p; fresh(p, 12);
    p.lastSeenEpoch = 0;
    p.setScreenOff(true);
    ck(!p.sleeping, "no clock, no bedtime");
  }

  // --- an egg is not something you put to sleep
  {
    Pet p; p.begin(); p.newEgg();
    p.setScreenOff(true);
    ck(!p.sleeping, "an egg does not sleep");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
