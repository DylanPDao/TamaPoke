// What a file arriving over PUT makes the running firmware reload.
//
// The web installer streams a region pack (~300 files) into the firmware that
// is already running. Every file used to mark the pet's sprite dirty AND the
// region art dirty, so after EACH one ensureMon() re-streamed the creature's
// ~135 KB sprite and sdScanRegionArt() reopened ~20 files -- in the middle of
// the transfer. Now only the creature's own sprite (or thumbs.bin) reloads it,
// and the rescan waits until the PUTs have gone quiet.
//
// What must NOT regress (CLAUDE.md, trap 1): a pack that arrives later still
// ungreys its region WITHOUT a reboot. The emulator has no card, so its
// sdScanRegionArt() is a counter; g_emuCardArt plays the packs on the card.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "sdmon.h"
#include <cstdio>
#include <chrono>
#include <thread>
uint32_t g_seed=8; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop();
extern Pet pet;
extern int g_emuArtScans;
extern uint16_t g_emuCardArt;
bool sdPathIsMonOf(const char *path, int16_t dex);

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static void pumpMs(int ms){
  auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    loop();
  }
}
// what the PUT handler does with a file that arrived whole, then a loop pass
static bool landed(const char *path){
  sdDirty = false;
  sdPutLanded(path);
  return sdDirty;
}

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(4, false);        // CHARMANDER, dex 4
  pumpMs(100);

  // --- the pet's sprite: only its own file reloads it -------------------------
  ck(landed("/mons/p004.bin"),  "its PMD sprite reloads it");
  ck(landed("/mons/ps004.bin"), "its shiny PMD sprite reloads it");
  ck(landed("/mons/004.bin"),   "its old-format sprite reloads it");
  ck(landed("/mons/s004.bin"),  "its old-format shiny sprite reloads it");
  ck(landed("/mons/thumbs.bin"), "thumbs.bin reloads it");
  ck(!landed("/mons/p152.bin"), "another species' sprite does NOT");
  ck(!landed("/mons/p040.bin") && !landed("/mons/p041.bin") && !landed("/mons/p400.bin"),
     "nor does a dex that merely shares digits with it");
  ck(!landed("/mons/p004.bin.tmp") && !landed("/mons/notes.txt"), "nor a file that is not a sprite");
  ck(!sdPathIsMonOf("/mons/p004.bin", -1), "an egg has no sprite to reload");
  // and a pack's worth of other species never touches it
  int reloads = 0;
  for (int d = 152; d <= 251; d++) {
    char p[24]; snprintf(p, sizeof(p), "/mons/p%03d.bin", d);
    if (landed(p)) reloads++;
  }
  ck(reloads == 0, "a hundred files of another region reload the pet's sprite zero times");
  sdDirty = false;
  sdArtDirty = false;
  pumpMs(50);

  // --- the region rescan: once, after the transfer goes quiet -----------------
  // a card that had only Kanto, onto which the Johto pack is being written
  gRegionArt = (uint16_t)(1u << 0);
  g_emuCardArt = (uint16_t)((1u << 0) | (1u << 1));
  ck(!regionAvailable(1), "precondition: Johto is greyed out (no pack yet)");
  int scans0 = g_emuArtScans;
  for (int i = 0; i < 15; i++) {                 // a burst of files, 100 ms apart
    char p[24]; snprintf(p, sizeof(p), "/mons/p%03d.bin", 152 + i);
    sdPutLanded(p);
    pumpMs(100);
  }
  printf("     rescans during a 1.5 s burst of 15 files: %d\n", g_emuArtScans - scans0);
  ck(g_emuArtScans == scans0, "no rescan while files are still arriving");
  pumpMs(2300);
  ck(g_emuArtScans == scans0 + 1, "exactly one rescan once they stop");
  ck(regionAvailable(1), "and Johto ungreys WITHOUT a reboot");
  pumpMs(300);
  ck(g_emuArtScans == scans0 + 1, "and it does not rescan again by itself");

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
