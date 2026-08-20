// Every screen must flush, or the panel freezes on the previous frame.
// Headless screenshots CANNOT catch this: shotMode reads gfx->buffer()
// directly and never looks at frameReady, which is exactly how the training
// submenu shipped frozen.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=7; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
// wasPressed lives in the sketch and millis() in clock.cpp; both are linked in
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render();
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Arduino_Canvas *gfx;
extern Pet pet;
extern bool cardOpen, galleryOpen, clockOpen, kbOpen, menuOpen, partyOpen, partyPick;
extern bool trainOpen, movePickOpen, battleOpen, gymOpen, playerOpen;
extern uint8_t cardPage;
void startBattle(int16_t dex, uint8_t lvl);

static int bad = 0;
static void clearAll(){
  cardOpen=galleryOpen=clockOpen=kbOpen=menuOpen=partyOpen=partyPick=false;
  trainOpen=movePickOpen=battleOpen=gymOpen=playerOpen=false;
}
static void check(const char *name){
  gfx->frameReady = false;
  render();
  if (!gfx->frameReady) { printf("FAIL  %-12s never flushed -- the panel would freeze\n", name); bad++; }
  else printf("PASS  %-12s flushes\n", name);
}

// The crash breadcrumb has to name the screen that is ACTUALLY drawn, or a
// crash report points at the wrong one -- which is worse than no report, since
// it sends the next person hunting in the wrong file. Checked here because
// this test already knows how to put each screen up.
static void crumbIs(const char *want){
  render();
  const char *got = SCREEN_NAME[uiCurrentScreen()];
  if (strcmp(got, want)) {
    printf("FAIL  crumb says '%s' while '%s' is on the panel\n", got, want); bad++;
  } else printf("PASS  crumb   %-12s named correctly\n", want);
}
int main(){
  setup();
  for (int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  pet.ageMinutes = 60UL*60;
  while (pet.hasLearnOffer()) pet.declineLearn();

  clearAll(); check("main");
  clearAll(); trainOpen=true;    check("train");
  clearAll(); movePickOpen=true; check("movepick");
  clearAll(); gymOpen=true;      check("gyms");
  clearAll(); playerOpen=true;   check("player");
  clearAll(); menuOpen=true;     check("menu");
  clearAll(); partyOpen=true;    check("party");
  clearAll(); clockOpen=true;    check("clock");
  for (uint8_t p=0;p<4;p++){ clearAll(); cardOpen=true; cardPage=p;
    char n[16]; snprintf(n,sizeof(n),"card%u",p); check(n); }
  clearAll(); startBattle(9,50); check("battle");

  clearAll();                       crumbIs("main");
  clearAll(); trainOpen=true;       crumbIs("train");
  clearAll(); menuOpen=true;        crumbIs("menu");
  clearAll(); partyOpen=true;       crumbIs("party");
  clearAll(); gymOpen=true;         crumbIs("gym");
  clearAll(); playerOpen=true;      crumbIs("player");
  clearAll(); movePickOpen=true;    crumbIs("movepick");
  clearAll(); clockOpen=true;       crumbIs("clock");
  clearAll(); cardOpen=true;        crumbIs("card");
  clearAll(); startBattle(9,50);    crumbIs("battle");
  clearAll();

  printf("%s\n", bad ? "FAILURES" : "every screen flushes");
  return bad?1:0;
}
