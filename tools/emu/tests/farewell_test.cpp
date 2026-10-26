// The farewell offer: asked once a day, not on every reboot or every tap.
//
// Reported from a board: "it keeps asking whether to keep it or let it go,
// sometimes several times in a row, although I chose to keep it". Three things
// did that, and each is pinned here:
//
//   * the answer lived in RAM only, so every restart asked again at once;
//   * the gold button lies over the creature, and closing the question without
//     answering (a miss, or the 12 s timeout) left it up -- the next tap meant
//     to pet the creature opened the question again;
//   * there was no way to say "never ask again".
//
// Plus a leak found on the way: the answers were never cleared on a new egg,
// so one creature's "keep form" hid the NEXT creature's evolution.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=5; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop(); void render();
void onTap(int16_t,int16_t);
uint8_t uiCurrentScreen();
void uiConfirmRects(int *b1Top, int *b1Bot, int *b2Top, int *b2Bot);
void uiConfirmRect3(int *b3Top, int *b3Bot);
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern uint8_t choiceKind;
extern uint32_t choiceUntil;
// the gold button: FAR_BTN_Y 176, FAR_BTN_H 58, full width around the centre
#define FAR_TAP_X 233
#define FAR_TAP_Y 205

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// A CHARIZARD (no further evolution) that has earned its farewell.
static void readyForFarewell(){
  pet.dbgHatchAs(6, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.ageMinutes = FAREWELL_AGE_MIN + 10;
  pet.sleeping = false;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 90;
}
static void openQuestion(){
  choiceKind = 0;
  onTap(FAR_TAP_X, FAR_TAP_Y);
}
static bool afterReboot(bool (Pet::*q)() const){
  Pet again; again.begin();
  return (again.*q)();
}

int main(){
  setup();
  for (int i = 0; i < 3; i++) loop();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  readyForFarewell();
  ck(!strcmp(SCREEN_NAME[uiCurrentScreen()], "main"), "on the main screen");
  ck(pet.wantFarewellButton(), "a final form at three days is offered its farewell");

  int b1t, b1b, b2t, b2b, b3t, b3b;
  uiConfirmRects(&b1t, &b1b, &b2t, &b2b);
  uiConfirmRect3(&b3t, &b3b);
  ck(b3t >= b2b + 8 && b3b - b3t >= 44, "KEEP FOREVER is a full-size button of its own, below STAY");

  // ---- 1. a miss is "stay together", and does not come straight back
  openQuestion();
  ck(choiceKind == 2, "tapping the gold button asks the question");
  onTap(20, 233);
  ck(choiceKind == 0 && pet.ceremony == CER_NONE, "a miss closes it without saying goodbye");
  ck(!pet.wantFarewellButton(), "and counts as STAY TOGETHER: the button is gone");
  openQuestion();
  ck(choiceKind == 0, "so the next tap on the creature pets it instead of asking again");

  // ---- 2. the answer survives a restart
  ck(!afterReboot(&Pet::wantFarewellButton), "after a reboot it is still not asked");

  // ---- 3. but it IS asked again a day later: that part is the design
  pet.ageMinutes += 1441;
  ck(pet.wantFarewellButton(), "a day later the farewell is offered again");

  // ---- 4. the timeout is "stay together" too
  openQuestion();
  ck(choiceKind == 2, "asked again");
  choiceUntil = millis() - 1;
  render();
  ck(choiceKind == 0 && !pet.wantFarewellButton(), "left unanswered until it closes: stay together");

  // ---- 5. KEEP FOREVER
  pet.ageMinutes += 1441;
  openQuestion();
  ck(choiceKind == 2, "asked a third time");
  onTap(233, (b3t + b3b) / 2);
  ck(pet.keptForever() && pet.ceremony == CER_NONE, "KEEP FOREVER keeps it, without a goodbye");
  pet.ageMinutes += 30UL * 1440;
  ck(!pet.wantFarewellButton(), "and a month later it is still not asked");
  ck(!afterReboot(&Pet::wantFarewellButton) && afterReboot(&Pet::keptForever), "nor after a reboot");
  ck(pet.retireIsFree(), "RETIRE in the menu still says goodbye, and still for free");

  // ---- 6. per creature: a new egg starts with no answers
  pet.newEgg();
  ck(!pet.keptForever(), "the next creature has not been kept forever");
  readyForFarewell();
  ck(pet.wantFarewellButton(), "and is offered its own farewell");

  // the same leak, on the evolve offer: CHARMANDER at the level it evolves
  pet.dbgHatchAs(4, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.ageMinutes = 20UL * MINUTES_PER_LEVEL;
  pet.sleeping = false;
  ck(pet.wantEvolveButton(), "a CHARMANDER at 21 is offered its evolution");
  pet.declineEvolve();
  ck(!pet.wantEvolveButton() && !afterReboot(&Pet::wantEvolveButton),
     "KEEP FORM holds, across a reboot too");
  pet.newEgg();
  pet.dbgHatchAs(4, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.ageMinutes = 20UL * MINUTES_PER_LEVEL;
  pet.sleeping = false;
  ck(pet.wantEvolveButton(), "the NEXT CHARMANDER at the same level is offered its own");

  // ---- 7. GOODBYE still says goodbye
  readyForFarewell();
  openQuestion();
  onTap(233, (b1t + b1b) / 2);
  ck(pet.ceremony == CER_FAREWELL, "GOODBYE starts the farewell");

  printf(bad ? "\n%d FAILED\n" : "\nall passed\n", bad);
  return bad ? 1 : 0;
}
