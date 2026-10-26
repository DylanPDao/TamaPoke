// Species names follow the language: German and French have their own,
// generated into names.h by tools/gen_names.py; the others use English.
//
// Two halves, because either alone passes with the feature half-built:
//   * speciesName() answers right, and a Combatant -- what battles narrate --
//     carries the localized name;
//   * NOTHING on the panel bypasses it. A display site still reading
//     DEX_TBL[x].name would stay English in silence -- CLAUDE.md trap #1, a
//     rule enforced in one path and not in its twin -- so the sources are
//     scanned: DEX_TBL[..].name may appear only on serial console lines, which
//     keep English on purpose.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "dex.h"
#include "i18n.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <regex>
#include <string>

uint32_t g_seed = 12345;
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

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

// The repository root, from this file's own path (tools/emu/tests/x.cpp).
static std::string root() {
  std::string f = __FILE__;
  for (int i = 0; i < 4; i++) f = f.substr(0, f.find_last_of('/'));
  return f;
}

// Lines that read a species name straight off the table, outside the console.
static int bypasses(const char *file) {
  std::ifstream in(root() + "/" + file);
  if (!in) { printf("FAIL  cannot read %s\n", file); return 1000; }
  // DEX_TBL[..].name, or the .name of the DexEntry references the sketch
  // binds as `d` / `de`
  std::regex direct(R"(DEX_TBL\[[^\]]+\]\.name|\b(d|de)\.name\b)");
  std::string line;
  int n = 0, at = 0;
  while (std::getline(in, line)) {
    at++;
    if (!std::regex_search(line, direct)) continue;
    if (line.find("Serial") != std::string::npos) continue;   // console: English on purpose
    printf("      %s:%d bypasses speciesName(): %s\n", file, at, line.c_str());
    n++;
  }
  return n;
}

int main() {
  // ---- what speciesName() answers
  setLang(LANG_EN);
  ck(!strcmp(speciesName(4), "CHARMANDER"), "English: CHARMANDER");
  setLang(LANG_DE);
  ck(!strcmp(speciesName(4), "GLUMANDA"), "German: GLUMANDA");
  ck(!strcmp(speciesName(25), "PIKACHU"), "a name German shares with English falls back to it");
  ck(!strcmp(speciesName(29), DEX_TBL[29].name), "NIDORAN keeps the table's spelling (no glyph for the sign)");
  char want[16];
  glcdEncode("M\xC3\x84HIKEL", want, sizeof(want));     // MAEHIKEL with a real A-umlaut
  ck(!strcmp(speciesName(672), want) && strlen(speciesName(672)) == 7,
     "an umlaut name reaches the panel as the font's own glyph");
  setLang(LANG_FR);
  ck(!strcmp(speciesName(4), "SALAMECHE"), "French: SALAMECHE, unaccented like the French UI");
  for (Lang l : { LANG_ES, LANG_IT, LANG_PT }) {
    setLang(l);
    char w[48]; snprintf(w, sizeof(w), "language %d uses the English names", (int)l);
    ck(!strcmp(speciesName(4), "CHARMANDER"), w);
  }
  setLang(LANG_DE);
  ck(!strcmp(speciesName(-5), DEX_TBL[0].name) && !strcmp(speciesName(DEX_COUNT + 9), DEX_TBL[0].name),
     "an out-of-range dex cannot index past the tables");

  // ---- every name the panel could be handed
  for (Lang l : { LANG_DE, LANG_FR }) {
    setLang(l);
    int tooLong = 0, odd = 0, local = 0;
    for (int d = 1; d <= DEX_COUNT; d++) {
      const char *s = speciesName(d);
      if (!s || !*s || strlen(s) > 12) { tooLong++; continue; }
      if (strcmp(s, DEX_TBL[d].name)) local++;
      for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x80) continue;
        // French is plain ASCII; German may carry only the capital umlauts
        bool ok = (l == LANG_DE) && (*p == 0x8E || *p == 0x99 || *p == 0x9A);
        if (!ok) { odd++; break; }
      }
    }
    char w[96];
    snprintf(w, sizeof(w), "%s: %d names of its own, all present, <= 12 glyphs, drawable",
             l == LANG_DE ? "DE" : "FR", local);
    ck(tooLong == 0 && odd == 0 && local > 600, w);
  }

  // ---- a battle narrates with the localized name
  setLang(LANG_DE);
  Pet p;
  p.dbgHatchAs(4, false);
  Combatant c;
  combatantFromPet(c, p);
  ck(!strcmp(c.name, "GLUMANDA"), "a Combatant built from the live pet is GLUMANDA");
  PartyMon m{};
  m.dex = 6; m.level = 40;
  combatantFromParty(c, m);
  ck(!strcmp(c.name, "GLURAK"), "and one from the party is GLURAK");
  strcpy(p.nick, "FUNKE");
  combatantFromPet(c, p);
  ck(!strcmp(c.name, "FUNKE"), "a nickname still wins over the species name");

  // ---- nothing on the panel bypasses speciesName()
  int n = bypasses("TamaPoke.ino") + bypasses("battle.cpp") + bypasses("pet.cpp") +
          bypasses("party.cpp") + bypasses("link.cpp");
  ck(n == 0, "no display code reads DEX_TBL[].name directly (console lines aside)");

  printf(bad ? "\n%d FAILED\n" : "\nall passed\n", bad);
  return bad ? 1 : 0;
}
