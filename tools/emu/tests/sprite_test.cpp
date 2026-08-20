// Which sprite file actually gets opened.
//
// PmdMon::load() took a uint8_t while the dex reached 386, so every species
// from 256 up wrapped into Kanto: a MARSHTOMP (258) opened p002.bin and stood
// there as an IVYSAUR. Reported from a board, because nothing here looked.
//
// It is the third time this exact trap has been sprung -- DexEntry::evolvesTo
// and TrainerMon::dex were both uint8_t when the expansion landed -- so this
// checks EVERY species can be asked for and comes back as itself, rather than
// spot-checking the one that was reported.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "sdmon.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=7; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
uint32_t millis(){return 0;}
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
// sfxPlay comes from host_impl.cpp here -- this test links the host, not the sketch
void emuSetSpriteDir(const char *);
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  emuSetSpriteDir(SPRITE_DIR);

  // the exact creature that was reported, and the exact one it turned into
  {
    PmdMon a, b;
    bool okA = a.load(258, false);      // MARSHTOMP
    bool okB = b.load(2, false);        // IVYSAUR
    ck(okA, "a Hoenn sprite loads at all");
    ck(okA && a.dex == 258, "and it is MARSHTOMP, not whatever 258 truncates to");
    if (okA && okB) {
      bool same = a.palCount == b.palCount &&
                  !memcmp(a.pal, b.pal, a.palCount * sizeof(uint16_t));
      ck(!same, "MARSHTOMP's pixels are not IVYSAUR's");
    }
    a.unload(); b.unload();
  }

  // EVERY species: asked for by number, comes back as itself
  {
    int loaded = 0, wrong = 0, missing = 0;
    for (int16_t d = 1; d <= DEX_COUNT; d++) {
      PmdMon m;
      if (!m.load(d, false)) { missing++; continue; }
      loaded++;
      if (m.dex != d) { wrong++; if (wrong < 4) printf("      asked %d, got %d\n", d, m.dex); }
      m.unload();
    }
    printf("      %d sprites loaded, %d missing from the pack\n", loaded, missing);
    ck(loaded > 300, "most of the dex has a sprite to check");
    ck(wrong == 0, "every species that loads is the species that was asked for");
  }

  // THE assertion that actually bites. m.dex only records what was ASKED for,
  // so a truncation inside the filename alone would slip past it -- proven by
  // putting the bug back and watching that check still pass. This compares the
  // PIXELS of every species past 255 against the Kanto species it would wrap
  // onto, which is the thing that was really going wrong on the panel.
  {
    int checked = 0, collided = 0;
    for (int16_t d = 256; d <= DEX_COUNT; d++) {
      PmdMon hi, lo;
      if (!hi.load(d, false)) continue;
      if (!lo.load((int16_t)(d - 256), false)) { hi.unload(); continue; }
      checked++;
      if (hi.palCount == lo.palCount &&
          !memcmp(hi.pal, lo.pal, hi.palCount * sizeof(uint16_t))) {
        if (collided < 3) printf("      dex %d draws the same pixels as %d\n", d, d - 256);
        collided++;
      }
      hi.unload(); lo.unload();
    }
    printf("      compared %d species past 255 against their wrap-around twin\n", checked);
    ck(checked > 100, "the whole back half of the dex is reachable");
    ck(collided == 0, "and none of them draws the Kanto species it would truncate to");
  }

  // and nonsense is refused rather than opening some other creature's file
  {
    PmdMon m;
    ck(!m.load(0, false), "dex 0 loads nothing");
    ck(!m.load(-1, false), "nor a negative dex");
    ck(!m.load(9999, false), "nor one past the table");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
