// Party and box safety: what survives LEAVING those screens, and the two ways a
// waiting newcomer used to cost a creature on a single gesture.
//
//   * a RELEASE confirm armed on one creature's sheet must not survive a swipe
//     and greet the NEXT sheet opened ("Release B?", with YES over its moves)
//   * a box creature armed for a party slot (boxSel) must not survive leaving,
//     and must never answer a waiting newcomer's choice
//   * while a newcomer waits (party AND box full), a swipe, the top band and
//     LET GO all ASK before letting it go; tapping an occupied slot asks before
//     overwriting a banked creature; and both confirms are modal
//   * the feed picker's right edge is the candy, not a fifth "berry"
//   * the console's PARTY <dex> banks a creature with a legal moveset
//
// Every check runs through onTap()/onSwipe()/onSwipeV() -- the router -- and
// first asserts the screen it thinks it is on, so a check cannot pass by
// landing somewhere else (CLAUDE.md trap 3).
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "moves.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
uint32_t g_seed=77; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
static std::vector<std::string> gIn; static size_t gAt=0;
int FakeSerial::available(){ return gAt < gIn.size() ? 1 : 0; }
String FakeSerial::readStringUntil(char){
  if (gAt >= gIn.size()) return String("");
  return String(gIn[gAt++].c_str());
}
void setup(); void loop(); void render(); void handleSerial();
void onTap(int16_t,int16_t); void onSwipe(int); void onSwipeV(int);
uint8_t uiCurrentScreen();
int feedItemAt(int16_t x, int16_t y);
uint8_t learnableFor(int16_t dex, uint8_t lvl, uint8_t *out, uint8_t max);
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern bool partyOpen, boxOpen, movePickOpen, releaseConfirm, partyPick, menuOpen;
extern uint8_t partyDetail, boxDetail, boxSwapFrom, boxSel, pickConfirm, boxPage;
extern uint32_t feedMenuUntil, confirmUntil;
extern uint8_t choiceKind;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static const char *scr(){ return SCREEN_NAME[uiCurrentScreen()]; }

#define PCELL_X(i) (78 + ((i) % 2) * 160 + 40)
#define PCELL_Y(i) (88 + ((i) / 2) * 78 + 30)
#define SHEET_R_X 320
#define SHEET_BTN_Y (336 + 48/2)
#define CONF_YES_Y (206 + 52/2)
#define CONF_NO_Y  (268 + 52/2)
#define CONF_X 233
#define BOXBTN_CY (320 + 22)
#define PICKCONF_LETGO_T 0xFF   // the sketch's PICKCONF_LETGO: "let the newcomer go"

static PartyMon mon(int16_t dex, uint16_t lvl, const char *nick){
  PartyMon m; m.dex=dex; m.level=lvl; m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20;
  snprintf(m.nick,sizeof(m.nick),"%s",nick);
  return m;
}
static void clearAll(){
  for(int i=0;i<PARTY_SLOTS;i++) party.releaseAt(i);
  for(int i=0;i<BOX_SLOTS;i++) party.boxReleaseAt(i);
  partyOpen=boxOpen=movePickOpen=releaseConfirm=partyPick=menuOpen=false;
  partyDetail=boxDetail=boxSwapFrom=boxSel=pickConfirm=0;
  pet.endedKind=CER_NONE;
  feedMenuUntil=0; confirmUntil=0; choiceKind=0;
}
static void fillParty(){
  static const char *n[]={"AAA","BBB","CCC","DDD","EEE","FFF"};
  for(int i=0;i<PARTY_SLOTS;i++) party.slots[i]=mon(10+i,20,n[i]);
  party.save();
}
static void fillBox(){
  for(int i=0;i<BOX_SLOTS;i++) party.box[i]=mon(30+i,20,"");
  party.boxSave();
}
// a farewell waiting for a slot, with party and box both full: the loop's own
// entry path, not the flags set by hand
static void newcomerWaiting(){
  clearAll(); fillParty(); fillBox();
  pet.endedMon=mon(150,73,"NEWBIE");
  pet.endedKind=CER_FAREWELL;
  loop();
}

int main(){
  setup();
  for(int i=0;i<3;i++) loop();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  pet.ageMinutes = 60UL*40;
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping=false;
  pet.energy=pet.fullness=pet.joy=pet.hygiene=60;

  // ---- 1a. a release confirm does not survive a horizontal swipe off the sheet
  {
    clearAll();
    party.slots[0]=mon(25,30,"AAA"); party.slots[1]=mon(133,30,"BBB"); party.save();
    partyOpen=true;
    ck(!strcmp(scr(),"party"), "1a: on the party screen");
    onTap(PCELL_X(0), PCELL_Y(0));
    onTap(SHEET_R_X, SHEET_BTN_Y);
    ck(partyDetail==1 && releaseConfirm, "1a: RELEASE armed on A's sheet");
    onSwipe(-1);
    ck(partyDetail==0 && partyOpen, "1a: the swipe closed the sheet, party still up");
    onTap(PCELL_X(1), PCELL_Y(1));
    ck(partyDetail==2, "1a: B's sheet is open");
    ck(!releaseConfirm, "1a: and it is NOT already asking to release B");
    onTap(CONF_X, CONF_YES_Y);                 // where YES would have been
    ck(party.count()==2, "1a: a tap where YES was drawn releases nobody");
  }

  // ---- 1b. nor a vertical swipe out of the party altogether
  {
    clearAll();
    party.slots[0]=mon(25,30,"AAA"); party.slots[1]=mon(133,30,"BBB"); party.save();
    partyOpen=true;
    onTap(PCELL_X(0), PCELL_Y(0));
    onTap(SHEET_R_X, SHEET_BTN_Y);
    ck(releaseConfirm, "1b: RELEASE armed");
    onSwipeV(1); onSwipeV(1);
    ck(!partyOpen && !strcmp(scr(),"main"), "1b: two swipes leave the party");
    onSwipe(1);                                // swipe right reopens it
    ck(!strcmp(scr(),"party"), "1b: reopened");
    onTap(PCELL_X(1), PCELL_Y(1));
    ck(partyDetail==2 && !releaseConfirm, "1b: B's sheet comes up clean");
  }

  // ---- 1c. the box sheet: a vertical swipe used to leave the box with the
  //          sheet AND its confirm still set, so reopening the box showed them
  {
    clearAll();
    party.box[0]=mon(133,25,"EEVEE"); party.boxSave();
    partyOpen=true;
    onTap(233, BOXBTN_CY);
    ck(!strcmp(scr(),"box"), "1c: the box is up");
    onTap(PCELL_X(0), PCELL_Y(0));
    onTap(SHEET_R_X, SHEET_BTN_Y);
    ck(boxDetail==1 && releaseConfirm, "1c: RELEASE armed on the box sheet");
    onSwipeV(1);
    ck(boxDetail==0 && !releaseConfirm, "1c: a swipe closes the box sheet and its confirm");
    onSwipeV(1);
    ck(!strcmp(scr(),"party"), "1c: a second swipe backs out to the party");
    onTap(233, BOXBTN_CY);
    ck(!strcmp(scr(),"box") && boxDetail==0 && !releaseConfirm,
       "1c: reopening the box shows the grid, not a stale sheet");
  }

  // ---- 1d. a box creature armed for a party slot does not survive leaving
  {
    clearAll(); fillParty();
    party.box[0]=mon(143,40,"SNORLAX"); party.boxSave();
    partyOpen=true;
    onTap(233, BOXBTN_CY);
    onTap(PCELL_X(0), PCELL_Y(0));             // party full: go pick who steps out
    ck(boxSel==1 && !strcmp(scr(),"party"), "1d: boxSel armed, back on the party");
    onSwipe(1);                                // leave instead of picking
    ck(!partyOpen, "1d: the swipe left the party");
    ck(boxSel==0, "1d: and disarmed the box creature");
    onSwipe(1);
    onTap(PCELL_X(2), PCELL_Y(2));
    ck(party.slots[2].dex==12 && party.box[0].dex==143,
       "1d: the next party-slot tap swaps nothing");
    ck(partyDetail==3, "1d: it opens that creature's sheet, as a tap should");
  }

  // ---- 2a. the newcomer's swipe asks, and the confirm is modal
  {
    newcomerWaiting();
    ck(partyPick && !strcmp(scr(),"party"), "2a: the loop opened the party for a newcomer");
    onSwipe(-1);
    ck(partyPick && pet.endedKind==CER_FAREWELL, "2a: a horizontal swipe does NOT let it go");
    ck(pickConfirm==PICKCONF_LETGO_T, "2a: it asks instead");
    onSwipeV(1);
    ck(partyPick && pickConfirm==PICKCONF_LETGO_T, "2a: a vertical swipe with the confirm up does nothing");
    onTap(PCELL_X(0), PCELL_Y(0));             // a slot, under the panel but off its buttons
    ck(partyPick && party.slots[0].dex==10 && pickConfirm==PICKCONF_LETGO_T,
       "2a: modal: a miss neither lets go nor replaces");
    onTap(CONF_X, CONF_NO_Y);
    ck(partyPick && pickConfirm==0, "2a: NO goes back to choosing");
    onSwipeV(-1);
    ck(partyPick && pickConfirm==PICKCONF_LETGO_T, "2a: a vertical swipe asks too");
    onTap(CONF_X, CONF_NO_Y);
  }

  // ---- 2b. the top band and LET GO ask; YES really lets it go
  {
    newcomerWaiting();
    onTap(233, 20);
    ck(partyPick && pickConfirm==PICKCONF_LETGO_T, "2b: the top band asks");
    onTap(CONF_X, CONF_NO_Y);
    onTap(233, 400);                           // LET GO
    ck(partyPick && pet.endedKind==CER_FAREWELL && pickConfirm==PICKCONF_LETGO_T,
       "2b: LET GO asks");
    render();                                  // the confirm draws over the grid
    onTap(CONF_X, CONF_YES_Y);
    ck(!partyPick && pet.endedKind==CER_NONE && !partyOpen, "2b: YES lets it go and leaves");
    bool kept=true;
    for(int i=0;i<PARTY_SLOTS;i++) if(party.slots[i].dex!=10+i) kept=false;
    ck(kept && party.boxCount()==BOX_SLOTS, "2b: and nobody banked was touched");
  }

  // ---- 2c. replacing a banked creature asks first
  {
    newcomerWaiting();
    onTap(PCELL_X(2), PCELL_Y(2));
    ck(party.slots[2].dex==12, "2c: tapping an occupied slot does NOT replace at once");
    ck(pickConfirm==3, "2c: it asks about THAT slot");
    render();
    onTap(CONF_X, CONF_NO_Y);
    ck(party.slots[2].dex==12 && partyPick, "2c: NO keeps the banked creature");
    onTap(PCELL_X(2), PCELL_Y(2));
    onTap(CONF_X, CONF_YES_Y);
    ck(party.slots[2].dex==150, "2c: YES puts the newcomer there");
    ck(!partyPick && pet.endedKind==CER_NONE && !partyOpen, "2c: and the choice is over");
  }

  // ---- 2d. a stale boxSel never answers the newcomer's choice
  {
    newcomerWaiting();
    boxSel=1;                                  // armed by a path that missed the reset
    onTap(PCELL_X(0), PCELL_Y(0));
    ck(party.slots[0].dex==10 && party.box[0].dex==30, "2d: boxSel did not swap");
    ck(pickConfirm==1 && partyPick, "2d: the newcomer's confirm answered instead");
    onTap(CONF_X, CONF_NO_Y);
  }

  // ---- 2e. a newcomer that arrives while the BOX is up, with a sheet open,
  //          gets the party grid -- not the box answering its choice
  {
    clearAll(); fillParty(); fillBox();
    partyOpen=true; boxOpen=true; boxDetail=1; releaseConfirm=true;
    ck(!strcmp(scr(),"box"), "2e: the box sheet is up");
    pet.endedMon=mon(150,73,"NEWBIE");
    pet.endedKind=CER_FAREWELL;
    loop();
    ck(partyPick && !strcmp(scr(),"party"), "2e: the newcomer's choice is on the party grid");
    ck(boxDetail==0 && !releaseConfirm, "2e: with no stale sheet or confirm under it");
    onTap(CONF_X, CONF_NO_Y);
  }

  // ---- 5. the feed picker's last pixel is the candy
  {
    clearAll();
    int worst=-1;
    for(int x=90;x<=380;x++){ int it=feedItemAt(x,320); if(it>worst) worst=it; }
    ck(worst==3, "5: no x on the panel yields an item past the candy");
    ck(feedItemAt(365,320)==3, "5: the right edge is the candy");
    ck(feedItemAt(101,320)==0 && feedItemAt(100,320)==-1, "5: the left edge is the first berry");
    // and through the real tap path: candy raises weight, a berry does not
    pet.sleeping=false; pet.ceremony=CER_NONE;
    ck(!strcmp(scr(),"main") && !pet.sleeping, "5: on the main screen, awake");
    pet.fullness=40; pet.weight=10;
    feedMenuUntil=millis()+6000;
    onTap(365,320);
    ck(pet.weight>10, "5: a tap on the right edge feeds the candy");
    clearAll();
  }

  // ---- 6. PARTY <dex> banks a creature that can actually fight
  {
    clearAll();
    gIn={"PARTY CLEAR","PARTY 6"}; gAt=0;
    while(gAt<gIn.size()) handleSerial();
    const PartyMon &m=party.slots[0];
    ck(m.dex==6, "6: PARTY 6 banked a CHARIZARD");
    int n=0, illegal=0, attacks=0;
    uint8_t ok[64]; uint8_t k=learnableFor(m.dex,(uint8_t)m.level,ok,sizeof(ok));
    for(int i=0;i<MOVE_SLOTS;i++){
      if(!m.moves[i]) continue;
      n++;
      bool found=false; for(int j=0;j<k;j++) if(ok[j]==m.moves[i]) found=true;
      if(!found) illegal++;
      if(MOVE_TBL[m.moves[i]].cat!=MC_STATUS) attacks++;
    }
    printf("      PARTY 6 moves: %d (%d attacks)\n", n, attacks);
    ck(n==MOVE_SLOTS, "6: with a full moveset");
    ck(illegal==0, "6: every move one the picker would allow at its level");
    ck(attacks>0, "6: and at least one that does damage");
    clearAll();
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
