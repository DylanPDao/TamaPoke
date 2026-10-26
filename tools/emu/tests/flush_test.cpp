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
extern uint8_t partyDetail, boxDetail;
extern bool boxOpen, releaseConfirm;
extern uint8_t pickConfirm;
extern Party party;
extern uint8_t sdAsk;   // the unreadable-SD dialog: 1 ask, 2 formatting, 3 ok, 4 failed
void startBattle(int16_t dex, uint8_t lvl);

extern uint32_t gFlushCalls;

static int bad = 0;
static void clearAll(){
  cardOpen=galleryOpen=clockOpen=kbOpen=menuOpen=partyOpen=partyPick=false;
  trainOpen=movePickOpen=battleOpen=gymOpen=playerOpen=false;
  boxOpen=releaseConfirm=false;
  partyDetail=boxDetail=pickConfirm=0;
  sdAsk=0;
}
// Two questions, because the canvas no longer pushes every frame it is handed
// (FrameCanvas drops a frame identical to the one on the panel -- frame_test).
//   asked:  the render path called gfx->flush() at all. This is the frozen-panel
//           bug itself, and it must not depend on what the frame contains.
//   pushed: the frame really went out when the screen was FIRST shown. Every
//           check below puts up a screen different from the one before it, so
//           a screen that asks but never reaches the panel fails here.
static void check(const char *name){
  gfx->frameReady = false;
  uint32_t asked = gFlushCalls;
  render();
  if (gFlushCalls == asked) { printf("FAIL  %-12s never flushed -- the panel would freeze\n", name); bad++; }
  else if (!gfx->frameReady) { printf("FAIL  %-12s flushed, but the first frame never reached the panel\n", name); bad++; }
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
  // a clean NVS opens on the first-boot language question -- checked FIRST,
  // before anything else has put that same frame on the panel
  check("lang"); crumbIs("lang");
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
  // The detail sheets and the release confirm drawn over them. A creature has to
  // be IN the slot: an empty one falls through to the grid, which flushes for a
  // different reason and would pass without ever drawing a sheet.
  {
    PartyMon m; m.dex=25; m.level=30; m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20;
    snprintf(m.nick,sizeof(m.nick),"PIKA");
    party.slots[0]=m; party.box[0]=m; party.save(); party.boxSave();
  }
  clearAll(); partyOpen=true; partyDetail=1;                      check("partysheet");
  clearAll(); partyOpen=true; partyDetail=1; releaseConfirm=true;  check("partyconfirm");
  clearAll(); partyOpen=true; boxOpen=true; boxDetail=1;           check("boxsheet");
  clearAll(); partyOpen=true; boxOpen=true; boxDetail=1; releaseConfirm=true; check("boxconfirm");
  // every state of the SD dialog, drawn over a game that is fully set up
  // behind it -- it must win and it must flush in each
  for (uint8_t k=1;k<=4;k++){ clearAll(); sdAsk=k; char n[16]; snprintf(n,sizeof(n),"sdcard%u",k); check(n); }
  // the newcomer's two confirms: let it go, and replace slot 1 with it
  pet.endedMon = party.slots[0];
  clearAll(); partyOpen=true; partyPick=true; pickConfirm=0xFF;   check("pickletgo");
  clearAll(); partyOpen=true; partyPick=true; pickConfirm=1;      check("pickreplace");

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
  clearAll(); sdAsk=1; menuOpen=true; crumbIs("sdcard");
  clearAll();

  printf("%s\n", bad ? "FAILURES" : "every screen flushes");
  return bad?1:0;
}
