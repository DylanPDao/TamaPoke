// A fresh board asks for its language before anything else.
//
// Checked from a clean NVS (run.sh deletes it before every test), so this is
// exactly what a new install and a WIPE look like. Each step first asserts the
// screen it thinks it is on: a routing test that never reaches the screen
// passes vacuously.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "i18n.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=11; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop(); void render();
void onTap(int16_t,int16_t); void onSwipe(int); void onSwipeV(int);
uint8_t uiCurrentScreen();
int langPickRowAt(int16_t x, int16_t y);
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern uint8_t sdAsk;
extern uint8_t rpickPage;
extern bool cardOpen, playerOpen, gymOpen, partyOpen, menuOpen;
#define LANGPICK_Y0 96
#define LANGPICK_PITCH 54

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static const char *scr(){ return SCREEN_NAME[uiCurrentScreen()]; }
static int rowCentre(int r){ return LANGPICK_Y0 + r * LANGPICK_PITCH + 24; }

int main(){
  setup();
  for (int i = 0; i < 3; i++) loop();
  ck(!langChosen(), "a clean NVS has no language chosen");
  ck(pet.awaitingStarter(), "and is a new game");
  ck(!strcmp(scr(), "lang"), "so the first screen is the language question");

  // ---- the native names: the picker is read by someone who may not read English
  for (int l = 0; l < LANG_COUNT; l++) {
    const char *n = langNativeName((Lang)l);
    bool ascii = n && *n;
    for (const char *c = n; ascii && *c; c++) ascii = (unsigned char)*c < 0x80;
    char w[64]; snprintf(w, sizeof(w), "language %d has a printable ASCII name", l);
    ck(ascii, w);
  }
  ck(!strcmp(langNativeName(LANG_DE), "DEUTSCH"), "each row is named in its own language");

  // ---- the rows tile: every pixel down the middle belongs to exactly one row
  int top = LANGPICK_Y0 - 3, bottom = LANGPICK_Y0 + LANG_COUNT * LANGPICK_PITCH - 3;
  bool tiled = true, ordered = true; int last = -1;
  for (int y = top; y < bottom; y++) {
    int r = langPickRowAt(233, y);
    if (r < 0) tiled = false;
    if (r < last) ordered = false;
    if (r >= 0) last = r;
  }
  ck(tiled, "no dead pixels between the rows");
  ck(ordered && last == LANG_COUNT - 1, "rows run top to bottom and the last is reachable");
  ck(langPickRowAt(233, top - 1) < 0 && langPickRowAt(233, bottom) < 0, "and nothing outside them is a row");

  // ---- modal: swipes and misses change nothing
  rpickPage = 0;
  onSwipe(1); onSwipe(-1); onSwipeV(1); onSwipeV(-1);
  ck(!strcmp(scr(), "lang") && rpickPage == 0, "a swipe neither leaves it nor pages the chooser behind it");
  // the screen would still READ "lang" with these set, since it routes first --
  // and the moment a language is picked, whatever they opened would be the screen
  ck(!cardOpen && !playerOpen && !gymOpen && !partyOpen && !menuOpen,
     "and opens nothing behind it to surface later");
  onTap(233, 30); onTap(20, rowCentre(2));
  ck(!strcmp(scr(), "lang") && !langChosen(), "a tap outside the rows does nothing");

  // ---- the SD question waits behind it, so it is asked in the chosen language
  sdAsk = 1;
  ck(!strcmp(scr(), "lang"), "a pending SD question does not jump ahead of it");
  render();

  // ---- choose DEUTSCH
  onTap(233, rowCentre(LANG_DE));
  ck(langChosen() && gLang == LANG_DE, "tapping DEUTSCH chooses German");
  ck(!strcmp(T(S_LANG_LABEL), "Sprache"), "and the strings follow");
  ck(!strcmp(scr(), "sdcard"), "the SD question comes next");
  sdAsk = 0;
  ck(!strcmp(scr(), "region"), "then the first-boot region chooser");

  // ---- persisted, by the KEY and not by the value
  gLang = LANG_EN; loadLang();
  ck(gLang == LANG_DE && langChosen(), "it survives a reboot");
  setLang(LANG_EN); gLang = LANG_ES; loadLang();
  ck(gLang == LANG_EN && langChosen(), "choosing the DEFAULT language counts as chosen too");

  // ---- WIPE asks again
  pet.factoryReset(); loadLang();
  ck(!langChosen(), "after WIPE the question comes back");
  ck(!strcmp(scr(), "lang"), "and it is the screen again");

  // ---- a player with a creature is never stopped to be asked
  pet.chooseStarter(4);
  ck(!pet.awaitingStarter() && !langChosen(), "an existing game with no language ever stored");
  ck(strcmp(scr(), "lang") != 0, "is NOT interrupted by the question");

  printf(bad ? "\n%d FAILED\n" : "\nall passed\n", bad);
  return bad ? 1 : 0;
}
