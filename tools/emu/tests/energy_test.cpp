// A battle costs the LIVE pet energy, and a pet that cannot pay does not fight.
//
// This was documented for a long time -- "battling already costs the live pet
// energy, that, not a cooldown, is what rate-limits rematching" -- and never
// built: nothing deducted anything, and a sleeping creature could take on the
// whole ladder. Now:
//
//   * BATTLE_ENERGY_COST is charged ONCE, when a trainer or LAN fight actually
//     starts, and only if the live pet is in the squad;
//   * banked members fight for free, and so does the console's BATTLE;
//   * the live pet may join only if it is awake and
//     energy - BATTLE_ENERGY_COST > CARE_MISTAKE_STAT, so a fight on its own can
//     never be what counts a care mistake. Pet::canBattle() is the one answer,
//     and every caller below is driven through the real UI rather than asking
//     canBattle() directly, which would only prove the rule and not that the
//     screens obey it (CLAUDE.md, trap 1).
//
// Every check starts from a state it sets itself, and the ones phrased as "X
// does not happen" each sit beside one proving the mechanism engaged.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "moves.h"
#include "battle.h"
#include "link.h"
#include "i18n.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
uint32_t g_seed=8080; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
static std::vector<std::string> gIn; static size_t gAt = 0;
int FakeSerial::available(){ return gAt < gIn.size() ? 1 : 0; }
String FakeSerial::readStringUntil(char){
  if (gAt >= gIn.size()) return String("");
  return String(gIn[gAt++].c_str());
}
void setup(); void loop(); void render(); void handleSerial();
void onTap(int16_t,int16_t);
void battleTap(int16_t,int16_t);
void pickDefault(uint8_t cap);
uint8_t squadCap(uint8_t, bool);
uint8_t pickChosen();
const char *pickGoLabel();
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern Link lan;
extern bool battleOpen, btlOver, btlWon, btlPetIn, btlLink;
extern bool pickOpen, gymOpen, lanOpen, lanWantHost, pickHard, lanPetIn;
extern uint8_t pickTrainer, pickPage, btlMsgCount, btlMenu, btlSquadN;
extern uint32_t btlWinUntil;
extern Combatant btlYou;
extern Combatant btlSquad[];
extern uint16_t squadMask;
#define PICK_LAN 0xFF
#define PICK_X(i) (78 + ((i) % 2) * (150 + 10))
#define PICK_Y(i) (86 + ((i) / 2) * (74 + 6))
#define BTL_CELL_X(i) (69 + ((i) % 2) * 168)
#define BTL_CELL_Y(i) (274 + ((i) / 2) * 52)

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static const char *scr(){ return SCREEN_NAME[uiCurrentScreen()]; }

// the live pet at a known state: a creature strong enough to finish a fight
static void livePet(uint8_t energy){
  pet.dbgHatchAs(6, false);                  // CHARIZARD
  pet.ageMinutes = 100UL * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  pet.fullness = pet.joy = pet.hygiene = 90;
  pet.energy = energy;
  pet.careMistakes = 0;
  pet.saveNow();
}
static void bankOne(){
  PartyMon m; m.dex = 9; m.level = 60;       // BLASTOISE
  m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 25;
  party.replaceAt(0, m);
}
static void emptyParty(){ for (int i = 0; i < PARTY_SLOTS; i++) party.slots[i] = PartyMon(); }

// the gym list's own entry into the picker (see onTap, SCR_GYMS)
static void openPicker(uint8_t trainer){
  battleOpen = false; gymOpen = false; lanOpen = false;
  pickTrainer = trainer; pickHard = false; pickPage = 0;
  pickDefault(squadCap(trainer, false));
  pickOpen = true;
}
static void tapCell(int i){ onTap(PICK_X(i) + 40, PICK_Y(i) + 30); }
static void tapFight(){ onTap(300, 366); }

// the whole fight through the real tap handler, with no pet ticks in between
static void fightOut(){
  int guard = 0;
  while (battleOpen && guard++ < 600) {
    if (btlWinUntil || btlMsgCount || btlOver) { battleTap(233, 320); continue; }
    int slot = -1;
    for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
    if (slot < 0) break;
    if (btlMenu == 0) battleTap(233, BTL_CELL_Y(0) + 20);   // FIGHT
    battleTap(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
  }
}
static bool liveInSquad(){
  for (uint8_t i = 0; i < btlSquadN; i++)
    if (btlSquad[i].dex == pet.speciesId) return true;
  return false;
}

int main(){
  setup();
  for (int i = 0; i < 3; i++) loop();
  const uint8_t OK_AT = BATTLE_ENERGY_COST + CARE_MISTAKE_STAT + 1;   // 21
  const uint8_t NO_AT = BATTLE_ENERGY_COST + CARE_MISTAKE_STAT;       // 20

  // ---- 1. a gym fight with the live pet costs exactly the price, once
  livePet(90); emptyParty(); bankOne();
  openPicker(0);                                   // BROCK
  ck((squadMask & 1) != 0, "a rested live pet is selected by default");
  tapFight();
  ck(battleOpen && btlPetIn, "the gym fight starts with the live pet in it");
  ck(pet.energy == 90 - BATTLE_ENERGY_COST, "and it cost exactly BATTLE_ENERGY_COST");
  { Pet q; q.begin(); ck(q.energy == pet.energy, "the charge is SAVED, not just in RAM"); }
  fightOut();
  ck(!battleOpen, "the fight runs to its end");
  ck(pet.energy == 90 - BATTLE_ENERGY_COST, "and is charged ONCE, not per foe or per turn");

  // ---- 2. banked members only: free
  livePet(90); emptyParty(); bankOne();
  openPicker(0);
  ck((squadMask & 3) == 3, "precondition: both the live pet and the banked one are picked");
  tapCell(0);                                      // deselect the live pet
  ck(!(squadMask & 1) && (squadMask & 2), "the live pet is left at home");
  tapFight();
  ck(battleOpen && !btlPetIn && !liveInSquad(), "the fight starts with the banked member alone");
  ck(pet.energy == 90, "and costs the live pet nothing");
  fightOut();

  // ---- 3. the console's BATTLE is a debug entry, and free
  livePet(90); emptyParty();
  gIn = { "BATTLE 74 10" }; gAt = 0;
  while (gAt < gIn.size()) handleSerial();
  ck(battleOpen && btlPetIn, "console BATTLE starts a fight with the live pet");
  ck(pet.energy == 90, "and charges nothing");
  fightOut();

  // ---- 4. the boundary: cost + 11 may fight, cost + 10 may not
  livePet(OK_AT); emptyParty(); bankOne();
  openPicker(0);
  ck(pet.canBattle() && (squadMask & 1), "at cost + 11 the live pet is picked by default");
  tapFight();
  ck(battleOpen && btlPetIn, "and fights");
  ck(pet.energy == CARE_MISTAKE_STAT + 1, "landing on 11, one above the care-mistake line");
  fightOut();

  livePet(NO_AT); emptyParty(); bankOne();
  openPicker(0);
  ck(!pet.canBattle(), "at cost + 10 it may not");
  ck(!(squadMask & 1) && (squadMask & 2), "the picker's default leaves it out but keeps the banked one");
  tapCell(0);
  ck(!(squadMask & 1), "tapping its cell does not put it in");
  squadMask |= 1;                                  // a stale mask, forced
  ck(pickChosen() == 1, "a forced bit does not count as chosen");
  tapFight();
  ck(battleOpen && !btlPetIn && !liveInSquad(),
     "even forced into the mask, buildSquad() leaves it out of btlSquad");
  ck(pet.energy == NO_AT, "and it pays nothing for a fight it is not in");
  fightOut();

  // ---- 5. asleep: never, whatever its energy
  livePet(100); emptyParty(); bankOne();
  pet.sleeping = true;
  openPicker(0);
  ck(!(squadMask & 1), "an asleep live pet is not picked by default");
  tapCell(0);
  ck(!(squadMask & 1), "nor by tapping it");
  squadMask |= 1;
  tapFight();
  ck(battleOpen && !btlPetIn && !liveInSquad(), "nor by a forced mask");
  ck(pet.energy == 100, "and is not charged");
  fightOut();

  // ---- 6. the live pet is the ONLY candidate and may not fight: refused, with the reason
  livePet(NO_AT); emptyParty();
  openPicker(0);
  ck(!strcmp(pickGoLabel(), T(S_FIGHT_TIRED)), "the FIGHT button reads TIRED");
  tapFight();
  ck(!battleOpen && pickOpen && !strcmp(scr(), "pick"), "and refuses: no fight, still on the picker");
  ck(pet.energy == NO_AT, "nothing charged");
  pet.sleeping = true;
  ck(!strcmp(pickGoLabel(), T(S_FIGHT_ASLEEP)), "asleep, it reads ASLEEP");
  pet.sleeping = false; pet.energy = OK_AT;
  ck(!strcmp(pickGoLabel(), T(S_FIGHT)), "and FIGHT again once it can pay");
  pickOpen = false;

  // ---- 7. a whole gym from the threshold never counts a care mistake
  livePet(OK_AT); emptyParty();
  uint8_t mistakes = pet.careMistakes;
  openPicker(0);
  tapFight();
  ck(battleOpen && btlPetIn && btlSquadN == 1, "the live pet takes on BROCK alone from cost + 11");
  fightOut();
  ck(!battleOpen, "the whole fight is fought");
  // No pet tick runs inside fightOut(), so careMistakes could not move here
  // whatever the battle did -- that check would be vacuous. What tick() counts
  // is lowestStat() <= CARE_MISTAKE_STAT, so assert that predicate directly.
  ck(pet.energy == CARE_MISTAKE_STAT + 1 && pet.lowestStat() > CARE_MISTAKE_STAT,
     "it ends ABOVE the line tick() counts a care mistake at");
  ck(pet.careMistakes == mistakes, "and no care mistake was recorded");

  // ---- 8. LAN: charged when the fight starts, not when it is offered
  livePet(90); emptyParty(); bankOne();
  lanWantHost = true;
  openPicker(PICK_LAN);
  ck((squadMask & 1) != 0, "the LAN picker takes a rested live pet too");
  lan.begin(true, "T");
  tapFight();                                      // -> lanOffer()
  ck(lanOpen && lanPetIn && lan.mineN == 2, "the offer carries the live pet");
  ck(lan.state == LINK_REFUSED, "and pairing fails here (no radio)");
  ck(pet.energy == 90, "a failed pairing costs nothing");
  // pretend the pairing completed
  lan.theirs[0] = lan.mine[1]; lan.theirsN = 1;
  lan.state = LINK_READY;
  onTap(233, 240);                                 // FIGHT on the LAN screen
  ck(battleOpen && btlLink, "the linked fight starts");
  ck(pet.energy == 90 - BATTLE_ENERGY_COST, "and THAT is when it is charged");
  battleOpen = false; btlLink = false;

  // a rematch with a live pet that can no longer pay is refused, with the reason
  pet.energy = NO_AT;
  lanOpen = true; lan.state = LINK_READY;
  onTap(233, 240);
  ck(!battleOpen && lanOpen, "a LAN fight the live pet can no longer pay for does not start");
  ck(pet.energy == NO_AT, "and charges nothing");
  // banked-only offer: free
  pet.energy = 90;
  openPicker(PICK_LAN);
  tapCell(0);                                      // leave the live pet at home
  lan.begin(true, "T");
  tapFight();
  ck(lanOpen && !lanPetIn && lan.mineN == 1, "an offer without the live pet");
  lan.theirs[0] = lan.mine[0]; lan.theirsN = 1;
  lan.state = LINK_READY;
  onTap(233, 240);
  ck(battleOpen && btlLink && pet.energy == 90, "fights for free");
  battleOpen = false; btlLink = false;

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
