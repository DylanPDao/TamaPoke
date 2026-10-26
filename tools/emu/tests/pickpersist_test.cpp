// A move chosen in the picker, and the avatar, reach NVS by themselves.
//
// Both called pet.flushSave(), which writes ONLY when a save is already
// pending. With nothing pending -- right after any other save -- the choice
// stayed in RAM, and a reboot or a crash put the old moveset or avatar back.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=61; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop(); void onTap(int16_t,int16_t);
uint8_t learnableList(uint8_t *out, uint8_t max);
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern bool cardOpen, movePickOpen, playerOpen;
extern uint8_t cardPage, playerPage;
#define MOVE_ROW_Y(i) (96 + (i) * 58)
#define MOVE_PICK_Y(i) (76 + (i) * 58)

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  setup();
  for (int i = 0; i < 3; i++) loop();
  pet.dbgHatchAs(6, false);
  pet.ageMinutes = 60UL * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.saveNow();
  ck(!pet.savePending(), "nothing pending (the case that lost the choice)");

  // ---- the move picker, through the real taps
  cardOpen = true; cardPage = 2;
  onTap(233, MOVE_ROW_Y(0) + 20);
  ck(movePickOpen && !strcmp(SCREEN_NAME[uiCurrentScreen()], "movepick"), "the picker is open for slot 0");
  uint8_t all[64]; uint8_t n = learnableList(all, sizeof(all));
  int row = -1;
  for (int i = 0; i < n && i < 5; i++) if (all[i] != pet.moves[0]) { row = i; break; }
  ck(row >= 0, "there is a different move to pick");
  uint8_t want = all[row];
  onTap(233, MOVE_PICK_Y(row) + 20);
  ck(pet.moves[0] == want, "slot 0 holds the picked move in RAM");
  { Pet q; q.begin(); ck(q.moves[0] == want, "and after a reboot -- it was SAVED"); }
  cardOpen = false;

  // ---- the avatar
  pet.saveNow();
  playerOpen = true; playerPage = 0;
  uint8_t av = pet.avatar;
  onTap(233, 100);
  ck(pet.avatar != av, "tapping the avatar cycles it");
  { Pet q; q.begin(); ck(q.avatar == pet.avatar, "and the new avatar survives a reboot"); }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
