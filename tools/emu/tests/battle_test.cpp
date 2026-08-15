// Exercises the real battle engine: damage, type chart, STAB, stat stages,
// ailments, turn order, and a full fight to a KO.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "dex.h"
#include "types.h"
#include <cstdio>

uint32_t g_seed = 12345;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static void mk(Combatant &c, int16_t dex, uint8_t lvl) {
  Pet p;
  p.dbgHatchAs(dex, false);
  p.ivAtk = p.ivDef = p.ivSpe = p.ivHp = 31;
  p.ageMinutes = (uint32_t)(lvl - 1) * MINUTES_PER_LEVEL;
  p.relearnFromLevel();
  combatantFromPet(c, p);
}

static uint8_t findMove(const char *name) {
  for (uint8_t i = 1; i < MOVE_COUNT; i++)
    if (!strcmp(MOVE_TBL[i].name, name)) return i;
  return 0;
}

int main() {
  // --- stat stages: the series' own fractions
  ck(stagedStat(100, 0) == 100, "stage 0 is 1x");
  ck(stagedStat(100, 2) == 200, "stage +2 is 2x (SWORDS DANCE)");
  ck(stagedStat(100, 6) == 400, "stage +6 is 4x");
  ck(stagedStat(100, -1) == 66, "stage -1 is 2/3 (GROWL)");
  ck(stagedStat(100, -6) == 25, "stage -6 is 1/4");

  // --- type chart drives damage
  Combatant zard, blast, venu;
  mk(zard, 6, 50); mk(blast, 9, 50); mk(venu, 3, 50);
  uint8_t flame = findMove("FLAMETHROWER"), surf = findMove("SURF");
  uint16_t vsGrass = battleDamage(zard, venu, flame, false, 255);
  uint16_t vsWater = battleDamage(zard, blast, flame, false, 255);
  printf("     FLAMETHROWER: vs VENUSAUR %u, vs BLASTOISE %u\n", vsGrass, vsWater);
  ck(vsGrass > vsWater * 2, "fire hits grass far harder than water");

  // --- SWORDS DANCE really doubles physical output
  Combatant m1, m2;
  mk(m1, 68, 50); mk(m2, 68, 50);
  uint8_t chop = findMove("KARATE CHOP");
  uint16_t before = battleDamage(m1, m2, chop, false, 255);
  TurnLog lg;
  battleAct(m1, m2, findMove("SWORDS DANCE"), lg);
  uint16_t after = battleDamage(m1, m2, chop, false, 255);
  printf("     KARATE CHOP before %u, after SWORDS DANCE %u (stage %d)\n",
         before, after, m1.stage[SI_ATK]);
  ck(m1.stage[SI_ATK] == 2, "SWORDS DANCE sets +2 ATK");
  ck(after > before * 18 / 10, "and roughly doubles damage");

  // --- GROWL lowers the FOE, not the user
  Combatant g1, g2;
  mk(g1, 6, 50); mk(g2, 9, 50);
  battleAct(g1, g2, findMove("GROWL"), lg);
  ck(g2.stage[SI_ATK] == -1 && g1.stage[SI_ATK] == 0, "GROWL lowers the target's ATK only");

  // --- DRAGON DANCE moves two stats at once
  Combatant d1, d2;
  mk(d1, 6, 50); mk(d2, 9, 50);
  battleAct(d1, d2, findMove("DRAGON DANCE"), lg);
  ck(d1.stage[SI_ATK] == 1 && d1.stage[SI_SPE] == 1, "DRAGON DANCE raises ATK and SPE");

  // --- turn order follows speed, and priority beats it
  Combatant fast, slow;
  mk(fast, 65, 50);   // Alakazam, 120 base speed
  mk(slow, 143, 50);  // Snorlax, 30 base speed
  ck(battleMovesFirst(fast, flame, slow, flame), "the faster creature acts first");
  uint8_t quick = findMove("QUICK ATTACK");
  ck(battleMovesFirst(slow, quick, fast, flame), "QUICK ATTACK beats raw speed");

  // --- paralysis halves speed
  Combatant p1, p2;
  mk(p1, 65, 50); mk(p2, 65, 50);
  p1.ailment = AIL_PARA;
  ck(!battleMovesFirst(p1, flame, p2, flame), "paralysis loses the speed tie");

  // --- a fire type cannot be burned
  Combatant f1, f2;
  mk(f1, 9, 50); mk(f2, 6, 50);   // Blastoise attacking Charizard with fire
  bool burned = false;
  for (int i = 0; i < 400 && !burned; i++) {
    f2.ailment = AIL_NONE;
    f2.hp = f2.maxHp;
    battleAct(f1, f2, flame, lg);
    if (f2.ailment == AIL_BURN) burned = true;
  }
  ck(!burned, "a FIRE type never catches a burn");

  // --- burn chips and halves physical attack
  Combatant b1, b2;
  mk(b1, 68, 50); mk(b2, 68, 50);
  uint16_t clean = battleDamage(b1, b2, chop, false, 255);
  b1.ailment = AIL_BURN;
  uint16_t burnt = battleDamage(b1, b2, chop, false, 255);
  ck(burnt < clean * 6 / 10, "burn roughly halves physical damage");
  uint16_t hpWas = b1.hp;
  battleEndTurn(b1, lg);
  ck(b1.hp < hpWas, "burn chips at end of turn");

  // --- immunity: no damage at all, not chip
  Combatant gh, norm;
  mk(gh, 94, 50);    // Gengar, Ghost
  mk(norm, 143, 50); // Snorlax, Normal
  uint8_t slam = findMove("BODY SLAM");
  ck(battleDamage(norm, gh, slam, false, 255) == 0, "NORMAL does nothing to a GHOST");

  // --- a full fight terminates and someone wins
  Combatant A, B;
  mk(A, 6, 50); mk(B, 9, 50);
  int turn = 0;
  while (!A.fainted() && !B.fainted() && turn < 200) {
    turn++;
    uint8_t ma = A.moves[random(MOVE_SLOTS)], mb = B.moves[random(MOVE_SLOTS)];
    Combatant *first = &A, *second = &B;
    uint8_t mf = ma, ms = mb;
    if (!battleMovesFirst(A, ma, B, mb)) { first = &B; second = &A; mf = mb; ms = ma; }
    battleAct(*first, *second, mf, lg);
    if (!second->fainted()) battleAct(*second, *first, ms, lg);
    battleEndTurn(A, lg);
    battleEndTurn(B, lg);
  }
  printf("     fight ended on turn %d: CHARIZARD %u/%u, BLASTOISE %u/%u\n",
         turn, A.hp, A.maxHp, B.hp, B.maxHp);
  ck(turn < 200, "a fight reaches a conclusion");
  ck(A.fainted() || B.fainted(), "and somebody actually faints");

  // --- how long does a fight actually last? 6 matchups x 40 fights.
  {
    const int16_t roster[] = { 6, 9, 3, 65, 68, 143 };
    int total = 0, fights = 0, shortest = 999, longest = 0;
    for (int i = 0; i < 6; i++)
      for (int j = 0; j < 6; j++) {
        if (i == j) continue;
        for (int rep = 0; rep < 8; rep++) {
          Combatant X, Y;
          mk(X, roster[i], 50); mk(Y, roster[j], 50);
          int t = 0;
          while (!X.fainted() && !Y.fainted() && t < 200) {
            t++;
            uint8_t mx = X.moves[random(MOVE_SLOTS)], my = Y.moves[random(MOVE_SLOTS)];
            Combatant *f = &X, *sd = &Y; uint8_t mf = mx, ms = my;
            if (!battleMovesFirst(X, mx, Y, my)) { f = &Y; sd = &X; mf = my; ms = mx; }
            battleAct(*f, *sd, mf, lg);
            if (!sd->fainted()) battleAct(*sd, *f, ms, lg);
            battleEndTurn(X, lg); battleEndTurn(Y, lg);
          }
          total += t; fights++;
          if (t < shortest) shortest = t;
          if (t > longest) longest = t;
        }
      }
    printf("     %d fights at L50: average %.1f turns (shortest %d, longest %d)\n",
           fights, (double)total / fights, shortest, longest);
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
