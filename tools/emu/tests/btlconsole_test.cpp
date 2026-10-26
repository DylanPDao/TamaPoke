// The console's BATTLE <dex> [lvl] opens the fight ON TOP, from any screen.
//
// startBattle() only set battleOpen. onTap() hands taps to the battle first,
// but render() draws the gallery, the party, the minigames, the training menu,
// the keyboard and the clock BEFORE it -- so the fight ran invisibly behind
// whatever was open, taking taps nobody could see the target of.
//
// Driven through handleSerial() exactly as a typed command arrives.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
uint32_t g_seed=31; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}

static std::vector<std::string> gIn;
static size_t gAt = 0;
int FakeSerial::available(){ return gAt < gIn.size() ? 1 : 0; }
String FakeSerial::readStringUntil(char){
  if (gAt >= gIn.size()) return String("");
  return String(gIn[gAt++].c_str());
}

void setup(); void loop(); void render(); void handleSerial();
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern bool battleOpen, galleryOpen, partyOpen, gameOpen, trainOpen, kbOpen,
            clockOpen, cardOpen, playerOpen, menuOpen, movePickOpen, partyPick;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static void type(const char *line) {
  gIn.clear(); gIn.push_back(line); gAt = 0;
  while (gAt < gIn.size()) handleSerial();
}
static const char *screen() { return SCREEN_NAME[uiCurrentScreen()]; }

int main(){
  setup();
  for (int i = 0; i < 3; i++) loop();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  pet.dbgHatchAs(6, false);
  pet.ageMinutes = 30UL * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();

  // Each of these is drawn BEFORE the battle in render(), so each one hid it.
  struct { bool *flag; const char *name; } hiders[] = {
    { &galleryOpen, "the gallery" }, { &partyOpen, "the party" },
    { &gameOpen, "the ball game" }, { &trainOpen, "the training menu" },
    { &kbOpen, "the keyboard" }, { &clockOpen, "the clock" },
    { &movePickOpen, "the move picker" },
  };
  for (auto &h : hiders) {
    battleOpen = false;
    for (auto &o : hiders) *o.flag = false;   // one screen at a time
    *h.flag = true;
    char w[96];
    snprintf(w, sizeof(w), "precondition: %s is the screen on the panel", h.name);
    ck(strcmp(screen(), "main") != 0 && strcmp(screen(), "battle") != 0, w);
    type("BATTLE 25 20");
    snprintf(w, sizeof(w), "BATTLE from %s opens the fight", h.name);
    ck(battleOpen, w);
    snprintf(w, sizeof(w), "and closes %s, which would be drawn over it", h.name);
    ck(!*h.flag, w);
    snprintf(w, sizeof(w), "so the battle is the screen (%s)", screen());
    ck(!strcmp(screen(), "battle"), w);
    render();
  }

  // Screens that do not hide it are closed too, so leaving the fight does not
  // drop the player back onto something they never opened.
  battleOpen = false;
  cardOpen = true; playerOpen = true; menuOpen = true;
  type("BATTLE 25 20");
  ck(battleOpen && !cardOpen && !playerOpen && !menuOpen,
     "the card, the player card and the menu close as well");

  // A creature waiting for a party slot is dropped if the party is closed, so
  // the console refuses rather than closing it.
  battleOpen = false;
  partyPick = true; partyOpen = true;
  type("BATTLE 25 20");
  ck(!battleOpen && partyOpen, "with a farewell waiting on the party, BATTLE refuses");
  partyPick = false; partyOpen = false;
  // and the companion: the same command does open from the main screen
  type("BATTLE 25 20");
  ck(battleOpen, "and from the main screen it opens as normal");

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
