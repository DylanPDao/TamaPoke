// Host versions of the three hardware modules. The sprite loader is the real
// TPK2/TPTH parser, just reading from a directory instead of the SD card.
#include "Arduino.h"
#include "sdmon.h"
#include "rtcbat.h"
#include "audio.h"
#include "linknow.h"
#include <cstdio>
#include <string>

// Baked in by build.sh from the repo location; --sprites <dir> overrides it.
#ifndef SPRITE_DIR
#define SPRITE_DIR "../sdcard/mons"
#endif
static std::string g_spriteDir = SPRITE_DIR;
void emuSetSpriteDir(const char *d) { g_spriteDir = d; }

bool sdReady = true;
bool sdNeedsFormat = false;
bool sdDirty = false;

// Which card is "in the slot". A test picks it before setup(), so the boot path
// the firmware takes on a board -- no card, an unreadable card, a format that
// works or fails -- can be driven here without one.
//   0 = a readable card (the default, and what every other test gets)
//   1 = no card at all
//   2 = a card that answers but whose filesystem will not mount
static int g_emuSdCard = 0;
static bool g_emuFormatOk = true;
static int g_emuFormatCalls = 0;
static void (*g_emuFormatHook)() = nullptr;   // runs at the moment sdFormat() blocks
void emuSetSdCard(int mode) { g_emuSdCard = mode; }
void emuSetSdFormatResult(bool ok) { g_emuFormatOk = ok; }
int emuSdFormatCalls() { return g_emuFormatCalls; }
void emuSetSdFormatHook(void (*f)()) { g_emuFormatHook = f; }
// The firmware rescans region art when a pack lands over PUT. There is no card
// here and no PUT path, so the flag never sets and the scan is a no-op -- but both
// symbols are referenced from loop(), so they have to exist to link.
bool sdArtDirty = false;
// Counted, so a test can see WHEN loop() rescans. g_emuCardArt stands in for
// the packs on a card: 0 (the default) keeps the scan a no-op, as with no card;
// a test that sets it gets gRegionArt narrowed to it, as the real scan would.
extern uint16_t gRegionArt;   // pet.cpp
int g_emuArtScans = 0;
uint16_t g_emuCardArt = 0;
void sdScanRegionArt(bool) {
  g_emuArtScans++;
  if (g_emuCardArt) gRegionArt = g_emuCardArt;
}
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

bool PmdMon::load(int16_t dexNum, bool shiny) {
  if (dexNum < 1 || dexNum > 999) return false;
  unload();
  if (!sdReady) return false;    // as on the board: no mounted card, no sprite
  char p[64];
  snprintf(p, sizeof(p), "%s/p%s%03u.bin", g_spriteDir.c_str(), shiny ? "s" : "", (unsigned)dexNum);
  uint32_t size = 0;
  blob = slurp(p, &size);
  if (!blob) {
    snprintf(p, sizeof(p), "%s/p%03u.bin", g_spriteDir.c_str(), (unsigned)dexNum);
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
    if (!pmdActTimed(a)) { unload(); return false; }   // the firmware refuses it too
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
  dex = dexNum;
  loaded = true;
  return true;
}

void PmdMon::unload() {
  if (blob) { free(blob); blob = nullptr; }
  for (auto &a : acts) { a.w = a.h = a.frames = a.base = 0; a.data = nullptr; }
  loaded = false;
}

// The header and offset checks are sdmon.h's, shared with the firmware, so a
// test of a cut-short thumbs.bin here is a test of what the board does.
bool SdThumbs::load() {
  if (data) { free(data); data = nullptr; }
  loaded = false;
  count = 0;
  size = 0;
  if (!sdReady) return false;
  data = slurp(g_spriteDir + "/thumbs.bin", &size);
  if (!data) { Serial.println("emu: no thumbs.bin"); size = 0; return false; }
  if (!sdThumbsHeader(data, size, &count)) {
    free(data); data = nullptr; size = 0; count = 0;
    return false;
  }
  loaded = true;
  Serial.printf("emu: thumbnails loaded: %u\n", count);
  return true;
}

const uint8_t *SdThumbs::get(int16_t dex) const {
  if (!loaded) return nullptr;
  return sdThumbAt(data, size, count, dex);
}

bool SdMon::load(int16_t, bool) { return false; }   // legacy TPK1 path unused
void SdMon::unload() { if (data) { free(data); data = nullptr; } loaded = false; }
bool sdBegin() {
  sdReady = (g_emuSdCard == 0);
  sdNeedsFormat = (g_emuSdCard == 2);
  if (sdReady) Serial.printf("emu: sprites from %s\n", g_spriteDir.c_str());
  else Serial.printf("emu: %s\n", sdNeedsFormat ? "unreadable SD card" : "no SD card");
  return sdReady;
}
bool sdFormat() {
  g_emuFormatCalls++;
  if (g_emuFormatHook) g_emuFormatHook();
  if (!g_emuFormatOk) return false;
  g_emuSdCard = 0;
  sdReady = true;
  sdNeedsFormat = false;
  sdScanRegionArt();
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
uint32_t pmuReadErrors() { return 0; }

// --- audio (silent) ---
void audioBegin() {}
void sfxPlay(uint8_t) {}
// no radio here; the protocol itself is exercised by tests/link_test.cpp
struct Link;
// No radio here at all, which is why lossy_test drives Link directly instead
// of going through this.
bool linkNowBegin(Link *) { return false; }
void linkNowEnd() {}
bool linkNowUp() { return false; }
void linkNowPoll() {}
static LinkNowStats gNoStats;
const LinkNowStats &linkNowStats() { return gNoStats; }
// audio is silent here, but the sketch calls these, so they have to exist
static uint8_t g_emuVol = 7, g_emuMusic = 0;
void audioMusic(uint8_t id) { g_emuMusic = id; }
void audioSetVolume(uint8_t v) { g_emuVol = v > 10 ? 10 : v; }
uint8_t audioVolume() { return g_emuVol; }
void audioSetEnabled(bool on) { (void)on; }
bool audioEnabled() { return true; }

// Why the last run ended. The tests link this file but not main_sdl.cpp, so the
// state lives here and the GUI sets it after reading a simulated crash.
static int g_resetReason = ESP_RST_POWERON;
esp_reset_reason_t emuResetReason() { return g_resetReason; }
void emuSetResetReason(int r) { g_resetReason = r; }
