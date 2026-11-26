// A sprite must load whatever folder the repository is cloned into.
//
// PmdMon::load() built the file's path in a 64-character buffer. A clone at
// /Users/<name>/Development/workspace/TamaPoke already needs 73, so the path
// was silently cut, no file was found, and the emulator showed the "no
// sprites" screen with a full pack sitting on disk -- while a clone at a short
// path worked. This copies one sprite into a folder with a long name and asks
// for it there.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "sdmon.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
uint32_t g_seed=7; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
uint32_t millis(){return 0;}
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void emuSetSpriteDir(const char *);
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  // the control: the pack itself, so a failure below is about the path's length
  emuSetSpriteDir(SPRITE_DIR);
  { PmdMon m; bool ok = m.load(1, false); m.unload();
    if (!ok && strlen(SPRITE_DIR) < 40) { printf("FAIL  the pack has no p001.bin to copy\n"); return 1; } }

  char cwd[1024]; if (!getcwd(cwd, sizeof(cwd))) return 1;
  std::string dir = std::string(cwd) + "/a-folder-whose-name-is-long-enough-to-pass-any-sixty-four-character-buffer/and-then-one-more-level-for-good-measure";
  std::string cmd = "mkdir -p '" + dir + "' && cp '" + std::string(SPRITE_DIR) + "/p001.bin' '" + dir + "/p001.bin'";
  ck(system(cmd.c_str()) == 0, "one sprite copied into a folder with a long path");
  ck(dir.size() > 150, "and that path is longer than any small fixed buffer");

  emuSetSpriteDir(dir.c_str());
  PmdMon m;
  bool ok = m.load(1, false);
  ck(ok, "the sprite loads from the long path");
  ck(ok && m.dex == 1, "and it is the species that was asked for");
  m.unload();
  if (bad) { printf("FAILURES\n"); return 1; }
  return 0;
}
