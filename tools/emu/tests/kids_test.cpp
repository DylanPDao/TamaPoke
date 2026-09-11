// Kinder-Modus: the creature can never be lost and the game never punishes.
//
// The spec, verbatim: "Alle 4-5 Tage mal ein wenig machen: voll ausreichend,
// mehr soll natuerlich gehen -- aber es darf kein Zwang sein!" So a device put
// down for a week must come back with a friend that would like a snack, not a
// bar at zero, a "Patzer" counter, or a run-away button. These drive the real
// Pet through live ticks, sleep, and the offline catch-up, in kids mode and
// (as a control) in the strict mode it replaces.
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

static uint32_t atHour(int h){ return (uint32_t)h * 3600 + 90; }

// awake, daytime, full bars, kids mode as requested
static void fresh(Pet &p, bool kids){
  p.begin();
  p.dbgHatchAs(147,false);          // DRATINI: the one that was lost overnight
  p.setKidsMode(kids);
  p.sleeping = false;
  p.sleepAuto = SLEEP_NONE;
  p.fullness = p.joy = p.energy = p.hygiene = 100;
  p.poops = 0;
  p.careMistakes = 0;
  p.lastSeenEpoch = atHour(13);
}

static void liveMinutes(Pet &p, int startHour, int n, bool attended){
  p.setAttended(attended);
  for (int i = 0; i < n; i++) {
    p.lastSeenEpoch = atHour(startHour) + (uint32_t)i * 60;
    p.dbgTick();
  }
}

static uint8_t lowest(const Pet &p){
  uint8_t m = p.fullness;
  if (p.joy < m) m = p.joy;
  if (p.energy < m) m = p.energy;
  if (p.hygiene < m) m = p.hygiene;
  return m;
}

int main(){
  // --- the default: a fresh save is in kids mode
  {
    Pet p; p.begin();
    ck(p.kidsMode == KIDS_MODE_DEFAULT, "a fresh save starts in the default mode");
    ck(KIDS_MODE_DEFAULT, "and in this fork the default is kids mode ON");
  }

  // --- CONTROL: strict mode still bites, so the kids checks below mean something
  {
    Pet p; fresh(p, false);
    liveMinutes(p, 13, 120, true);
    ck(p.fullness == 0, "strict mode: two attended hours empty the food bar");
    ck(p.careMistakes > 0, "strict mode: and that counts as a care mistake");
  }

  // --- PUT DOWN: a week on the shelf, awake, screen dimmed
  {
    Pet p; fresh(p, true);
    p.fullness = p.joy = p.hygiene = 70;
    p.energy = 40;
    liveMinutes(p, 13, 7 * 24 * 60, false);
    printf("      a week unattended: food=%u joy=%u ene=%u hyg=%u mistakes=%u poops=%u\n",
           p.fullness, p.joy, p.energy, p.hygiene, p.careMistakes, p.poops);
    ck(p.fullness == 70 && p.joy == 70 && p.hygiene == 70, "unattended, nothing drains at all");
    ck(p.energy == 100, "unattended, it rests and comes back with full energy");
    ck(p.careMistakes == 0, "a week alone is not a mistake");
    ck(!p.canRunawayNow(), "and it is not one tap from leaving");
    ck(p.poops == 0, "nothing to clean up either");
    ck(p.level() > 100 || p.level() == MAX_LEVEL, "it kept growing the whole time (levels are time)");
  }

  // --- PLAYING: attended, the needs move so there is something to do
  {
    Pet p; fresh(p, true);
    liveMinutes(p, 13, 240, true);
    printf("      four attended hours: food=%u joy=%u ene=%u hyg=%u poops=%u\n",
           p.fullness, p.joy, p.energy, p.hygiene, p.poops);
    ck(p.fullness < 100, "playing, it does get a little hungry");
    ck(lowest(p) >= KIDS_FLOOR, "but no bar ever goes below the floor");
    ck(p.careMistakes == 0, "and there are no mistakes to make");
    ck(p.poops <= KIDS_POOP_MAX, "at most one poop on screen");
    ck(!p.canRunawayNow(), "run-away is never offered");
  }

  // --- NEGLECT that would have lost it: all bars at zero for hours
  {
    Pet p; fresh(p, true);
    p.fullness = p.joy = p.energy = p.hygiene = 0;   // however it got there
    liveMinutes(p, 13, 180, true);
    ck(!p.canRunawayNow(), "even from zero, run-away is never offered");
    ck(p.careMistakes == 0, "and zero is not a mistake");
    // evolution: exactly at the dex level, with nothing added for the neglect
    p.fullness = p.joy = p.energy = p.hygiene = 100;
    p.ageMinutes = (uint32_t)DEX_TBL[147].evolveLevel * MINUTES_PER_LEVEL;
    ck(p.canEvolveNow(), "evolution comes at the dex level, never delayed by care");
  }

  // --- A SAVE AT ZERO (from the strict firmware) loads straight onto the floor
  {
    Pet p; p.begin(); p.setKidsMode(false); p.dbgHatchAs(147,false);
    p.fullness = p.joy = p.energy = p.hygiene = 0; p.poops = 3; p.saveNow();
    Pet q; q.begin(); q.setKidsMode(true); q.saveNow();
    Pet r; r.begin();
    ck(lowest(r) >= KIDS_FLOOR && r.poops <= KIDS_POOP_MAX,
       "a save at zero comes up at the floor, with the mess mostly cleared");
  }

  // --- NIGHT: asleep, the floors are the kids floor
  {
    Pet p; fresh(p, true);
    p.sleeping = true;
    liveMinutes(p, 1, 6 * 60, false);
    printf("      six hours asleep: food=%u joy=%u hyg=%u\n", p.fullness, p.joy, p.hygiene);
    ck(lowest(p) >= KIDS_FLOOR, "asleep, nothing drops below the floor either");
  }

  // --- OFFLINE: powered off for ten days, then switched on
  {
    Pet p; fresh(p, true);
    p.fullness = p.joy = p.energy = p.hygiene = 100;
    p.dbgSetSeen(atHour(13));
    p.saveNow();
    p.syncClock(atHour(13) + 10UL * 24 * 3600);
    printf("      ten days off: food=%u joy=%u ene=%u hyg=%u poops=%u\n",
           p.fullness, p.joy, p.energy, p.hygiene, p.poops);
    ck(lowest(p) >= KIDS_FLOOR, "ten days powered off: still at or above the floor");
    ck(p.poops <= KIDS_POOP_MAX, "and at most one poop waiting");
    ck(p.careMistakes == 0 && !p.canRunawayNow(), "no mistakes, no run-away, after ten days off");
  }

  // --- RETIRE: always the good ending, always banked, always free
  {
    Pet p; fresh(p, true);
    ck(p.retireIsFree(), "retiring early costs nothing in kids mode");
    p.startRetire();
    ck(p.ceremony == CER_FAREWELL, "retire runs the farewell ceremony");
    ck(!p.retireIsEarly(), "and is never counted as an early retire");
    ck(p.lastEnd == CER_FAREWELL, "so the next egg is blessed, not neutral");
  }

  // --- SWITCHING it on forgives; switching it off does not punish retroactively
  {
    Pet p; fresh(p, false);
    liveMinutes(p, 13, 200, true);       // strict mode wrecks it
    ck(p.fullness == 0 && p.careMistakes > 0, "control: strict mode left damage");
    p.setKidsMode(true);
    ck(lowest(p) >= KIDS_FLOOR, "switching kids mode on lifts every bar to the floor");
    ck(p.careMistakes == 0, "and wipes the mistakes");
    ck(!p.canRunawayNow(), "and the run-away readiness");
    p.setKidsMode(false);
    ck(lowest(p) >= KIDS_FLOOR, "switching it off changes nothing on the spot");
    liveMinutes(p, 13, 60, true);
    ck(p.fullness < KIDS_FLOOR, "strict rules simply apply from the next tick");
  }

  // --- PERSISTENCE: the switch survives a reload
  {
    Pet p; p.begin(); p.setKidsMode(false); p.saveNow();
    Pet q; q.begin();
    ck(!q.kidsMode, "kids mode OFF is persisted");
    q.setKidsMode(true); q.saveNow();
    Pet r; r.begin();
    ck(r.kidsMode, "and kids mode ON is persisted");
  }

  // --- A SAVE FROM THE STRICT FIRMWARE: loaded in kids mode, its debts are forgiven
  {
    Pet p; p.begin(); p.setKidsMode(false); p.dbgHatchAs(147,false);
    p.careMistakes = 4; p.saveNow();
    Pet q; q.begin(); q.setKidsMode(true); q.saveNow();
    Pet r; r.begin();
    ck(r.kidsMode && r.careMistakes == 0, "mistakes written by the strict rules are forgiven on load");
  }

  printf(bad ? "FAILURES\n" : "all good\n");
  return bad ? 1 : 0;
}
