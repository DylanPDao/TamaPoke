// Headless proof that a click reaches onTap(). Replaces main_sdl.cpp's main()
// and runtime globals, then drives the REAL setup()/loop()/handleTouch() by
// poking the same touch globals the SDL event loop pokes.
//
// Build twice: with and without -DNO_IRQ, to A/B the emuFireInterrupt() fix.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "moves.h"
#include "battle.h"
#include <chrono>
#include <thread>
#include <string>
#include <deque>

uint32_t g_seed = 0xC0FFEE;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;

// millis() deliberately NOT defined here: clock.cpp is linked in so this
// exercises the real emulator clock, --fast handling included.
void FakeESP::restart() { exit(0); }

// No stdin console in this harness: the serial path is not what is under test.
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void loop();
extern Pet pet;   // defined in the sketch
extern bool trainOpen, sackOpen, gameOpen, menuOpen, cardOpen, movePickOpen, spdOpen;
extern uint8_t movePickSlot, movePickPage;
extern bool battleOpen, btlOver, btlWon;
extern Combatant btlYou, btlFoe;
extern uint8_t btlMsgCount;
void startBattle(int16_t dex, uint8_t lvl);
void startTrainerBattle(uint8_t idx, bool hard);
extern uint8_t btlFoeAt, btlSquadN, btlSquadAt, btlMenu;
extern Combatant btlSquad[7];
extern int8_t btlSwapWho;
extern bool btlHard;
extern bool gymOpen, gymHard; extern uint8_t gymPage;
extern bool pickOpen, pickHard; extern uint8_t pickTrainer; extern uint16_t squadMask;
uint8_t squadCap(uint8_t idx, bool hard);
uint8_t pickChosen(); uint8_t pickCandidates(); void pickDefault(uint8_t);
extern uint8_t pickPage;
#define PICK_X(i) (78 + ((i) % 2) * 160)
#define PICK_Y(i) (86 + ((i) / 2) * 80)
#define BTL_CELL_X(i) (69 + ((i) % 2) * 168)
#define BTL_CELL_Y(i) (286 + ((i) / 2) * 52)
uint8_t learnableList(uint8_t *out, uint8_t max);
#define MOVE_PICK_PER_PAGE 5
#define MOVE_PICK_Y(i) (76 + (i) * 58)
extern uint8_t cardPage;

// handleTouch() self-gates to 50 Hz off millis(), so real time must pass
// between polls; a tight loop() spin would be swallowed by that gate.
static void pump(int times) {
  for (int i = 0; i < times; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    loop();
  }
}

static void fire() {
#ifndef NO_IRQ
  emuFireInterrupt();
#endif
}

// One press/release at (x,y), driven exactly as the SDL event loop drives it.
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true;
  fire();
  pump(5);                       // finger held: gesture starts
  g_touchDown = false;
  fire();
  pump(3);                       // finger up: gesture resolves as a tap
}

int main(int argc, char **argv) {
  if (argc > 1) emuSetTimeScale((uint32_t)atoi(argv[1]));
  printf("--- time scale x%u ---\n", emuTimeScale());
  setup();
  pump(4);

  if (!pet.awaitingStarter()) { printf("FAIL: not on the starter screen\n"); return 1; }
  printf("on starter screen, awaitingStarter=1\n");

  // Negative control: below the three rows (they span y 110..344). If this
  // "selects" a starter the test proves nothing about coordinate routing.
  click(233, 420);
  if (!pet.awaitingStarter()) { printf("FAIL: off-row click selected a starter\n"); return 1; }
  printf("click at (233,420) off the rows: correctly ignored\n");

  // row 0 == STARTER_DEX[0] == Bulbasaur. Centre of the row: x 70..396, y 110..180.
  click(233, 145);
  printf("click at (233,145) on row 0: awaitingStarter=%d\n", (int)pet.awaitingStarter());

  if (pet.awaitingStarter()) { printf("FAIL: click did not register\n"); return 1; }
  printf("PASS: tap reached onTap and picked a starter\n");

  // --fast must still do its job: with no finger down, game time has to run at
  // the scale. Otherwise the touch fix would have quietly neutered the flag.
  uint32_t t0 = millis();
  auto w0 = std::chrono::steady_clock::now();
  pump(20);                      // ~600 ms of real time, idle
  uint32_t gameMs = millis() - t0;
  uint64_t realMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - w0).count();
  double ratio = realMs ? (double)gameMs / (double)realMs : 0.0;
  printf("idle: %llu ms real -> %u ms game (x%.1f, want x%u)\n",
         (unsigned long long)realMs, gameMs, ratio, emuTimeScale());
  if (ratio < emuTimeScale() * 0.8 || ratio > emuTimeScale() * 1.2) {
    printf("FAIL: --fast no longer accelerates idle game time\n");
    return 1;
  }
  printf("PASS: --fast still accelerates idle game time\n");

  // ---- new UI routing: 5th icon -> training submenu -> the two live trainers,
  // and the menu's STATS row -> the stats card page. Both changed index
  // mappings inside existing handlers, which is where an off-by-one would hide.
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  pump(4);

  // The learn prompt is MODAL -- it swallows every tap until answered -- so a
  // creature with moves waiting will ignore the icons. That is correct
  // behaviour, and it started firing here as soon as dex_moves.py gained the
  // cheap early attacks: a level 1 creature now actually has things to learn.
  while (pet.hasLearnOffer()) pet.declineLearn();
  pump(2);
  click(341, 390);                       // 5th icon (dumbbell)
  if (!trainOpen) { printf("FAIL: train icon did not open the submenu\n"); return 1; }
  printf("PASS: 5th icon opens the training submenu\n");

  click(233, 178);                       // row 0 == STRENGTH == TRAIN_ROW_Y(0)+28
  if (!sackOpen) { printf("FAIL: STRENGTH row did not start the sack\n"); return 1; }
  printf("PASS: STRENGTH routes to the punching bag\n");
  sackOpen = false;

  click(341, 390);
  click(233, 242);                       // row 1 == SPEED == TRAIN_ROW_Y(1)+28
  if (!spdOpen) { printf("FAIL: SPEED row did not start the reaction test\n"); return 1; }
  printf("PASS: SPEED routes to the reaction test\n");
  spdOpen = false;

  click(341, 390);
  click(233, 306);                       // row 2 == DEFENCE -> the ball game
  if (!gameOpen) { printf("FAIL: DEFENCE row did not start the ball game\n"); return 1; }
  printf("PASS: DEFENCE routes to the ball game\n");
  gameOpen = false;
  trainOpen = false;

  click(233, 60);                        // name/status band opens the menu
  if (!menuOpen) { printf("FAIL: name band did not open the menu\n"); return 1; }
  click(233, 104 + 16 + 22);             // menu row 0 == STATS == MENU_ROW_Y(0)+22
  if (!cardOpen || cardPage != 1) {
    printf("FAIL: STATS row -> cardOpen=%d cardPage=%d (want 1,1)\n",
           (int)cardOpen, (int)cardPage);
    return 1;
  }
  printf("PASS: menu STATS row opens the stats card page\n");

  // ---- moves card page -> picker -> slot actually changes, with no duplicates
  cardPage = 2;
  uint8_t before = pet.moves[1];
  click(233, 154);                       // slot 1 == MOVE_ROW_Y(1)
  if (!movePickOpen) { printf("FAIL: tapping a move slot did not open the picker\n"); return 1; }
  printf("PASS: tapping a move slot opens the picker\n");

  click(233, 100);                       // first row of the picker
  if (movePickOpen) { printf("FAIL: picking a move left the picker open\n"); return 1; }
  if (pet.moves[1] == before) { printf("FAIL: slot 1 did not change\n"); return 1; }
  printf("PASS: picking a move replaces the slot (%s -> %s)\n",
         before ? MOVE_TBL[before].name : "-",
         pet.moves[1] ? MOVE_TBL[pet.moves[1]].name : "-");

  // the swap must never leave the same move in two slots
  bool dupe = false;
  for (int i = 0; i < MOVE_SLOTS; i++)
    for (int j = i + 1; j < MOVE_SLOTS; j++)
      if (pet.moves[i] && pet.moves[i] == pet.moves[j]) dupe = true;
  if (dupe) {
    printf("FAIL: a move ended up in two slots\n");
    for (int i = 0; i < MOVE_SLOTS; i++)
      printf("      slot %d: %s\n", i, pet.moves[i] ? MOVE_TBL[pet.moves[i]].name : "-");
    return 1;
  }
  printf("PASS: no duplicate move after the swap\n");

  // picking a move the pet already knows should TRADE slots, not clone it
  uint8_t s0 = pet.moves[0], s2 = pet.moves[2];
  cardPage = 2;
  click(233, 212);                       // slot 2
  uint8_t all[64];
  uint8_t n = learnableList(all, sizeof(all));
  int page = -1, row = -1;
  for (uint8_t i = 0; i < n; i++)
    if (all[i] == s0) { page = i / MOVE_PICK_PER_PAGE; row = i % MOVE_PICK_PER_PAGE; }
  if (page >= 0) {
    movePickPage = page;
    click(233, MOVE_PICK_Y(row) + 10);
    bool ok = (pet.moves[2] == s0 && pet.moves[0] == s2);
    printf("%s: picking a known move trades the two slots (0=%s 2=%s)\n",
           ok ? "PASS" : "FAIL",
           pet.moves[0] ? MOVE_TBL[pet.moves[0]].name : "-",
           pet.moves[2] ? MOVE_TBL[pet.moves[2]].name : "-");
    if (!ok) return 1;
  }
  // ---- level-up learn prompt: accept into a slot, and decline.
  // Past level 36, so LEER@15 / FLAMETHROWER@34 / WING ATTACK@36 are all in
  // range and more of them exist than there are slots.
  pet.ageMinutes = 40 * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  pet.lastLearnLevel = 0;
  pet.learnQCount = 0;
  pet.checkLearnGates();
  if (!pet.hasLearnOffer()) { printf("FAIL: no learn offer was queued\n"); return 1; }
  printf("PASS: crossing gates with a full moveset queues an offer\n");

  uint8_t offered = pet.learnOffer(), was2 = pet.moves[2];
  click(233, 104 + 2 * 56 + 10);          // LEARN_ROW_Y(2)
  if (pet.moves[2] != offered) {
    printf("FAIL: accepting did not put %s in slot 2 (got %s)\n",
           MOVE_TBL[offered].name, pet.moves[2] ? MOVE_TBL[pet.moves[2]].name : "-");
    return 1;
  }
  printf("PASS: accepting replaces the chosen slot (%s -> %s)\n",
         was2 ? MOVE_TBL[was2].name : "-", MOVE_TBL[offered].name);

  if (pet.hasLearnOffer()) {
    uint8_t before[MOVE_SLOTS];
    for (int i = 0; i < MOVE_SLOTS; i++) before[i] = pet.moves[i];
    uint8_t skipped = pet.learnOffer();
    click(233, 334 + 20);                 // LEARN_SKIP_Y
    bool same = true;
    for (int i = 0; i < MOVE_SLOTS; i++) if (pet.moves[i] != before[i]) same = false;
    if (!same || pet.knowsMove(skipped)) {
      printf("FAIL: declining changed the moveset\n"); return 1;
    }
    printf("PASS: declining %s leaves the moveset untouched\n", MOVE_TBL[skipped].name);
  }
  // ---- battle: drive a whole fight through the real tap handler
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.ageMinutes = 50 * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  startBattle(9, 50);
  if (!battleOpen) { printf("FAIL: battle did not start\n"); return 1; }
  printf("PASS: BATTLE opens (%s L%u vs %s L%u)\n",
         btlYou.name, btlYou.level, btlFoe.name, btlFoe.level);

  uint16_t foeHp0 = btlFoe.hp;
  int turns = 0;
  while (battleOpen && turns < 60) {
    turns++;
    if (btlMsgCount) { click(233, 320); continue; }   // clear narration
    int slot = -1;
    for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
    if (slot < 0) break;
    click(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
  }
  printf("     after %d taps: you %u/%u, foe %u/%u, over=%d won=%d\n",
         turns, btlYou.hp, btlYou.maxHp, btlFoe.hp, btlFoe.maxHp,
         (int)btlOver, (int)btlWon);
  if (btlFoe.hp >= foeHp0) { printf("FAIL: tapping a move did no damage\n"); return 1; }
  printf("PASS: tapping a move resolves a turn and deals damage\n");
  if (!btlOver) { printf("FAIL: fight never concluded\n"); return 1; }
  printf("PASS: the fight concludes and closes\n");

  // ---- a trainer fight: the foe's squad must chain, and winning awards a badge
  pet.ageMinutes = 100 * MINUTES_PER_LEVEL;   // strong enough to sweep Brock
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  bool hadBadge = pet.hasBadge(0, 0, false);
  startTrainerBattle(0, false);
  if (!battleOpen) { printf("FAIL: trainer battle did not start\n"); return 1; }
  printf("PASS: gym battle starts (squad of %u vs BROCK)\n", btlSquadN);

  int taps = 0;
  uint8_t sawFoeAt = 0;
  while (battleOpen && taps < 200) {
    taps++;
    if (btlMsgCount) { click(233, 320); continue; }
    int slot = -1;
    for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
    if (slot < 0) break;
    click(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
    if (btlFoeAt > sawFoeAt) sawFoeAt = btlFoeAt;
  }
  printf("     %d taps, foe reached team index %u, won=%d\n", taps, sawFoeAt, (int)btlWon);
  if (sawFoeAt == 0) { printf("FAIL: the second gym creature never came out\n"); return 1; }
  printf("PASS: the trainer sends out its next creature on a faint\n");
  if (!btlWon) { printf("FAIL: a L100 creature lost to Brock\n"); return 1; }
  if (!pet.hasBadge(0, 0, false) || hadBadge) { printf("FAIL: no badge awarded\n"); return 1; }
  printf("PASS: beating a leader awards its badge (%u/8)\n", pet.badgeCount(false));

  // ---- hard mode caps the team to the opponent's size AND level
  battleOpen = false;
  pet.ageMinutes = 100 * MINUTES_PER_LEVEL;   // a level 100 creature
  startTrainerBattle(0, true);                // BROCK: 2 mons, top level 14
  printf("     hard vs BROCK: squad %u, your lead is L%u (pet is L%u)\n",
         btlSquadN, btlYou.level, pet.level());
  if (btlYou.level != 14) {
    printf("FAIL: level not capped to the leader's top (got %u, want 14)\n", btlYou.level);
    return 1;
  }
  printf("PASS: hard mode caps your level to the leader's best\n");
  if (btlSquadN > 2) { printf("FAIL: squad not capped to 2\n"); return 1; }
  printf("PASS: hard mode caps your team size to the leader's\n");
  if (pet.level() != 100) { printf("FAIL: the stored pet was modified\n"); return 1; }
  printf("PASS: the stored creature is untouched (still L%u)\n", pet.level());
  battleOpen = false;

  // ---- switching mid-fight
  battleOpen = false; pickOpen = false; pet.badges = 0;
  for (int i = 0; i < 3; i++) { PartyMon m; m.dex = 9 + i * 20; m.level = 40;
    m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 25; party.replaceAt(i, m); }
  squadMask = 0xFFFF;
  startTrainerBattle(0, false);
  if (btlSquadN < 2) { printf("FAIL: squad too small to test switching\n"); return 1; }
  printf("PASS: battle starts with a squad of %u\n", btlSquadN);

  btlMenu = 0;
  click(150, 274 + 52 + 22);            // POKEMON: lower-LEFT cell now (RUN is right)
  if (btlMenu != 2) { printf("FAIL: POKEMON did not open the switch list\n"); return 1; }
  printf("PASS: POKEMON opens the switch list\n");

  uint8_t was = btlSquadAt;
  uint16_t swFoeHp0 = btlFoe.hp;
  click(69 + 40, 286 + 22);             // slot 0 = the one already out: inert
  if (btlSquadAt != was) { printf("FAIL: switching to the active creature was allowed\n"); return 1; }
  printf("PASS: you cannot switch to the one already out\n");

  btlMenu = 2;
  click(69 + 168 + 40, 286 + 22);       // slot 1
  if (btlSquadAt == was) { printf("FAIL: the switch did not happen\n"); return 1; }
  printf("PASS: switching brings on another creature (slot %u -> %u)\n", was, btlSquadAt);
  bool costTurn = (btlYou.hp < btlYou.maxHp) || (btlFoe.hp == swFoeHp0);
  printf("     foe hp %u -> %u, your new one %u/%u\n", swFoeHp0, btlFoe.hp, btlYou.hp, btlYou.maxHp);
  if (!costTurn) { printf("FAIL: switching was free -- the foe never acted\n"); return 1; }
  printf("PASS: switching spends the turn\n");
  battleOpen = false; btlMenu = 0;
  for (int i = 0; i < 3; i++) party.releaseAt(i);

  // ---- the gym ladder is sequential: only the next one may be entered
  battleOpen = false; pickOpen = false;
  pet.badges = 0;                       // nothing beaten yet
  gymOpen = true; gymHard = false; gymPage = 0;
  click(233, 110 + 1 * 50 + 20);        // MISTY, the second row
  if (pickOpen || battleOpen) { printf("FAIL: a locked leader was enterable\n"); return 1; }
  printf("PASS: a locked leader cannot be entered\n");
  click(233, 110 + 0 * 50 + 20);        // BROCK, the first
  if (!pickOpen) { printf("FAIL: the first leader was not enterable\n"); return 1; }
  printf("PASS: the first leader is always open\n");
  pickOpen = false;
  pet.badges = 1;                       // Brock beaten
  gymOpen = true; gymPage = 0;
  click(233, 110 + 1 * 50 + 20);        // MISTY again
  if (!pickOpen) { printf("FAIL: beating one did not unlock the next\n"); return 1; }
  printf("PASS: beating a leader unlocks the next\n");
  pickOpen = false;
  // and the hard ladder is its own run
  gymOpen = true; gymHard = true; gymPage = 0;
  click(233, 110 + 1 * 50 + 20);
  if (pickOpen) { printf("FAIL: easy progress unlocked the hard ladder\n"); return 1; }
  printf("PASS: hard mode keeps its own unlock order\n");
  gymHard = false; gymOpen = false; pet.badges = 0;

  // ---- a live pet plus a FULL party is 7 candidates against a cap of 6. The
  // 7th used to be counted but never drawn and never tappable, so FIGHT sat
  // inert with no way to fix it.
  battleOpen = false; pickOpen = false;
  for (int i = 0; i < PARTY_SLOTS; i++) { PartyMon m; m.dex = 9 + i * 10; m.level = 40;
    m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 25; party.replaceAt(i, m); }
  pickTrainer = 0; pickHard = false; pickPage = 0;
  pickDefault(squadCap(0, false));
  printf("     candidates=%u chosen=%u cap=%u\n",
         pickCandidates(), pickChosen(), squadCap(0, false));
  if (pickCandidates() != PARTY_SLOTS + 1) { printf("FAIL: expected 7 candidates\n"); return 1; }
  printf("PASS: a live pet plus a full party is 7 candidates\n");
  if (pickChosen() > squadCap(0, false)) {
    printf("FAIL: the picker opens over its own cap\n"); return 1; }
  printf("PASS: it opens with a valid selection, not everything\n");
  { uint8_t pages = (pickCandidates() + 6 - 1) / 6;
    if (pages < 2) { printf("FAIL: 7 candidates must span 2 pages\n"); return 1; }
    printf("PASS: the 7th is reachable on page %u of %u\n", pages, pages); }
  for (int i = 0; i < PARTY_SLOTS; i++) party.releaseAt(i);
  squadMask = 0xFFFF;

  // ---- team select: cap enforcement and toggling
  battleOpen = false;
  for (int i = 0; i < 5; i++) { PartyMon m; m.dex = 9 + i; m.level = 40;
    m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 25; party.replaceAt(i, m); }
  squadMask = 0xFFFF;
  pickTrainer = 0; pickHard = true; pickOpen = true;   // BROCK: cap of 2
  uint8_t cap = squadCap(0, true);
  // x=300: FIGHT sits right of centre now, with BACK to its left. Tapping 233
  // lands in the gap between them and hits nothing, which made the next check
  // pass for the wrong reason.
  click(300, 366);                                     // FIGHT with 6 chosen
  if (battleOpen || !pickOpen) { printf("FAIL: FIGHT fired while over the cap\n"); return 1; }
  printf("PASS: FIGHT is inert while over the cap (%u chosen, cap %u)\n", 6, cap);

  // deselect down to the cap, then it should start
  for (int slot = 5; slot >= (int)cap; slot--)
    click(PICK_X(slot) + 40, PICK_Y(slot) + 30);
  click(300, 366);
  if (!battleOpen) { printf("FAIL: FIGHT did not start at the cap\n"); return 1; }
  printf("PASS: trimming to the cap lets the fight start (squad %u)\n", btlSquadN);
  if (btlSquadN > cap) { printf("FAIL: squad exceeded the cap\n"); return 1; }
  printf("PASS: the squad respects the chosen team\n");
  battleOpen = false;
  squadMask = 0xFFFF;
  for (int i = 0; i < 5; i++) party.releaseAt(i);

  // easy caps the level too -- it just does not cap the team size
  startTrainerBattle(0, false);
  if (btlYou.level != 14) { printf("FAIL: easy mode did not cap the level\n"); return 1; }
  printf("PASS: easy mode caps the level as well\n");
  return 0;
}
