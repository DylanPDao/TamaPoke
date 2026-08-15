// Walks one real life: hatch a Charmander, age it, evolve at each gate, and
// print the moveset at every step. Answers what actually happens to moves
// across an evolution, rather than assuming.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
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
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static void show(Pet &p, const char *when) {
  printf("%-22s %-11s L%-3u :", when, DEX_TBL[p.speciesId].name, p.level());
  for (int i = 0; i < MOVE_SLOTS; i++)
    printf(" %s", p.moves[i] ? MOVE_TBL[p.moves[i]].name : "-");
  printf("\n");
}

// what the species could learn by this level but does not know
static void pending(Pet &p) {
  uint8_t n = learnCount(p.speciesId), miss = 0;
  char buf[240];
  buf[0] = 0;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t at = learnLevel(p.speciesId, i);
    if (at == 0 || at > p.level()) continue;      // TMs are on-demand, skip
    uint8_t mv = learnMove(p.speciesId, i);
    if (p.knowsMove(mv)) continue;
    miss++;
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " %s@%u", MOVE_TBL[mv].name, at);
  }
  if (miss) printf("%-22s   MISSED level-up moves:%s\n", "", buf);
}

int main() {
  Pet p;
  p.dbgHatchAs(4, false);          // Charmander, the form that actually hatches
  p.ageMinutes = 0;
  show(p, "hatched");

  // healthy pet, so evolution is never blocked by low stats
  auto healthy = [&] { p.fullness = p.joy = p.energy = p.hygiene = 100; };
  for (uint32_t lvl : { 15u, 16u, 35u, 36u, 54u, 73u, 100u }) {
    p.ageMinutes = (lvl - 1) * MINUTES_PER_LEVEL;
    healthy();
    const char *note = "";
    if (p.canEvolveNow()) { p.evolve(); note = " (evolved)"; }
    p.checkLearnGates();
    while (p.hasLearnOffer()) {          // accept every offer into the last slot
      printf("%-22s   OFFER %s -> replacing %s\n", "",
             MOVE_TBL[p.learnOffer()].name,
             p.moves[MOVE_SLOTS-1] ? MOVE_TBL[p.moves[MOVE_SLOTS-1]].name : "-");
      p.acceptLearn(MOVE_SLOTS - 1);
    }
    char when[40];
    snprintf(when, sizeof(when), "level %u%s", lvl, note);
    show(p, when);
    pending(p);
  }
  return 0;
}
