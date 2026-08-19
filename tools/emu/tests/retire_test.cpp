// Retiring a creature on demand, and what it costs.
//
// The farewell was only ever OFFERED at final form and three days. Retiring
// before that is now possible from the menu, and the price is that the NEXT
// creature evolves a day later -- EVO_PENALTY_LEVELS on top of the same
// threshold careMistakes already moves.
//
// Three things here can go wrong silently and so are pinned:
//   * the debt must land on the NEXT creature, never the one being retired
//   * it must not compound: retiring early twice is still one day, not two
//   * retiring a creature that HAS earned its farewell must cost nothing
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
uint32_t g_seed=41; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
// a clock the test can push forward: a ceremony ends when millis() passes
// ceremonyUntil, so a frozen clock would leave every farewell running forever
static uint32_t gNow = 1;
uint32_t millis(){ return gNow; }
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// Ends the ceremony the way the firmware does -- by letting time pass into
// Pet::update() -- rather than by calling the two halves by hand, which would
// stop testing the order they run in.
static void finish(Pet &p, Party &q){
  gNow += CEREMONY_MS + 1000;
  p.update(gNow);
  if (p.endedKind != CER_NONE) {
    if (!q.add(p.endedMon)) q.boxAdd(p.endedMon);
    p.endedKind = CER_NONE;
  }
}

// a creature part-way through its life: hatched, young, not final form
static void young(Pet &p, int16_t dex, uint8_t lvl){
  p.dbgHatchAs(dex,false);
  p.ageMinutes = (uint32_t)(lvl-1)*MINUTES_PER_LEVEL;
  p.fullness=p.joy=p.energy=p.hygiene=100;
}

int main(){
  ck(EVO_PENALTY_LEVELS == (24*60)/MINUTES_PER_LEVEL,
     "the penalty is a day expressed in levels, not a hardcoded 24");

  // --- a young creature can be retired, and it really is banked
  {
    Pet p; Party q; p.begin(); q.begin();
    young(p, 4, 21);                        // a Charmeleon, nowhere near 3 days
    ck(!p.canFarewellNow(), "a young creature is not offered a farewell");
    ck(p.canRetireNow(), "but it can be retired on demand");
    ck(!p.retireIsFree(), "and the game says that costs something");
    p.startRetire();
    ck(p.ceremony == CER_FAREWELL, "retiring runs the farewell ceremony");
    finish(p, q);
    ck(q.count() == 1 && q.slots[0].dex == 4, "the creature is banked, not lost");
    ck(q.slots[0].level == 21, "frozen at the level it had");
    ck(p.isEgg(), "and a new egg is waiting");

    // THE POINT: the debt is on the new creature
    ck(p.evoPenalty() == EVO_PENALTY_LEVELS, "the next creature owes the day");
    p.dbgHatchAs(1, false);                 // a Bulbasaur, evolves at 16
    p.fullness=p.joy=p.energy=p.hygiene=100;
    const DexEntry &d = DEX_TBL[1];
    p.ageMinutes = (uint32_t)(d.evolveLevel - 1) * MINUTES_PER_LEVEL;
    ck(!p.canEvolveNow(), "which really does hold its evolution back");
    p.ageMinutes = (uint32_t)(d.evolveLevel + EVO_PENALTY_LEVELS - 1) * MINUTES_PER_LEVEL;
    ck(p.canEvolveNow(), "and it evolves exactly a day late, not never");
  }

  // --- retiring one that HAS earned its farewell is free
  {
    Pet p; Party q; p.begin(); q.begin();
    p.dbgHatchAs(6, false);                 // Charizard: final form
    p.ageMinutes = FAREWELL_AGE_MIN + 60;
    p.fullness=p.joy=p.energy=p.hygiene=100;
    ck(p.canFarewellNow() && p.retireIsFree(),
       "a creature past three days in final form retires for nothing");
    p.startRetire();
    finish(p, q);
    ck(p.evoPenalty() == 0, "and hands the next creature no debt");
  }

  // --- it does not compound
  {
    Pet p; Party q; p.begin(); q.begin();
    for (int i = 0; i < 3; i++) {
      young(p, 4, 10);
      p.startRetire();
      finish(p, q);
    }
    ck(p.evoPenalty() == EVO_PENALTY_LEVELS,
       "three early retires in a row still cost one day, not three");
  }

  // --- a normal farewell clears a debt inherited from before
  {
    Pet p; Party q; p.begin(); q.begin();
    young(p, 4, 10);
    p.startRetire(); finish(p, q);
    ck(p.evoPenalty() == EVO_PENALTY_LEVELS, "debt taken on");
    p.dbgHatchAs(6, false);
    p.ageMinutes = FAREWELL_AGE_MIN + 60;
    p.fullness=p.joy=p.energy=p.hygiene=100;
    p.startFarewell(); finish(p, q);
    ck(p.evoPenalty() == 0, "and paid off by raising the next one properly");
  }

  // --- what cannot be retired
  {
    Pet p; Party q; p.begin(); q.begin();
    p.newEgg();
    ck(!p.canRetireNow(), "an egg cannot be retired");
    young(p, 4, 10);
    p.frozen = true;
    ck(!p.canRetireNow(), "nor can a revived companion");
    p.frozen = false;
    p.sleeping = true;
    ck(!p.canRetireNow(), "nor one that is asleep");
  }

  // --- and it survives a reload, debt included
  {
    Pet p; Party q; p.begin(); q.begin();
    young(p, 7, 12);
    p.startRetire(); finish(p, q);
    Pet again; again.begin();
    ck(again.evoPenalty() == EVO_PENALTY_LEVELS, "the debt is persisted");
    ck(again.isEgg(), "and so is the egg it came with");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
