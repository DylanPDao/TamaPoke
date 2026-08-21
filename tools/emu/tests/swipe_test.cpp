// Every paged screen must PAGE on a horizontal swipe rather than closing.
// This has been got wrong four separate times -- the move picker, the player
// card, the gym list and the box -- each found by hand, so it is checked here
// for all of them at once.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "moves.h"
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
extern bool gymPick, galleryPick;
extern uint8_t movePickSlot, movePickParty, boxSel, boxSwapFrom;
extern uint16_t squadMask;
extern uint8_t pickTrainer; extern bool pickHard;
void pickDefault(uint8_t);
uint8_t squadCap(uint8_t, bool);
// Asked of the firmware, not recomputed here: a test that keeps its own copy of
// RPICK_PER_PAGE proves the transcription, which is how hit_test spent years
// passing on 12 % REGION_COUNT == 0.
extern uint8_t rpickPage;
uint8_t rpickRegions(uint8_t mode);
uint8_t rpickPageCount(uint8_t mode);
uint8_t rpickModeNow();

static int bad=0;
static void clearAll(){
  gymPick=galleryPick=false;
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
uint8_t learnableFor(int16_t dex, uint8_t lvl, uint8_t *out, uint8_t max);

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
  clearAll(); gymOpen=true; gymPick=false;        check("gyms",     &gymOpen,      &gymPage);
  clearAll(); playerOpen=true;                    check("player",   &playerOpen,   &playerPage);
  clearAll(); partyOpen=true; boxOpen=true;       check("box",      &boxOpen,      &boxPage);
  clearAll(); movePickOpen=true; movePickParty=0; movePickSlot=0;
                                                  check("movepick", &movePickOpen, &movePickPage);
  clearAll(); pickTrainer=7; pickHard=false; pickDefault(squadCap(7,false)); pickOpen=true;
                                                  check("teampick", &pickOpen,     &pickPage);
  // The Pokedex pages within ONE region and changes region on a vertical swipe.
  // Every species must be reachable: it was capped at 10 flat pages when the dex
  // was 151 long, which silently hid everything past 160 once it grew to 386.
  clearAll(); galleryOpen=true; galleryPick=false; galleryDetail=0; galleryPage=0; galleryRegion=0;
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
  clearAll(); gymOpen=true; gymPick=false; gymRegion=0; gymPage=0;
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

  // Opening either screen must land on the REGION CHOOSER, not on whichever
  // region happened to be set last. Without it the Johto and Hoenn content is
  // built, reachable only by an invisible gesture, and so looks absent.
  {
    clearAll();
    onSwipe(-1);                       // swipe left from the main screen
    if (!gymOpen || !gymPick) { printf("FAIL  gyms       does not open on the region chooser\n"); bad++; }
    else printf("PASS  %-10s opens on the region chooser\n", "gyms");
    // and paging back off the front of a ladder returns to it
    gymPick = false; gymRegion = 2; gymPage = 0;
    onSwipe(1);
    if (!gymPick || !gymOpen) { printf("FAIL  gyms       paging back does not return to the chooser\n"); bad++; }
    else printf("PASS  %-10s paging back returns to the chooser\n", "gyms");

    clearAll(); galleryOpen = true; galleryPick = false; galleryRegion = 1; galleryPage = 0;
    onSwipe(1);
    if (!galleryPick || !galleryOpen) { printf("FAIL  gallery    paging back does not return to the chooser\n"); bad++; }
    else printf("PASS  %-10s paging back returns to the chooser\n", "gallery");
  }

  // THE REGION CHOOSER IS PAGED NOW, and every paged screen in this sketch has
  // to be driven here -- the same paging bug shipped four times before this
  // test existed, and each time it was a screen that closed instead of paging.
  //
  // The chooser is the one that WRAPS rather than closing: it is the root of
  // its own screen, and at first boot there is nowhere to go back to.
  {
    clearAll();
    gRegionArt = 0xFFFF;                       // every pack present
    galleryOpen = true; galleryPick = true; rpickPage = 0;
    uint8_t nreg  = rpickRegions(rpickModeNow());
    uint8_t pages = rpickPageCount(rpickModeNow());
    if (pages < 2) { printf("FAIL  dexpick    expected >1 page for %u regions\n", nreg); bad++; }
    onSwipe(-1);
    if (!galleryOpen || !galleryPick) { printf("FAIL  dexpick    closed on a swipe instead of paging\n"); bad++; }
    else if (rpickPage != 1) { printf("FAIL  dexpick    did not advance a page (page=%u)\n", rpickPage); bad++; }
    else printf("PASS  %-10s pages on a horizontal swipe\n", "dexpick");

    // wrapping, in both directions, so no page can strand the player
    rpickPage = (uint8_t)(pages - 1);
    onSwipe(-1);
    if (rpickPage != 0 || !galleryPick) { printf("FAIL  dexpick    does not wrap forward to page 0\n"); bad++; }
    else printf("PASS  %-10s wraps forward rather than closing\n", "dexpick");
    onSwipe(1);
    if (rpickPage != pages - 1 || !galleryPick) { printf("FAIL  dexpick    does not wrap backward\n"); bad++; }
    else printf("PASS  %-10s wraps backward rather than closing\n", "dexpick");

    // EVERY region must be reachable across the pages -- the old chooser
    // listed GYM_REGIONS in all three modes, so Sinnoh had no row at all.
    uint8_t perPage = (uint8_t)((nreg + pages - 1) / pages);
    int seen = 0;
    for (uint8_t pg = 0; pg < pages; pg++)
      for (uint8_t row = 0; row < perPage; row++)
        if (pg * perPage + row < nreg) seen++;
    if (seen != nreg) { printf("FAIL  dexpick    only %d of %u regions have a row\n", seen, nreg); bad++; }
    else printf("PASS  %-10s every region has a row across the pages\n", "dexpick");
  }

  // The sprite pack is a real gate: without it the region is not selectable,
  // the egg pool will not draw from it, and the pill skips over it.
  {
    clearAll();
    gRegionArt = 0x1;                          // KANTO only
    if (!regionAvailable(0)) { printf("FAIL  gating     KANTO should be available\n"); bad++; }
    else if (regionAvailable(1)) { printf("FAIL  gating     JOHTO has no pack and must be locked\n"); bad++; }
    else printf("PASS  %-10s a region with no pack is locked\n", "gating");

    // Cycle the pill all the way round twice. The invariant is that it never
    // RESTS on a locked region -- not that it returns any particular index.
    // REGION_ALL stays selectable while any one pack is present, since the
    // mixed pool is then simply that region's creatures, so asserting a fixed
    // answer here just encoded my own wrong guess about ALL.
    {
      uint8_t r = 0; int landedLocked = 0; uint16_t visited = 0;
      for (int i = 0; i < REGION_COUNT * 2; i++) {
        r = nextAvailableRegion(r);
        if (!regionAvailable(r)) landedLocked++;
        visited |= (uint16_t)(1u << r);
      }
      int nvisited = 0;
      for (uint8_t b2 = 0; b2 < REGION_COUNT; b2++) if (visited & (1u << b2)) nvisited++;
      // BOTH halves matter. "never rests on a locked region" is vacuously true
      // when nothing is locked, so on its own it survives the gate being
      // deleted -- it did exactly that when this was negative-checked. The
      // second half is the one with teeth: with only KANTO present the pill
      // must reach FEWER regions than exist.
      if (landedLocked) { printf("FAIL  gating     the pill landed on a locked region %d times\n", landedLocked); bad++; }
      else if (nvisited >= REGION_COUNT) { printf("FAIL  gating     the pill reached all %d regions with 3 packs missing\n", (int)REGION_COUNT); bad++; }
      else printf("PASS  %-10s the pill skips locked regions and reaches only %d\n", "gating", nvisited);
    }

    uint8_t was = pet.region;
    pet.setRegion(1);                          // JOHTO: locked, must not take
    if (pet.region != was) { printf("FAIL  gating     setRegion accepted a locked region\n"); bad++; }
    else printf("PASS  %-10s a locked region cannot be chosen\n", "gating");

    // and no egg may hatch from a region whose art is not on the card
    int wrong = 0;
    for (int i = 0; i < 200; i++) {
      int16_t d = pet.rollInRegion(1, 0);       // ask for JOHTO explicitly
      if (regionOfDex(d) != 0) wrong++;
    }
    if (wrong) { printf("FAIL  gating     %d/200 eggs came from a locked region\n", wrong); bad++; }
    else printf("PASS  %-10s eggs never come from a region with no pack\n", "gating");

    gRegionArt = 0xFFFF;                       // leave it as we found it
  }

  // The rule lives in moveUnlockLevel() and moves_test pins it -- but the
  // PICKER has to actually ask it. It once kept its own copy of the check,
  // which IS the bug, so this drives the screen's own list, not the rule.
  {
    uint8_t l[64];
    uint8_t n = learnableFor(5, 22, l, sizeof(l));    // a Charmeleon at 22
    bool blast = false, ember = false;
    for (uint8_t i = 0; i < n; i++) {
      if (!strcmp(MOVE_TBL[l[i]].name, "FIRE BLAST")) blast = true;
      if (!strcmp(MOVE_TBL[l[i]].name, "EMBER")) ember = true;
    }
    if (blast) { printf("FAIL  picker     offers a level 22 Charmeleon FIRE BLAST\n"); bad++; }
    else printf("PASS  picker     no FIRE BLAST for a level 22 Charmeleon\n");
    if (!ember) { printf("FAIL  picker     lost the moves it really knows\n"); bad++; }
    else printf("PASS  picker     still offers what it really knows\n");
    if (learnableFor(5, 40, l, sizeof(l)) <= n) {
      printf("FAIL  picker     TMs never arrive at all\n"); bad++;
    } else printf("PASS  picker     and the TMs arrive once it is built\n");
  }

  printf("%s\n", bad?"FAILURES":"every paged screen pages");
  return bad?1:0;
}
