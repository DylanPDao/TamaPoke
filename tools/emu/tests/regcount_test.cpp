// registeredCount() is cached: it walks every one of DEX_COUNT bits, and the
// egg screen, the player card, the Pokedex chooser and the menu all ask for it
// every frame. A cache is only as good as its invalidation, so this compares
// it against an independent recount after every kind of change the Pokedex
// bitmaps can go through -- and, so the comparison cannot pass by the cache
// never being used, also checks that it IS used between changes.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "save.h"
#include <cstdio>
uint32_t g_seed=21; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
static uint32_t g_ms=1000; uint32_t millis(){return g_ms;}
void FakeESP::restart(){}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static uint16_t recount(const Pet &p){
  uint16_t n = 0;
  for (int d = 1; d <= DEX_COUNT; d++) if (p.isRegistered(d)) n++;
  return n;
}
static void same(const Pet &p, const char *after){
  char w[96]; snprintf(w, sizeof(w), "after %s the count matches a recount (%u)", after, recount(p));
  ck(p.registeredCount() == recount(p), w);
}

int main(){
  Pet p; p.begin();
  same(p, "a fresh begin()");
  if (p.awaitingStarter()) p.chooseStarter(4);

  p.eggTap(); p.eggTap(); p.eggTap();            // hatch -> registerSpecies
  same(p, "a hatch");
  uint16_t before = p.registeredCount();
  p.dbgHatchAs(150, false);
  same(p, "registering a new species");
  ck(p.registeredCount() == before + 1, "and it went up by exactly one");
  p.dbgHatchAs(150, false);
  same(p, "registering one already registered");

  p.ageMinutes = 20UL * MINUTES_PER_LEVEL;      // an evolution registers too
  p.dbgHatchAs(4, false);
  p.ageMinutes = 20UL * MINUTES_PER_LEVEL; p.careMistakes = 0;
  p.fullness = p.joy = p.energy = p.hygiene = 90; p.sleeping = false;
  p.evolve();
  same(p, "an evolution");

  PartyMon m; m.dex = 300; m.level = 30;
  p.reviveFrom(m);
  same(p, "a revive");

  // the cache really is a cache: a bit poked in WITHOUT telling it is not seen
  // until dexChanged() -- proving the answers above came through invalidation,
  // not through a recount on every call
  uint16_t cached = p.registeredCount();
  p.dexReg[(400 - 1) >> 3] |= 1 << ((400 - 1) & 7);
  ck(p.registeredCount() == cached, "between changes the cached value is used");
  p.dexChanged();
  same(p, "dexChanged() after a direct write");

  // a reload: what is in NVS, not what was in RAM
  p.saveNow();
  { Pet q; q.begin(); same(q, "a load from NVS"); ck(q.registeredCount() == p.registeredCount(), "and it is the saved Pokedex"); }

  // an import: the restore replaces the save, then the board reboots into it
  uint8_t blob[2048];
  size_t n = saveExport(blob, sizeof(blob));
  ck(n > 0, "export works");
  for (int d = 500; d < 520; d++) p.dbgHatchAs(d, false);   // grows past the export
  same(p, "twenty more species");
  ck(saveImport(blob, n), "import works");
  { Pet q; q.begin(); same(q, "booting into an import"); ck(q.registeredCount() + 20 == p.registeredCount(), "and it is the imported Pokedex, not the newer one"); }

  // a factory reset, then the reboot it does
  p.factoryReset();
  { Pet q; q.begin(); same(q, "booting after a factory reset"); ck(q.registeredCount() <= 1, "which starts the Pokedex over"); }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
