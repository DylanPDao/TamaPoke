// Every paged screen must PAGE on a horizontal swipe rather than closing.
// This has been got wrong four separate times -- the move picker, the player
// card, the gym list and the box -- each found by hand, so it is checked here
// for all of them at once.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
uint32_t g_seed=2; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render(); void onSwipe(int dir);
extern Pet pet;
extern bool cardOpen, galleryOpen, clockOpen, kbOpen, menuOpen, partyOpen, partyPick;
extern bool trainOpen, movePickOpen, battleOpen, gymOpen, playerOpen, boxOpen, pickOpen;
extern uint8_t cardPage, gymPage, playerPage, movePickPage, boxPage, pickPage, partyDetail;
extern uint8_t movePickSlot, movePickParty, boxSel, boxSwapFrom;
extern uint16_t squadMask;
extern uint8_t pickTrainer; extern bool pickHard;
void pickDefault(uint8_t);
uint8_t squadCap(uint8_t, bool);

static int bad=0;
static void clearAll(){
  cardOpen=galleryOpen=clockOpen=kbOpen=menuOpen=partyOpen=partyPick=false;
  trainOpen=movePickOpen=battleOpen=gymOpen=playerOpen=boxOpen=pickOpen=false;
  partyDetail=0; boxSel=boxSwapFrom=0;
}
// swipe left; the page must advance and the screen must stay open
static void check(const char *name, bool *open, uint8_t *page){
  *page = 0;
  onSwipe(-1);
  if (!*open) { printf("FAIL  %-10s closed on a swipe instead of paging\n", name); bad++; return; }
  if (*page != 1) { printf("FAIL  %-10s did not advance a page (page=%u)\n", name, *page); bad++; return; }
  printf("PASS  %-10s pages on a horizontal swipe\n", name);
}
int main(){
  setup();
  for (int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  pet.ageMinutes = 60UL*60; pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  for (int i=0;i<PARTY_SLOTS;i++){ PartyMon m; m.dex=9+i; m.level=40;
    m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20; party.replaceAt(i,m); }
  for (int i=0;i<BOX_SLOTS;i++){ PartyMon m; m.dex=1+i; m.level=20;
    m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20; party.box[i]=m; }

  clearAll(); cardOpen=true;                      check("card",     &cardOpen,     &cardPage);
  clearAll(); gymOpen=true;                       check("gyms",     &gymOpen,      &gymPage);
  clearAll(); playerOpen=true;                    check("player",   &playerOpen,   &playerPage);
  clearAll(); partyOpen=true; boxOpen=true;       check("box",      &boxOpen,      &boxPage);
  clearAll(); movePickOpen=true; movePickParty=0; movePickSlot=0;
                                                  check("movepick", &movePickOpen, &movePickPage);
  clearAll(); pickTrainer=7; pickHard=false; pickDefault(squadCap(7,false)); pickOpen=true;
                                                  check("teampick", &pickOpen,     &pickPage);
  printf("%s\n", bad?"FAILURES":"every paged screen pages");
  return bad?1:0;
}
