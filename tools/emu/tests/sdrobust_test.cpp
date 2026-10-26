// Damaged files on the card must not read out of bounds or hang the panel.
//
//  * thumbs.bin arrives over PUT, which writes in place: an upload cut short
//    keeps a valid header whose later offsets point past the end of what
//    arrived, and SdThumbs::get() handed the gallery a pointer off the end of
//    the buffer. A file shorter than its own magic was memcmp'd anyway.
//  * pmdFrameAt() walks the frame durations until the elapsed time runs out;
//    with every duration 0 it never runs out, and the loop task has no
//    watchdog, so the panel froze for good.
//
// The thumbs bounds and the duration check live in sdmon.h and are shared by
// the firmware's sdmon.cpp and the emulator's loader, so what is tested here is
// the code the board runs. pmdFrameAt() is the sketch's own.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "sdmon.h"
#include <cstdio>
#include <cstring>
#include <csignal>
#include <string>
#include <vector>
#include <unistd.h>
uint32_t g_seed=9; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
uint8_t pmdFrameAt(const PmdAct &a, uint32_t t, bool loop);
void emuSetSpriteDir(const char *);

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static std::string g_dir;
static void put(const char *name, const std::vector<uint8_t> &b){
  FILE *f = fopen((g_dir + "/" + name).c_str(), "wb");
  if (!b.empty()) fwrite(b.data(), 1, b.size(), f);
  fclose(f);
}
static void u16(std::vector<uint8_t> &v, uint16_t x){ v.push_back(x & 0xFF); v.push_back(x >> 8); }
static void u32(std::vector<uint8_t> &v, uint32_t x){ for (int i = 0; i < 4; i++) v.push_back((x >> (8*i)) & 0xFF); }

// A well-formed thumbs.bin with `n` species, each a 4x3 image on a 2-colour palette.
static std::vector<uint8_t> thumbsFile(int n){
  std::vector<uint8_t> v = {'T','P','T','H'};
  u16(v, (uint16_t)n);
  uint32_t blob = 3 + 2*2 + 4*3;
  for (int i = 0; i < n; i++) u32(v, 6 + 4*n + i*blob);
  for (int i = 0; i < n; i++) {
    v.push_back(4); v.push_back(3); v.push_back(2);
    u16(v, 0x1234); u16(v, 0x5678);
    for (int k = 0; k < 12; k++) v.push_back(k & 1);
  }
  return v;
}

static void hang(int){ printf("FAIL  pmdFrameAt() never returned -- the panel would freeze\n"); fflush(stdout); _exit(1); }

int main(){
  char tmpl[] = "/tmp/sdrobustXXXXXX";
  g_dir = mkdtemp(tmpl);
  emuSetSpriteDir(g_dir.c_str());
  sdReady = true;

  // ---- thumbs.bin
  std::vector<uint8_t> good = thumbsFile(3);
  put("thumbs.bin", good);
  ck(thumbs.load() && thumbs.count == 3, "a whole thumbs.bin loads");
  ck(thumbs.get(1) && thumbs.get(2) && thumbs.get(3), "and every species in it is served");
  ck(thumbs.get(3) && thumbs.get(3)[0] == 4 && thumbs.get(3)[2] == 2, "pointing at the right blob");
  ck(!thumbs.get(0) && !thumbs.get(4), "and nothing outside 1..count");

  // the upload died inside species 2's pixels
  std::vector<uint8_t> cut(good.begin(), good.begin() + (6 + 4*3) + 19 + 10);
  put("thumbs.bin", cut);
  ck(thumbs.load(), "a cut-short file still loads (its header is intact)");
  ck(thumbs.get(1) != nullptr, "the species that arrived whole are still drawn");
  ck(!thumbs.get(2), "the one cut in half is refused, not read past the end");
  ck(!thumbs.get(3), "and the one whose offset points beyond the file is refused");

  // an offset that wraps a 32-bit add
  std::vector<uint8_t> wrap = good;
  wrap[6 + 4*1 + 0] = wrap[6 + 4*1 + 1] = wrap[6 + 4*1 + 2] = wrap[6 + 4*1 + 3] = 0xFF;
  put("thumbs.bin", wrap);
  ck(thumbs.load() && !thumbs.get(2) && thumbs.get(3), "an offset of 0xFFFFFFFF is refused on its own");

  // the offset table itself cut off: claims 200 species, carries 2 bytes of table
  std::vector<uint8_t> table = {'T','P','T','H'}; u16(table, 200); table.push_back(0); table.push_back(0);
  put("thumbs.bin", table);
  ck(!thumbs.load() && !thumbs.get(1), "a header whose offset table is cut is refused whole");

  put("thumbs.bin", std::vector<uint8_t>{'T','P','T'});
  ck(!thumbs.load(), "a file shorter than its magic is refused");
  put("thumbs.bin", std::vector<uint8_t>{});
  ck(!thumbs.load() && !thumbs.loaded, "an empty file is refused");

  // ---- PMD frame durations
  signal(SIGALRM, hang);
  PmdAct a; a.frames = 3; a.w = a.h = 1;
  a.ms[0] = a.ms[1] = a.ms[2] = 100;
  ck(pmdFrameAt(a, 150, true) == 1 && pmdFrameAt(a, 250, true) == 2 && pmdFrameAt(a, 350, true) == 0,
     "normal durations walk the frames and loop");
  ck(pmdFrameAt(a, 999, false) == 2, "and a one-shot holds its last frame");
  a.ms[0] = 0; a.ms[2] = 0;
  ck(pmdFrameAt(a, 50, true) == 1, "a zero-length frame among timed ones is simply skipped");
  a.ms[1] = 0;
  alarm(3);
  uint8_t f = pmdFrameAt(a, 50, true);
  uint8_t g = pmdFrameAt(a, 12345, true);
  alarm(0);
  ck(f == 0 && g == 0, "every duration 0: it returns (frame 0) instead of spinning");

  // the loader refuses such an action outright
  auto tpk2 = [](uint16_t ms){
    std::vector<uint8_t> v = {'T','P','K','2', 1};
    u16(v, 1); u16(v, 0x1234);                     // palette of one
    v.push_back(PMD_IDLE); v.push_back(2); v.push_back(2); v.push_back(2);   // id w h frames
    u16(v, ms); u16(v, ms);
    for (int k = 0; k < 8; k++) v.push_back(0);
    return v;
  };
  PmdMon m;
  put("p001.bin", tpk2(120));
  ck(m.load(1, false) && m.has(PMD_IDLE), "a sprite with timed frames loads");
  put("p001.bin", tpk2(0));
  ck(!m.load(1, false) && !m.loaded, "a sprite whose action has no duration at all is refused");

  unlink((g_dir + "/thumbs.bin").c_str());
  unlink((g_dir + "/p001.bin").c_str());
  rmdir(g_dir.c_str());
  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
