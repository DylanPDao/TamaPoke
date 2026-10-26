// What comes OUT of NVS is not trusted to index a table.
//
// A save can hold values this build cannot represent: loadBlob() deliberately
// supports flashing an older build (smaller DEX_COUNT) over a newer save, and
// an IMPORT block only has to pass its CRC. Every dex, move index and level
// read back is pinned here, and the state that is not saved but should be (the
// queued level-up offers) is checked to survive a reboot.
//
// Each section writes the BAD value through the Preferences stub exactly as a
// foreign build would have, then loads it with the real code.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "dex.h"
#include "moves.h"
#include "noart.h"
#include <cstdio>
#include <cstring>
#include <set>
uint32_t g_seed=21; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
static uint32_t g_ms=0; uint32_t millis(){return g_ms;}
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static bool validDex(int16_t d){ return d>=1 && d<=DEX_COUNT; }

// A save the player has been living in: some Pokedex, some badges, a name.
static void baseSave(Preferences &p){
  p.clear();
  p.putBool("init", true);
  p.putBool("stpk", false);
  uint8_t reg[(DEX_COUNT+7)/8] = {0};
  reg[0] = 0xFF;                         // dex 1..8 registered
  p.putBytes("dexreg", reg, sizeof(reg));
  p.putUShort("badg", 0x0007);
  p.putString("tnam", "RED");
  p.putUChar("reg", 0);                  // Kanto
}

int main(){
  // ---------------------------------------------------------------- item 1
  // A live creature from a bigger dex. It cannot be clamped to another species
  // (that would be a creature nobody raised), so it must come up as an egg --
  // and the player's own progress must come through untouched.
  for (int16_t badDex : { (int16_t)(DEX_COUNT + 91), (int16_t)0, (int16_t)32000 }) {
    {
      Preferences p; p.begin("tamapoke", false); baseSave(p);
      p.putShort("dexn", badDex);
      p.putShort("eggT2", 9999);
      p.putUInt("age", 50UL * MINUTES_PER_LEVEL);
      int16_t er[REGION_COUNT] = {0};
      er[0] = 9999; er[1] = -7; er[2] = 300;
      p.putBytes("eggR", er, sizeof(er));
      uint8_t q[8] = { 5, 0 };
      p.putBytes("mvq", q, sizeof(q));
      p.end();
    }
    Pet pet; pet.begin();
    char m[160];
    snprintf(m, sizeof(m), "dex %d on load: comes up as an egg, not as DEX_TBL[%d]",
             badDex, badDex);
    ck(pet.isEgg() && pet.speciesId == -1, m);
    ck(validDex(pet.eggPeek()), "  ...whose target is a real species");
    bool erOk = true;
    for (int i = 0; i < REGION_COUNT; i++)
      if (pet.eggByRegion[i] != 0 && !validDex(pet.eggByRegion[i])) erOk = false;
    ck(erOk, "  ...with no out-of-range eggByRegion entry left");
    ck(!pet.hasLearnOffer(), "  ...and no level-up offer on the egg");
    ck(pet.badges == 0x0007 && pet.isRegistered(1) && pet.isRegistered(8) &&
       !strcmp(pet.trainerName, "RED"),
       "  ...and the player's badges, Pokedex and name survive it");
    Preferences r; r.begin("tamapoke", true);
    ck(r.getShort("dexn", 0) == -1, "  ...and the recovery is what is now stored");
    r.end();
  }

  // A negative dex other than -1 is still an egg, but normalised.
  {
    {
      Preferences p; p.begin("tamapoke", false); baseSave(p);
      p.putShort("dexn", -50); p.putShort("eggT2", 25); p.end();
    }
    Pet pet; pet.begin();
    ck(pet.speciesId == -1 && pet.eggPeek() == 25,
       "dex -50 loads as a plain egg and keeps a valid target");
  }

  // An egg whose remembered answer for ANOTHER region is junk. setRegion()
  // promotes eggByRegion[r] straight into eggTarget, so a bad entry would be
  // hatched -- that path is what this checks, not just the array.
  {
    {
      Preferences p; p.begin("tamapoke", false); baseSave(p);
      p.putShort("dexn", -1);
      p.putShort("eggT2", 19);                 // a Rattata, in Kanto
      int16_t er[REGION_COUNT] = {0};
      er[0] = 19; er[1] = 30000;               // Johto: garbage
      p.putBytes("eggR", er, sizeof(er));
      p.end();
    }
    Pet pet; pet.begin();
    ck(pet.eggPeek() == 19, "a valid egg target is left alone");
    pet.setRegion(1);
    ck(validDex(pet.eggPeek()) && pet.eggPeek() >= REGIONS[1].lo &&
       pet.eggPeek() <= REGIONS[1].hi,
       "switching to a region whose stored answer was junk rolls a real Johto egg");
  }

  // An egg target past the dex, on an egg: re-rolled, not hatched. Forty
  // loads, because ONE common roll proves nothing -- an ordinary egg roll
  // (27%+ rare here) passed a single-load version of this by luck.
  {
    bool inRegion = true, allCommon = true, hatchOk = true;
    for (int i = 0; i < 40; i++) {
      {
        Preferences p; p.begin("tamapoke", false); baseSave(p);
        p.putShort("dexn", -1); p.putShort("eggT2", DEX_COUNT + 1 + i); p.end();
      }
      Pet pet; pet.begin();
      int16_t t = pet.eggPeek();
      if (!validDex(t) || t > REGIONS[0].hi) { inRegion = false; continue; }
      if (DEX_TBL[t].rarity != R_COMUN) allCommon = false;
      pet.dbgHatchAs(t, false);
      if (!validDex(pet.speciesId)) hatchOk = false;
    }
    ck(inRegion, "eggT2 past the dex is re-rolled inside the player's region");
    ck(allCommon,
       "  ...always as a common: a corrupted value is never a route to a rarer egg");
    ck(hatchOk, "  ...and hatches as a real species");
  }

  // ---------------------------------------------------------------- item 2
  {
    PartyMon ps[PARTY_SLOTS], bx[BOX_SLOTS];
    ps[0].dex = 25; ps[0].level = 300;
    ps[0].moves[0] = 5; ps[0].moves[1] = 200; ps[0].moves[2] = 255; ps[0].moves[3] = 3;
    ps[1].dex = 7; ps[1].level = 0;
    bx[0].dex = 1; bx[0].level = 65535;
    bx[0].moves[0] = MOVE_COUNT; bx[0].moves[1] = MOVE_COUNT - 1;
    {
      Preferences p; p.begin("tamapoke", false); baseSave(p);
      p.putShort("dexn", 25);
      p.putBytes("party", ps, sizeof(ps));
      p.putBytes("box", bx, sizeof(bx));
      p.end();
    }
    party.begin();
    const PartyMon &a = party.slots[0], &b = party.slots[1], &c = party.box[0];
    ck(a.dex == 25 && a.level == MAX_LEVEL, "a banked level 300 loads as MAX_LEVEL");
    ck(b.level == 1, "a banked level 0 loads as 1");
    ck(c.level == MAX_LEVEL, "a BOXED level 65535 is clamped by the same rule");
    ck(a.moves[0] == 5 && a.moves[1] == 0 && a.moves[2] == 0 && a.moves[3] == 3,
       "party move indices past MOVE_TBL become 0, valid ones stay");
    ck(c.moves[0] == 0 && c.moves[1] == MOVE_COUNT - 1,
       "box move indices: MOVE_COUNT dropped, MOVE_COUNT-1 kept");

    // The cast in combatantFromParty() is its own guard: a record built
    // anywhere but Party::begin() must not wrap 300 to 44.
    PartyMon raw; raw.dex = 25; raw.level = 300;
    Combatant cb; combatantFromParty(cb, raw);
    ck(cb.level == MAX_LEVEL, "combatantFromParty clamps a level 300 record to 100");
    raw.level = 0; combatantFromParty(cb, raw);
    ck(cb.level == 1, "  ...and a level 0 record to 1");
  }

  // ---------------------------------------------------------------- item 3
  // A neglected companion that is never going anywhere. 256 mistakes used to
  // wrap to 0, which re-qualifies for MED_FIT and erases every evolution delay.
  {
    { Preferences p; p.begin("tamapoke", false); baseSave(p); p.end(); }
    Pet pet; pet.begin();
    pet.dbgHatchAs(25, false);
    pet.careMistakes = 254;
    pet.fullness = pet.joy = pet.energy = pet.hygiene = 0;
    uint8_t lowest = 255;
    for (int i = 0; i < 200; i++) {
      g_ms += PET_TICK_MS;
      pet.update(g_ms);
      if (pet.careMistakes < lowest) lowest = pet.careMistakes;
    }
    ck(pet.careMistakes == 255 && lowest >= 254,
       "careMistakes saturates at 255 rather than wrapping to 0");
  }

  // ---------------------------------------------------------------- item 4
  // A queued level-up offer survives a reboot -- and a DECLINED one does not
  // come back, which is why it is persisted rather than rebuilt from the learnset.
  {
    { Preferences p; p.begin("tamapoke", false); baseSave(p); p.end(); }
    const int16_t DEX = 4;                    // Charmander
    // the first move it learns by LEVEL above 1, gated and not a TM
    uint8_t at = 0, target = 0;
    for (uint8_t i = 0; i < learnCount(DEX); i++) {
      uint8_t l = learnLevel(DEX, i);
      if (l >= 2) { at = l; target = learnMove(DEX, i); break; }
    }
    ck(at >= 2 && target, "found a level-gated move to queue");

    Pet pet; pet.begin();
    pet.dbgHatchAs(DEX, false);
    // a FULL moveset of other moves, so the gate queues an offer instead of
    // filling a free slot silently
    uint8_t w = 0;
    for (uint8_t mv = 1; mv < MOVE_COUNT && w < MOVE_SLOTS; mv++)
      if (mv != target) pet.moves[w++] = mv;
    pet.lastLearnLevel = at - 1;
    pet.ageMinutes = (uint32_t)(at - 1) * MINUTES_PER_LEVEL;
    pet.checkLearnGates();
    ck(pet.hasLearnOffer() && pet.learnOffer() == target, "the gate queued the offer");
    uint8_t queued = pet.learnQCount;
    pet.saveNow();

    {
      Pet again; again.begin();
      ck(again.hasLearnOffer() && again.learnOffer() == target &&
         again.learnQCount == queued,
         "after a reboot the same offers are still waiting");
      // decline every one of them, as a player would
      while (again.hasLearnOffer()) again.declineLearn();
    }
    {
      Pet third; third.begin();
      ck(!third.hasLearnOffer(),
         "a declined offer is NOT offered again on the next boot");
    }

    // hatch() resets the count without zeroing the array: the stale entries
    // must not be written out and resurrected.
    {
      Pet p4; p4.begin();
      p4.learnQueue[0] = target; p4.learnQueue[1] = target; p4.learnQCount = 0;
      p4.saveNow();
      Pet p5; p5.begin();
      ck(!p5.hasLearnOffer(), "entries past learnQCount are not saved as offers");
    }

    // junk and duplicates in the stored queue
    {
      Preferences p; p.begin("tamapoke", false);
      uint8_t q[8] = { 250, target, target, MOVE_COUNT, 0, 7, 0, 0 };
      p.putBytes("mvq", q, sizeof(q));
      p.end();
      Pet p6; p6.begin();
      ck(p6.learnQCount == 1 && p6.learnOffer() == target,
         "a stored queue drops junk indices and duplicates, and stops at the first 0");

      // An offer belongs to the creature that unlocked it. An ending with one
      // still waiting must not hand it to the egg (which would open the learn
      // screen on an egg) -- and now that the queue is saved, not across a
      // reboot either.
      p6.release();
      g_ms += CEREMONY_MS + 1000;
      p6.update(g_ms);
      ck(p6.isEgg(), "the release ended in an egg");
      ck(!p6.hasLearnOffer(), "  ...which carries no level-up offer");
    }
    // and an egg save that somehow holds a queue loads none
    {
      Preferences p; p.begin("tamapoke", false);
      uint8_t q[8] = { target, 0 };
      p.putShort("dexn", -1);
      p.putBytes("mvq", q, sizeof(q));
      p.end();
      Pet p7; p7.begin();
      ck(p7.isEgg() && !p7.hasLearnOffer(), "an egg never loads a stored offer");
    }
  }

  // ---------------------------------------------------------------- item 5
  // Every common that may hatch in the mixed pool must be reachable. The old
  // fixed candidate array (260) was exactly full, so one more generation would
  // have silently cut the highest dex numbers out of every egg.
  {
    int expect = 0; int16_t highest = 0;
    for (int16_t d = 1; d <= DEX_COUNT; d++)
      if (DEX_TBL[d].rarity == R_COMUN && speciesHasArt(d)) { expect++; highest = d; }
    Pet scratch;   // never begin()'d: rolls only, cannot write
    std::set<int16_t> seen;
    for (int i = 0; i < 30000; i++) seen.insert(scratch.rollInRegion(REGION_ALL, R_COMUN));
    printf("      mixed-pool commons with art: %d, distinct rolled: %zu\n",
           expect, seen.size());
    ck((int)seen.size() == expect, "every eligible common in REGION_ALL can be rolled");
    ck(seen.count(highest) == 1, "  ...including the highest-numbered one");
  }

  printf(bad ? "\n%d FAILED\n" : "\nall passed\n", bad);
  return bad ? 1 : 0;
}
