// Putting the device down must not cost you the creature.
//
// A player left the board running overnight with the pet awake and came back to
// a Dratini that had run away. That was reachable because the LIVE tick is the
// only drain path with no floor: offline floors at 15, asleep floors at 30/35/45
// and skips the neglect check entirely, but a board left RUNNING took an awake
// creature to zero on all four stats in 100 minutes and to the point of running
// away in 160.
//
// The fix is that sleep follows the SCREEN: pressing PWR is a deliberate "I am
// putting this down", and asleep the stats floor and the neglect check is
// skipped, so a night away is survivable by construction. It is tied to the
// screen rather than to an hour because the RTC on an unset board is months out
// -- a clock-based bedtime would fire at the wrong time for the people most
// likely to leave a board running.
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
  // --- the screen going off puts it to sleep
  {
    Pet p; fresh(p, 14);
    p.dbgTick();
    ck(!p.sleeping, "awake while you are looking at it");
    p.screenSleep(true);
    ck(p.sleeping, "and asleep the moment the screen goes off");
    p.screenSleep(false);
    ck(!p.sleeping, "awake again when you come back");
  }

  // --- THE ONE THAT MATTERS: a night with the screen off costs you nothing
  {
    Pet p; fresh(p, 21);
    p.screenSleep(true);
    liveMinutes(p, 21, 10 * 60);        // ten hours, board running, screen off
    printf("      ten hours with the screen off: food=%u joy=%u ene=%u hyg=%u ready=%d\n",
           p.fullness, p.joy, p.energy, p.hygiene, (int)p.canRunawayNow());
    ck(p.fullness >= 30 && p.joy >= 35 && p.hygiene >= 45,
       "ten hours asleep never drops below the floors");
    ck(p.energy > 50, "and it wakes rested");
    ck(!p.canRunawayNow(), "NOWHERE NEAR running away");
    ck(p.speciesId == 147, "the creature is still there");
  }

  // --- the light button still beats the screen
  {
    Pet p; fresh(p, 14);
    p.toggleLight();                    // the player sends it to bed
    ck(p.sleeping, "the light puts it to bed by hand");
    p.screenSleep(false);               // turning the screen back on
    ck(p.sleeping, "and the screen does not overrule that");
    p.toggleLight();
    ck(!p.sleeping, "only the light does");
  }

  // --- total neglect while AWAKE still ends badly: the teeth are still there
  {
    Pet p; fresh(p, 9);
    liveMinutes(p, 9, 170);             // ignored all day, screen on
    ck(!p.sleeping, "ignored with the screen on, still awake");
    ck(p.canRunawayNow(), "and genuine neglect still makes it ready to leave");
  }

  // --- an egg is not something you put to sleep
  {
    Pet p; p.begin(); p.newEgg();
    p.screenSleep(true);
    ck(!p.sleeping, "an egg does not sleep");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
