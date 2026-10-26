// Who replaces a fainted creature, on both sides of a fight.
//
// The replacement used to be "the next index" -- `btlSquadAt + 1 < btlSquadN`
// to decide, `btlSquadAt++` to send it -- which is only right if nobody ever
// switches. Two ways it went wrong for the player:
//
//   A. switch from slot 0 to slot 2, lose slot 2: 3 < 3 is false, so the fight
//      was LOST while slots 0 and 1 were untouched.
//   B. 0 -> switch to 1 -> 1 faints, 2 comes in -> switch back to 0 -> 0 faints:
//      ++ sent in slot 1, which was already at 0 HP.
//
// And for a linked opponent, whose team switches like ours: the replacement
// only scanned FORWARD, so with every later slot down and an earlier one alive
// the swap was cleared with the foe still fainted, and the fight never ended.
// Plus the two that ride on the same rule: both sides down in one turn, and a
// squad of six whose last two members the 2x2 switch grid could not list.
//
// Everything is driven through battleTap(), the way a player reaches it. The
// foe is given DRAGON RAGE alone -- flat 40, acc 100, so it never misses and
// never depends on stats -- which makes every faint below deterministic.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "moves.h"
#include "battle.h"
#include "link.h"
#include "types.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=777; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}

void setup(); void loop(); void render();
void battleTap(int16_t,int16_t);
void startBattle(int16_t dex, uint8_t lvl);
void startLinkBattle();
int btlCellIndexAt(int16_t x, int16_t y);
int btlSwitchCellAt(int16_t x, int16_t y);
extern Pet pet;
extern Link lan;
extern bool battleOpen, btlOver, btlWon, btlLink, btlLinkHost;
extern uint8_t btlMenu, btlMsgCount, btlSquadN, btlSquadAt, btlFoeAt, btlFoeSquadN, btlMyAct;
extern int8_t btlSwapWho;
extern Combatant btlYou, btlFoe;
extern Combatant btlSquad[];
extern Combatant btlFoeSquad[];

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// The centre of a cell, found by asking the FIRMWARE's own hit test rather
// than restating its geometry here -- a copy of the rectangles would drift.
static bool centre(int (*at)(int16_t,int16_t), int idx, int &cx, int &cy) {
  long sx = 0, sy = 0, n = 0;
  for (int y = 0; y < 466; y++)
    for (int x = 0; x < 466; x++)
      if (at(x, y) == idx) { sx += x; sy += y; n++; }
  if (!n) return false;
  cx = (int)(sx / n); cy = (int)(sy / n);
  return true;
}

static uint8_t moveNamed(const char *nm) {
  for (int i = 1; i < MOVE_COUNT; i++) if (!strcmp(MOVE_TBL[i].name, nm)) return (uint8_t)i;
  return 0;
}
static uint8_t DR = 0;   // DRAGON RAGE

static void tapSwitch(int slot) {
  int x, y;
  btlMenu = 2;
  if (!centre(btlSwitchCellAt, slot, x, y)) { ck(false, "switch cell exists"); return; }
  battleTap((int16_t)x, (int16_t)y);
}
static void tapMove(int slot) {
  int x, y;
  btlMenu = 1;
  if (!centre(btlCellIndexAt, slot, x, y)) { ck(false, "move cell exists"); return; }
  battleTap((int16_t)x, (int16_t)y);
}
// one tap per queued message; each may bring a flagged replacement on
static void clearMessages() {
  for (int g = 0; g < 10 && btlMsgCount && !btlOver; g++) battleTap(233, 320);
}

static void setParty(const int16_t *dex, int n) {
  for (int i = 0; i < PARTY_SLOTS; i++) { PartyMon e; ::party.replaceAt(i, e); }
  for (int i = 0; i < n; i++) {
    PartyMon m; m.dex = dex[i]; m.level = 10;
    m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
    ::party.replaceAt(i, m);
  }
}

// A one-off fight against a level 100 SNORLAX that only knows DRAGON RAGE.
static void fight() {
  startBattle(143, 100);
  for (int i = 0; i < MOVE_SLOTS; i++) btlFoe.moves[i] = 0;
  btlFoe.moves[0] = DR;
}

static LinkMon mon(int16_t dex, uint8_t lvl, const char *nm) {
  Pet p; p.dbgHatchAs(dex, false);
  p.ivAtk = p.ivDef = p.ivSpe = p.ivHp = 25;
  p.ageMinutes = (uint32_t)(lvl - 1) * MINUTES_PER_LEVEL;
  p.relearnFromLevel();
  Combatant c; combatantFromPet(c, p);
  snprintf(c.name, sizeof(c.name), "%s", nm);
  LinkMon m; linkMonFrom(m, c); return m;
}
static void capture(void*, const uint8_t*, uint8_t) {}

int main(){
  setup();
  for (int i = 0; i < 3; i++) loop();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  pet.dbgHatchAs(4, false);                      // CHARMANDER
  pet.ageMinutes = 9UL * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();

  DR = moveNamed("DRAGON RAGE");
  ck(DR != 0, "DRAGON RAGE is in the move table");
  // nobody below may be immune to it, or the faints would not happen
  const int16_t three[] = { 7, 1 };              // SQUIRTLE, BULBASAUR
  ck(typeEffVsDex(T_DRAGON, 4) && typeEffVsDex(T_DRAGON, 7) && typeEffVsDex(T_DRAGON, 1),
     "the squad takes DRAGON RAGE");

  // ---- A: switch 0 -> 2, lose 2. Slots 0 and 1 are still standing.
  setParty(three, 2);
  fight();
  ck(battleOpen && btlSquadN == 3, "A: a squad of three");
  btlSquad[2].hp = 1;                            // the switch-in turn finishes it
  tapSwitch(2);
  ck(btlSquadAt == 2 && btlYou.fainted(), "A: slot 2 came in and went down");
  ck(!btlOver, "A: the fight is NOT lost with slots 0 and 1 healthy");
  ck(btlSwapWho == 0, "A: a replacement is due");
  clearMessages();
  ck(btlSquadAt != 2 && !btlYou.fainted() && battleOpen,
     "A: a healthy creature from BEFORE slot 2 comes on");
  printf("      A: replacement is slot %u (%s, %u hp)\n", btlSquadAt, btlYou.name, btlYou.hp);

  // ---- B: 0 -> 1 (faints) -> 2 comes on -> back to 0 (faints): not slot 1
  setParty(three, 2);
  fight();
  btlSquad[1].hp = 1;
  tapSwitch(1);
  ck(btlSquadAt == 1 && btlYou.fainted(), "B: slot 1 came in and went down");
  clearMessages();
  ck(btlSquadAt == 2 && !btlYou.fainted(), "B: slot 2 replaces it");
  btlSquad[0].hp = 1;
  tapSwitch(0);
  ck(btlSquadAt == 0 && btlYou.fainted(), "B: back to slot 0, which goes down too");
  ck(!btlOver, "B: slot 2 is still standing, so the fight goes on");
  clearMessages();
  ck(btlSquadAt == 2, "B: the replacement is slot 2, not the fainted slot 1");
  ck(!btlYou.fainted(), "B: and it is actually standing");

  // The companion: the rule must still END the fight when nobody is left,
  // or the checks above could pass by never deciding anything.
  btlYou.hp = 1;
  btlYou.moves[0] = DR;
  btlMsgCount = 0;
  tapMove(0);
  ck(btlOver && !btlWon, "B: the last one down loses the fight");
  ck(btlSwapWho < 0, "B: with nobody flagged to come on");

  // ---- the link foe: its team switches, so an earlier slot can be the last
  {
    lan.begin(true, "HOST");
    snprintf(lan.peerName, sizeof(lan.peerName), "%s", "RIVAL");
    lan.addMon(mon(9, 50, "SHELL"));
    lan.theirs[0] = mon(6, 50, "BLAZE");
    lan.theirs[1] = mon(65, 50, "SPOON");
    lan.theirs[2] = mon(25, 50, "ZAP");
    lan.theirsN = 3;
    lan.state = LINK_READY;
    lan.send = capture;
    startLinkBattle();
    ck(battleOpen && btlLink && btlLinkHost && btlFoeSquadN == 3,
       "L: the host holds the rival's three");
    ck(typeEffVsDex(T_DRAGON, 6) && typeEffVsDex(T_DRAGON, 65) && typeEffVsDex(T_DRAGON, 25),
       "L: none of them is immune to DRAGON RAGE");
    for (int i = 0; i < MOVE_SLOTS; i++) btlYou.moves[i] = 0;
    btlYou.moves[0] = DR;
    for (int k = 0; k < 3; k++) btlFoeSquad[k].moves[0] = 0;   // they only take it
    btlFoe.moves[0] = 0;
    btlFoeSquad[2].hp = 0;           // the rival's LAST slot is already down
    btlFoeSquad[1].hp = 1;

    // the rival switches 0 -> 1 and we knock 1 out on the same turn
    lan.pendingAct = LINK_ACT_SWITCH_TO(1);
    tapMove(0);
    ck(btlFoeAt == 1 && btlFoe.fainted(), "L: the rival's slot 1 went down");
    ck(!btlOver, "L: its slot 0 is alive, so the fight goes on");
    ck(btlSwapWho == 1, "L: a replacement is due");
    clearMessages();
    ck(btlFoeAt == 0 && !btlFoe.fainted(),
       "L: slot 0, BEFORE the fainted one, is sent back in");
    ck(btlSwapWho < 0 && !btlOver, "L: and the fight can carry on");

    // now everyone is down: the fight must end, not wait on a swap forever
    btlFoe.hp = 1;
    btlFoe.moves[0] = 0;
    lan.pendingAct = LINK_ACT_MOVE(0);
    btlMsgCount = 0;
    tapMove(0);
    ck(btlFoe.fainted(), "L: the rival's last creature goes down");
    ck(btlOver && btlWon, "L: and the fight ends as a win");
    battleOpen = false; btlLink = false;
  }

  // ---- both down in one turn (recoil): the foe's replacement comes on, and
  // then yours -- not a fainted creature left standing in your corner
  {
    uint8_t dedge = moveNamed("DOUBLE-EDGE");
    lan.begin(true, "HOST");
    snprintf(lan.peerName, sizeof(lan.peerName), "%s", "RIVAL");
    lan.addMon(mon(9, 50, "SHELL"));
    lan.addMon(mon(3, 50, "VINE"));
    lan.theirs[0] = mon(6, 50, "BLAZE");
    lan.theirs[1] = mon(65, 50, "SPOON");
    lan.theirsN = 2;
    lan.state = LINK_READY;
    lan.send = capture;
    startLinkBattle();
    ck(dedge && btlSquadN == 2 && btlFoeSquadN == 2, "K: two a side, and DOUBLE-EDGE exists");
    for (int i = 0; i < MOVE_SLOTS; i++) btlYou.moves[i] = 0;
    btlYou.moves[0] = dedge;
    btlFoe.moves[0] = 0;
    btlYou.hp = 1; btlFoe.hp = 1;
    lan.pendingAct = LINK_ACT_MOVE(0);
    tapMove(0);
    ck(btlYou.fainted() && btlFoe.fainted(), "K: the hit and its recoil drop both");
    ck(!btlOver, "K: each side has another, so the fight goes on");
    clearMessages();
    ck(btlFoeAt == 1 && !btlFoe.fainted(), "K: the rival's replacement is in");
    ck(btlSquadAt == 1 && !btlYou.fainted(), "K: and so is yours");
    battleOpen = false; btlLink = false;
  }

  // ---- a full squad of six: members 5 and 6 can be sent in
  {
    const int16_t five[] = { 7, 1, 16, 19, 25 };
    setParty(five, 5);
    fight();
    for (int i = 0; i < MOVE_SLOTS; i++) btlFoe.moves[i] = 0;   // it never hits
    ck(btlSquadN == 6, "6: a squad of six");
    tapSwitch(5);
    ck(btlSquadAt == 5 && !strcmp(btlYou.name, btlSquad[5].name),
       "6: the sixth member can be switched in");
    clearMessages();
    tapSwitch(4);
    ck(btlSquadAt == 4, "6: and so can the fifth");
    clearMessages();
    btlMenu = 2;
    render();                                    // all six drawn, nothing breaks
    ck(battleOpen && btlMenu == 2, "6: the six-slot switch list renders");
    battleOpen = false;
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
