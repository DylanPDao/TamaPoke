// Battle animation state: a hit must flag the right side, the HP bar must ease
// rather than snap, and none of it may change the actual battle result.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include <cstdio>
uint32_t g_seed=31; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render();
extern Pet pet;
extern bool battleOpen, btlOver;
extern Combatant btlYou, btlFoe;
extern uint8_t btlMsgCount;
extern uint32_t btlHitUntil[2], btlLungeUntil[2];
extern uint16_t btlHpShown[2];
void startBattle(int16_t dex, uint8_t lvl);

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  setup(); for(int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  pet.ageMinutes = 50UL*MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();

  startBattle(9, 50);
  ck(battleOpen, "battle starts");
  ck(btlHpShown[0]==btlYou.maxHp && btlHpShown[1]==btlFoe.maxHp,
     "the eased bars start full");

  // drop the foe's health and confirm the bar walks down instead of jumping
  btlFoe.hp = btlFoe.maxHp / 2;
  uint16_t first = btlHpShown[1];
  render();
  uint16_t afterOne = btlHpShown[1];
  ck(afterOne < first, "the bar starts draining after a hit");
  ck(afterOne > btlFoe.hp, "and does NOT snap straight to the new value");
  int frames = 1;
  while (btlHpShown[1] != btlFoe.hp && frames < 400) { render(); frames++; }
  printf("     bar reached the real value in %d frames\n", frames);
  ck(btlHpShown[1]==btlFoe.hp, "it does converge");
  ck(frames > 3 && frames < 200, "over a sensible number of frames");

  // healing must ease upward too, not only downward
  btlFoe.hp = btlFoe.maxHp;
  int up=0; while (btlHpShown[1] != btlFoe.hp && up < 400) { render(); up++; }
  ck(btlHpShown[1]==btlFoe.maxHp, "it eases upward on a heal as well");

  // animation timers must never alter the fight itself
  uint16_t hp = btlYou.hp;
  for (int i=0;i<20;i++) render();
  ck(btlYou.hp==hp, "rendering alone never changes anyone's health");
  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
