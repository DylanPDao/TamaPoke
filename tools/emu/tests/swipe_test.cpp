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
void setup(); void render(); void onSwipe(int dir); void onSwipeV(int dir);
extern Pet pet;
extern bool cardOpen, galleryOpen, clockOpen, kbOpen, menuOpen, partyOpen, partyPick;
extern bool trainOpen, movePickOpen, battleOpen, gymOpen, playerOpen, boxOpen, pickOpen;
extern uint8_t cardPage, gymPage, playerPage, movePickPage, boxPage, pickPage, partyDetail;
extern int galleryPage; extern bool galleryDirty; extern uint8_t galleryDetail;
extern uint8_t galleryRegion;
extern uint8_t gymRegion;
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
  // The Pokedex pages within ONE region and changes region on a vertical swipe.
  // Every species must be reachable: it was capped at 10 flat pages when the dex
  // was 151 long, which silently hid everything past 160 once it grew to 386.
  clearAll(); galleryOpen=true; galleryDetail=0; galleryPage=0; galleryRegion=0;
  onSwipe(-1);
  if (!galleryOpen) { printf("FAIL  gallery    closed on a swipe instead of paging\n"); bad++; }
  else if (galleryPage != 1) { printf("FAIL  gallery    did not advance (page=%d)\n", galleryPage); bad++; }
  else printf("PASS  %-10s pages on a horizontal swipe\n", "gallery");
  {
    // walk every region to its last page and tick off what it can show
    static bool seen[DEX_COUNT + 1] = { false };
    int regions = REGION_COUNT - 1;
    for (int r = 0; r < regions; r++) {
      galleryRegion = (uint8_t)r;
      galleryPage = 0;
      int lo = REGIONS[r].lo, hi = REGIONS[r].hi;
      int pages = (hi - lo + 1 + 15) / 16;
      for (int p = 0; p < pages; p++) {
        for (int i = 0; i < 16; i++) {
          int d = lo + p * 16 + i;
          if (d <= hi && d <= DEX_COUNT) seen[d] = true;
        }
        onSwipe(-1);
      }
      if (galleryPage != pages - 1) {
        printf("FAIL  gallery    %s stops at page %d of %d\n",
               REGIONS[r].name, galleryPage + 1, pages);
        bad++;
      }
    }
    int missing = 0;
    for (int d = 1; d <= DEX_COUNT; d++) if (!seen[d]) missing++;
    if (missing) { printf("FAIL  gallery    %d species are unreachable\n", missing); bad++; }
    else printf("PASS  %-10s every one of the %d species is reachable\n", "gallery", DEX_COUNT);
    // and a vertical swipe really does move between regions
    galleryRegion = 0; galleryDetail = 0;
    onSwipeV(1);
    if (galleryRegion != 1 || !galleryOpen) {
      printf("FAIL  gallery    a vertical swipe does not change region\n"); bad++;
    } else printf("PASS  %-10s changes region on a vertical swipe\n", "gallery");
  }
  // The gym ladder changes REGION on a vertical swipe, the same gesture the
  // Pokedex uses. Without this the Johto and Hoenn ladders exist but cannot be
  // reached, which is a worse failure than a paging bug: nothing looks broken.
  clearAll(); gymOpen=true; gymRegion=0; gymPage=0;
  {
    bool ok = true;
    for (int r = 1; r <= GYM_REGIONS; r++) {
      onSwipeV(1);
      if (gymRegion != r % GYM_REGIONS || !gymOpen) ok = false;
    }
    if (!ok) { printf("FAIL  gyms       vertical swipe does not cycle the ladders\n"); bad++; }
    else printf("PASS  %-10s cycles all %d ladders on a vertical swipe\n", "gyms", GYM_REGIONS);
    for (int r = 0; r < GYM_REGIONS; r++) {
      gymRegion = (uint8_t)r; gymPage = 0; gymOpen = true;
      onSwipe(-1);
      if (gymPage != 1 || !gymOpen) {
        printf("FAIL  gyms       %s does not page\n", TRAINER_SETS[r].region); bad++;
      }
    }
    if (!bad) printf("PASS  %-10s every ladder still pages horizontally\n", "gyms");
  }

  printf("%s\n", bad?"FAILURES":"every paged screen pages");
  return bad?1:0;
}
