// Host versions of the three hardware modules. The sprite loader is the real
// TPK2/TPTH parser, just reading from a directory instead of the SD card.
#include "Arduino.h"
#include "sdmon.h"
#include "rtcbat.h"
#include "audio.h"
#include <cstdio>
#include <string>

// Baked in by build.sh from the repo location; --sprites <dir> overrides it.
#ifndef SPRITE_DIR
#define SPRITE_DIR "../sdcard/mons"
#endif
static std::string g_spriteDir = SPRITE_DIR;
void emuSetSpriteDir(const char *d) { g_spriteDir = d; }

bool sdReady = true;
bool sdDirty = false;
SdThumbs thumbs;

static uint8_t *slurp(const std::string &path, uint32_t *size) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) return nullptr;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n <= 0) { fclose(f); return nullptr; }
  uint8_t *b = (uint8_t *)malloc(n);
  if (!b || fread(b, 1, n, f) != (size_t)n) { free(b); fclose(f); return nullptr; }
  fclose(f);
  *size = (uint32_t)n;
  return b;
}

bool PmdMon::load(uint8_t dexNum, bool shiny) {
  unload();
  char p[64];
  snprintf(p, sizeof(p), "%s/p%s%03u.bin", g_spriteDir.c_str(), shiny ? "s" : "", dexNum);
  uint32_t size = 0;
  blob = slurp(p, &size);
  if (!blob) {
    snprintf(p, sizeof(p), "%s/p%03u.bin", g_spriteDir.c_str(), dexNum);
    blob = slurp(p, &size);
  }
  if (!blob) return false;
  if (size < 7 || memcmp(blob, "TPK2", 4) != 0) { unload(); return false; }

  uint8_t nActs = blob[4];
  memcpy(&palCount, blob + 5, 2);
  if (palCount > 256 || (uint32_t)7 + palCount * 2 > size) { unload(); return false; }
  memcpy(pal, blob + 7, palCount * 2);

  const uint8_t *q = blob + 7 + palCount * 2, *end = blob + size;
  for (uint8_t i = 0; i < nActs && q + 4 <= end; i++) {
    uint8_t id = q[0], w = q[1], h = q[2], nf = q[3];
    q += 4;
    if (id >= PMD_NACTS || nf > 24) { unload(); return false; }
    uint32_t bytes = (uint32_t)nf * 2 + (uint32_t)w * h * nf;
    if (w == 0 || h == 0 || nf == 0 || q + bytes > end) { unload(); return false; }
    PmdAct &a = acts[id];
    a.w = w; a.h = h; a.frames = nf;
    for (uint8_t k = 0; k < nf; k++) { a.ms[k] = q[0] | (q[1] << 8); q += 2; }
    a.data = q;
    q += (uint32_t)w * h * nf;
    uint8_t base = 1;
    for (uint8_t f = 0; f < nf; f++) {
      const uint8_t *fr = a.data + (uint32_t)f * w * h;
      for (int r = h - 1; r >= 0; r--) {
        bool any = false;
        for (int c = 0; c < w && !any; c++) if (fr[r * w + c] != 0xFF) any = true;
        if (any) { if (r + 1 > base) base = r + 1; break; }
      }
    }
    a.base = base;
  }
  loaded = true;
  return true;
}

void PmdMon::unload() {
  if (blob) { free(blob); blob = nullptr; }
  for (auto &a : acts) { a.w = a.h = a.frames = a.base = 0; a.data = nullptr; }
  loaded = false;
}

bool SdThumbs::load() {
  uint32_t size = 0;
  data = slurp(g_spriteDir + "/thumbs.bin", &size);
  if (!data) { Serial.println("emu: no thumbs.bin"); return false; }
  if (memcmp(data, "TPTH", 4) != 0) { free(data); data = nullptr; return false; }
  memcpy(&count, data + 4, 2);
  loaded = true;
  Serial.printf("emu: thumbnails loaded: %u\n", count);
  return true;
}

const uint8_t *SdThumbs::get(int16_t dex) const {
  if (!loaded || dex < 1 || dex > count) return nullptr;
  uint32_t off;
  memcpy(&off, data + 6 + 4 * (dex - 1), 4);
  return data + off;
}

bool SdMon::load(uint8_t, bool) { return false; }   // legacy TPK1 path unused
void SdMon::unload() { if (data) { free(data); data = nullptr; } loaded = false; }
bool sdBegin() {
  Serial.printf("emu: sprites from %s\n", g_spriteDir.c_str());
  return true;
}
bool sdSerialCommand(const String &) { return false; }

// --- RTC / battery / PMU ---
static uint32_t g_epoch = 0;
bool rtcBegin() { return true; }
uint32_t rtcEpoch() { return g_epoch + millis() / 1000; }
void rtcSetEpoch(uint32_t e) { g_epoch = e - millis() / 1000; }
bool batBegin() { return true; }
void pmuEnablePanel() {}
int batPercent() { return 87; }
bool batCharging() { return false; }
bool usbPresent() { return true; }
void pwrSetup() {}
bool pwrShortPressed() { return false; }

// --- audio (silent) ---
void audioBegin() {}
void sfxPlay(uint8_t) {}
// no radio here; the protocol itself is exercised by tests/link_test.cpp
struct Link;
bool linkNowBegin(Link *) { return false; }
void linkNowEnd() {}
bool linkNowUp() { return false; }
// audio is silent here, but the sketch calls these, so they have to exist
static uint8_t g_emuVol = 7, g_emuMusic = 0;
void audioMusic(uint8_t id) { g_emuMusic = id; }
void audioSetVolume(uint8_t v) { g_emuVol = v > 10 ? 10 : v; }
uint8_t audioVolume() { return g_emuVol; }
void audioSetEnabled(bool on) { (void)on; }
bool audioEnabled() { return true; }
