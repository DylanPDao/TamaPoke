// Checks the positional STRINGS table: a language row with too few entries is
// zero-padded by the compiler with no diagnostic, which silently shifts every
// string after the gap. Also enforces what the bitmap font can draw: ASCII
// everywhere, plus German's umlauts and ss, which i18n.cpp maps onto the font's
// own code-page-437 glyphs -- and checks the panel really gets those glyphs.
#include "Arduino.h"
#include "Preferences.h"
// linked against the same core as every other suite, so it needs the same
// hardware stubs even though it only exercises the string table
uint32_t g_seed = 1;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}
#include "i18n.h"
#include <cstdio>
#include <cstring>

// STRINGS is file-static, so go through the same accessor the firmware uses.
static const char *LANG[] = { "ES", "EN", "FR", "DE", "IT", "PT" };

int main() {
  int bad = 0;
  for (int l = 0; l < LANG_COUNT; l++)
    for (int s = 0; s < STR_COUNT; s++) {
      setLang((Lang)l);
      const char *v = T((StrId)s);
      if (!v) { printf("NULL  %s index %d\n", LANG[l], s); bad++; continue; }
    }
  // ---- what may be written, and what reaches the panel
  int umlauts = 0;
  for (int l = 0; l < LANG_COUNT; l++)
    for (int s = 0; s < STR_COUNT; s++) {
      const char *src = i18nSource((Lang)l, (StrId)s);
      char enc[256];
      int lost = glcdEncode(src, enc, sizeof(enc));
      bool high = false;
      for (const unsigned char *p = (const unsigned char *)src; *p; p++) high |= *p > 0x7F;
      if (lost) { printf("NO GLYPH  %s index %d: \"%s\"\n", LANG[l], s, src); bad++; }
      // German only, for now: the other rows stay unaccented on purpose
      if (high && l != LANG_DE) { printf("NON-ASCII outside German  %s index %d: \"%s\"\n", LANG[l], s, src); bad++; }
      setLang((Lang)l);
      if (strcmp(T((StrId)s), enc)) { printf("NOT ENCODED  %s index %d: \"%s\"\n", LANG[l], s, src); bad++; }
      if (high && l == LANG_DE) umlauts++;
    }
  // the mechanism actually engaged: German has umlauts, and one of them comes
  // out as the font's single-byte glyph with the length the centring expects
  setLang(LANG_DE);
  bool sawGlyph = false;
  for (int s = 0; s < STR_COUNT && !sawGlyph; s++) {
    const char *src = i18nSource(LANG_DE, (StrId)s);
    if (!strstr(src, "\xC3\x9C")) continue;           // a capital U-umlaut
    const char *t = T((StrId)s);
    sawGlyph = strchr(t, (char)0x9A) && strlen(t) == strlen(src) - 1;
  }
  if (umlauts < 10 || !sawGlyph) { printf("FAIL  German umlauts not reaching the panel (%d strings)\n", umlauts); bad++; }
  else printf("PASS  %d German strings carry umlauts, drawn as font glyphs\n", umlauts);
  printf("%s: %d languages x %d strings\n", bad ? "FAIL" : "PASS", LANG_COUNT, STR_COUNT);
  return bad ? 1 : 0;
}
