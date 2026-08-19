// Move storage: learnset population, save/load round-trip, backfill of a save
// made before moves existed, and the party blob migration. Asserts against the
// real Pet/Party rather than restating their rules.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "dex.h"
#include "moves.h"
#include <cstdio>

uint32_t g_seed = 0xC0FFEE;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
static uint32_t g_ms = 0;
uint32_t millis() { return g_ms; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}
static void dump(const char *tag, const uint8_t *mv) {
  printf("     %s:", tag);
  for (int i = 0; i < MOVE_SLOTS; i++)
    printf(" %s", mv[i] ? MOVE_TBL[mv[i]].name : "-");
  printf("\n");
}

int main() {
  // --- a level-100 Charizard should know four real, distinct moves
  Pet p;
  p.dbgHatchAs(6, false);
  p.ageMinutes = 5940;                 // level 100
  p.relearnFromLevel();
  dump("Charizard L100", p.moves);
  ck(p.moveCount() == 4, "L100 Charizard knows 4 moves");
  bool distinct = true, valid = true;
  for (int i = 0; i < MOVE_SLOTS; i++) {
    if (p.moves[i] >= MOVE_COUNT) valid = false;
    for (int j = i + 1; j < MOVE_SLOTS; j++)
      if (p.moves[i] && p.moves[i] == p.moves[j]) distinct = false;
  }
  ck(distinct, "no duplicate moves");
  ck(valid, "every move is a valid MOVE_TBL index");

  // --- a freshly hatched pet knows fewer, and never more than it should
  Pet baby;
  baby.dbgHatchAs(6, false);
  baby.ageMinutes = 0;                 // level 1
  baby.relearnFromLevel();
  dump("Charizard L1  ", baby.moves);
  ck(baby.moveCount() >= 1, "a level 1 pet still knows at least one move");
  ck(baby.moveCount() <= 4, "never exceeds 4 slots");

  // every known move must actually be learnable at or below its level
  bool legal = true;
  for (int i = 0; i < MOVE_SLOTS; i++) {
    if (!baby.moves[i]) continue;
    bool found = false;
    for (uint8_t k = 0; k < learnCount(6); k++)
      if (learnMove(6, k) == baby.moves[i] && learnLevel(6, k) <= 1) found = true;
    if (!found) legal = false;
  }
  ck(legal, "a level 1 pet knows nothing it has not learned yet");

  // --- a default set must be mostly attacks, not a pile of stat-lowering
  // status moves. This is what caught GROWL/LEER on a level 100 Charizard.
  for (int dex : { 6, 9, 3, 65, 68, 25, 143, 150 }) {
    Pet q;
    q.dbgHatchAs(dex, false);
    q.ageMinutes = 5940;
    q.relearnFromLevel();
    int atk = 0, stab = 0;
    for (int i = 0; i < MOVE_SLOTS; i++) {
      if (!q.moves[i]) continue;
      const MoveEntry &m = MOVE_TBL[q.moves[i]];
      if (m.cat != MC_STATUS) atk++;
      if (m.type == DEX_TBL[dex].type1 || m.type == DEX_TBL[dex].type2) stab++;
    }
    printf("     %-11s atk=%d stab=%d :", DEX_TBL[dex].name, atk, stab);
    for (int i = 0; i < MOVE_SLOTS; i++)
      printf(" %s", q.moves[i] ? MOVE_TBL[q.moves[i]].name : "-");
    printf("\n");
    if (atk < 3) { printf("FAIL  %s has fewer than 3 attacks\n", DEX_TBL[dex].name); bad++; }
    if (stab < 1) { printf("FAIL  %s has no same-type move\n", DEX_TBL[dex].name); bad++; }
  }
  ck(true, "default sets are attack-led with STAB (see above)");

  // --- learn candidates are offered, and never ones already known
  uint8_t cand[8];
  uint8_t n = p.pendingLearnables(cand, 8);
  bool alreadyKnown = false;
  for (uint8_t i = 0; i < n; i++)
    if (p.knowsMove(cand[i])) alreadyKnown = true;
  printf("     L100 learnables not yet known: %u\n", n);
  ck(!alreadyKnown, "pendingLearnables never offers a known move");

  // --- party migration: a blob written in the OLD layout (no moves[]) must
  // survive, with slots still aligned. This is the case that silently
  // corrupted the party if migrated by a plain getBytes().
  const size_t oldStride = sizeof(PartyMon) - MOVE_SLOTS;
  uint8_t legacy[PARTY_SLOTS * (sizeof(PartyMon))];
  for (int i = 0; i < PARTY_SLOTS; i++) {
    PartyMon m;
    m.dex = 1 + i * 20;                // 1, 21, 41, 61, 81, 101
    m.level = 40 + i;
    m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
    snprintf(m.nick, sizeof(m.nick), "OLD%d", i);
    memcpy(legacy + i * oldStride, &m, oldStride);   // old records, old stride
  }
  Preferences seed;
  seed.begin("tamapoke", false);
  seed.putBytes("party", legacy, PARTY_SLOTS * oldStride);
  seed.end();

  Party pty;
  pty.begin();
  bool aligned = true;
  for (int i = 0; i < PARTY_SLOTS; i++) {
    if (pty.slots[i].dex != 1 + i * 20 || pty.slots[i].level != 40 + i) aligned = false;
    printf("     slot %d: dex=%d lvl=%u nick=%s\n",
           i, pty.slots[i].dex, pty.slots[i].level, pty.slots[i].nick);
  }
  ck(aligned, "legacy party blob migrates with slots still aligned");
  ck(pty.count() == PARTY_SLOTS, "all 6 legacy members survive");

  // The MOVE PICKER had its own gate and so its own opinion: it checked
  // learnLevel() alone, and a TM is stored as level 0, so a level 22 Charmeleon
  // was offered FIRE BLAST (110 power). Found by hand on the board, exactly
  // like the level 1 Squirtle holding SURF -- the same bug in the one path that
  // fix never reached. moveUnlockLevel() is the single answer now.
  {
    int zero = 0, tooEarly = 0;
    for (int16_t d = 1; d <= DEX_COUNT; d++)
      for (uint8_t i = 0; i < learnCount(d); i++) {
        uint8_t at = moveUnlockLevel(d, i);
        if (at == 0) zero++;
        if (learnLevel(d, i) == 0 && at < 20) tooEarly++;
      }
    ck(zero == 0, "no learnset entry anywhere unlocks at level 0");
    ck(tooEarly == 0, "and no TM is reachable before a creature is built");

    const int16_t CHARMELEON = 5;
    bool early = false, late = false, ember1 = false, flame34 = false;
    for (uint8_t i = 0; i < learnCount(CHARMELEON); i++) {
      uint8_t mv = learnMove(CHARMELEON, i);
      if (mv >= MOVE_COUNT) continue;
      uint8_t at = moveUnlockLevel(CHARMELEON, i);
      if (!strcmp(MOVE_TBL[mv].name, "FIRE BLAST")) { early = at <= 22; late = at >= 40; }
      if (!strcmp(MOVE_TBL[mv].name, "EMBER")) ember1 = (at == 1);
      if (!strcmp(MOVE_TBL[mv].name, "FLAMETHROWER")) flame34 = (at == 34);
    }
    ck(!early, "a level 22 Charmeleon is NOT offered FIRE BLAST");
    ck(late, "it waits for the TM level like every other TM");
    ck(ember1 && flame34,
       "while its real level-up moves keep their real levels (EMBER 1, FLAMETHROWER 34)");
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
