// A tap or a swipe goes to the screen that is DRAWN.
//
// render(), onTap(), onSwipe() and onSwipeV() each kept their own if-chain over
// the same screen flags, in different orders. Where they disagreed, the player
// pressed one screen and another answered:
//
//   * a level-up during the ball game: render() drew the game, onTap() asked
//     hasLearnOffer() first -- every tap at the ball was an invisible "replace
//     this move?" answer, and a move was overwritten for good;
//   * the keyboard opened from the player card: drawn on top, but taps went to
//     the card underneath (closing it, or cycling the avatar);
//   * a horizontal swipe on the LAN screen had no handler at all and opened the
//     party over it, with lanOpen still set.
//
// All of them switch on uiCurrentScreen() now. Each check below first proves
// the situation really arose (an offer is pending, the game is the screen),
// because a routing test that never reaches the conflict passes vacuously.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=2024; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop(); void render();
void onTap(int16_t,int16_t); void onSwipe(int); void onSwipeV(int);
void startGame(); void leaveGame(); void openKeyboardFor(uint8_t);
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern bool gameOpen, playerOpen, kbOpen, lanOpen, partyOpen, gymOpen, cardOpen;
extern uint8_t playerPage;
#define LEARN_ROW_Y(i) (104 + (i) * 56)

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static const char *scr(){ return SCREEN_NAME[uiCurrentScreen()]; }
static bool sameMoves(const uint8_t *a){ return !memcmp(a, pet.moves, MOVE_SLOTS); }

// A level-up with something to learn, without waiting an hour of real time.
static void queueAnOffer(){
  pet.ageMinutes = 40UL * MINUTES_PER_LEVEL;   // CHARMELEON range: gates to pass
  pet.lastLearnLevel = 1;
  pet.checkLearnGates();
}

int main(){
  setup();
  for (int i = 0; i < 3; i++) loop();
  pet.dbgHatchAs(4, false);                       // CHARMANDER
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  pet.energy = pet.fullness = pet.joy = pet.hygiene = 90;
  ck(!strcmp(scr(), "main"), "starts on the main screen");

  // ---- 1. a level-up during the ball game
  startGame();
  ck(gameOpen && !strcmp(scr(), "minigame"), "the ball game is the screen");
  queueAnOffer();
  ck(pet.hasLearnOffer(), "a move offer is pending behind it (the conflict really arose)");
  uint8_t before[MOVE_SLOTS]; memcpy(before, pet.moves, MOVE_SLOTS);
  // straight onto the first learn row -- where a reach for the ball lands
  for (int i = 0; i < MOVE_SLOTS; i++) onTap(233, LEARN_ROW_Y(i) + 20);
  ck(sameMoves(before), "tapping the game does NOT answer the hidden offer");
  ck(pet.hasLearnOffer(), "and the offer is still waiting");
  ck(!strcmp(scr(), "minigame"), "and the game is still the screen");
  leaveGame();
  ck(!strcmp(scr(), "learn"), "back from the game, the offer is what is drawn");
  // modal: a swipe must not open something over it
  onSwipeV(1); onSwipe(1);
  ck(!playerOpen && !partyOpen && !gymOpen, "a swipe does not open a screen over the offer");
  while (pet.hasLearnOffer()) pet.declineLearn();

  // ---- 2. the keyboard opened from the player card
  playerOpen = true; playerPage = 0;
  ck(!strcmp(scr(), "player"), "the player card is open");
  onTap(233, 50);                                 // the name: opens the keyboard
  ck(kbOpen && !strcmp(scr(), "keyboard"), "tapping the name draws the keyboard over it");
  uint8_t av = pet.avatar;
  onTap(233, 100);                                // where the card has its avatar
  ck(pet.avatar == av, "a key press does not cycle the avatar underneath");
  ck(playerOpen, "nor close the card underneath");
  ck(kbOpen || !strcmp(scr(), "player"), "the keyboard got the tap");
  kbOpen = false; playerOpen = false;

  // ---- 3. a horizontal swipe on the LAN screen
  lanOpen = true;
  ck(!strcmp(scr(), "lan"), "the LAN screen is open");
  onSwipe(1);
  ck(!lanOpen, "a horizontal swipe backs out of it, as a vertical one does");
  ck(!partyOpen && !gymOpen, "and does not open the party or the gyms over it");
  ck(!strcmp(scr(), "main"), "landing back on the main screen");

  // ---- the breadcrumb names what render() draws, in the case that used to differ
  cardOpen = true; queueAnOffer();
  ck(pet.hasLearnOffer() && cardOpen, "an offer pending over the card");
  ck(!strcmp(scr(), "learn"), "uiCurrentScreen() says learn, which is what render() draws");
  while (pet.hasLearnOffer()) pet.declineLearn();
  cardOpen = false;

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
