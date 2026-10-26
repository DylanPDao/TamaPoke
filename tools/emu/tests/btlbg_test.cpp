// The battle backdrop is scaled once into a PSRAM buffer and copied in each
// frame, instead of drawBack() re-expanding every run of every row ten times a
// second. It has to be the SAME picture, for every scene, day and night.
//
// The reference below is drawBack() as it was -- runs of 2x fillRect through
// the canvas, which clips at the edge -- and the cache is compared with it pixel
// for pixel for all BACK_BIOMES x 2 scenes, each reached the real way (a fight
// against a creature of that biome, at that hour). Then: the buffer is freed
// when the fight ends, as the two battle sprites are.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "backs.h"
#include <cstdio>
#include <cstring>
#include <vector>
uint32_t g_seed=12; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render();
void startBattle(int16_t dex, uint8_t lvl);
void battleTap(int16_t, int16_t);
extern Pet pet;
extern Arduino_Canvas *gfx;
extern bool battleOpen;
extern uint8_t btlMenu;
extern uint16_t *btlBgBuf;
extern int16_t btlBgKey;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// drawBack() before the cache, verbatim apart from the name
static void refDrawBack(const BackScene &b, int y0) {
  const int SC = 2;
  int x0 = 233 - (b.w * SC) / 2;
  for (int r = 0; r < b.h; r++) {
    const uint8_t *row = b.idx + (uint32_t)r * b.w;
    int c = 0;
    while (c < b.w) {
      uint8_t v = row[c];
      int run = 1;
      while (c + run < b.w && row[c + run] == v) run++;
      gfx->fillRect(x0 + c * SC, y0 + r * SC, run * SC, SC, b.pal[v]);
      c += run;
    }
  }
}

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.ageMinutes = 50UL * MINUTES_PER_LEVEL;

  const uint32_t MIDNIGHT = 1767225600UL;       // 00:00, a whole number of days
  int scenes = 0, matched = 0;
  for (int bi = 0; bi < BACK_BIOMES; bi++) {
    int16_t dex = 0;
    for (int d = 1; d <= DEX_COUNT && !dex; d++) if (DEX_TBL[d].biome == bi) dex = d;
    if (!dex) { printf("     (no species lives in biome %d)\n", bi); continue; }
    for (int night = 0; night < 2; night++) {
      pet.lastSeenEpoch = MIDNIGHT + (night ? 2 : 13) * 3600UL;
      startBattle(dex, 50);
      render();
      scenes++;
      if (!btlBgBuf || btlBgKey != bi * 2 + night) {
        printf("FAIL  biome %d %s: the cache holds key %d\n", bi, night ? "night" : "day", btlBgKey);
        bad++; continue;
      }
      std::vector<uint16_t> frame(gfx->buffer(), gfx->buffer() + 466 * 466);
      // the reference, drawn through the canvas the old way
      gfx->fillScreen(0);
      refDrawBack(BACKS[bi][night], 30);
      const uint16_t *ref = gfx->buffer();
      int diff = 0;
      for (int y = 0; y < 224; y++)
        for (int x = 0; x < 466; x++)
          if (btlBgBuf[y * 466 + x] != ref[(30 + y) * 466 + x]) diff++;
      // and the rendered frame really is built from it: the band is the scene,
      // wherever nothing is drawn over it (spot-check a column at the far left)
      int frameDiff = 0;
      for (int y = 30; y < 254; y++) if (frame[y * 466 + 0] != ref[y * 466 + 0]) frameDiff++;
      int topBlack = 0;
      for (int y = 0; y < 30; y++) if (frame[y * 466 + 0] != 0) topBlack++;
      if (diff || frameDiff || topBlack) {
        printf("FAIL  biome %d %s: %d px differ in the cache, %d in the frame's edge column, %d non-black above\n",
               bi, night ? "night" : "day", diff, frameDiff, topBlack);
        bad++;
      } else matched++;
    }
  }
  printf("     %d of %d scenes pixel-identical to drawBack()\n", matched, scenes);
  ck(scenes == BACK_BIOMES * 2, "every scene, day and night, was reached through a real fight");
  ck(matched == scenes, "and every one is pixel-identical to the old drawing");

  // the buffer goes when the fight does
  ck(btlBgBuf != nullptr, "during a fight the backdrop is held");
  btlMenu = 0;
  battleTap(69 + 168 + 60, 274 + 52 + 20);   // RUN: cell 3, the right of the lower row
  ck(!battleOpen, "running ends the fight");
  ck(btlBgBuf == nullptr && btlBgKey == -1, "and frees the backdrop with the sprites");

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
