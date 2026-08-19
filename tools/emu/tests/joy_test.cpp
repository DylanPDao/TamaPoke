// Joy from the ball game, that leaving early still banks it, and that the three
// trainers raise BOND in proportion to the session.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <chrono>
#include <thread>
#include <cstdio>
uint32_t g_seed=0xC0FFEE; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop();
extern Pet pet;
extern bool gameOpen;
extern uint8_t gameScore;
void startGame(); void leaveGame();
int main(){
  setup(); for(int i=0;i<4;i++) loop();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(25,false);

  printf("direct playResult:\n");
  for (int sc : {0, 3, 8, 20}) {
    pet.joy = 40;
    pet.playResult((uint8_t)sc);
    printf("  score %2d: joy 40 -> %u\n", sc, pet.joy);
  }
  // and the path that actually matters: start a game, score, leave early
  pet.joy = 40;
  startGame();
  gameScore = 9;              // as if nine rallies had landed
  leaveGame();
  printf("\nleave early with score 9: joy 40 -> %u, gameOpen=%d, record=%u\n",
         pet.joy, (int)gameOpen, pet.gameHi);
  int bad = pet.joy > 40 ? 0 : 1;

  // --- the ball game is DEFENCE's trainer now
  {
    Pet p; p.begin(); p.dbgHatchAs(25, false);
    p.ivAtk = p.ivDef = p.ivSpe = p.ivHp = 31;
    p.trDef = 0;
    p.playResult(20);
    printf("\nball game trains DEF: trDef 0 -> %u\n", p.trDef);
    if (!p.trDef) { printf("FAIL: the ball game did not train defence\n"); bad = 1; }
  }

  // --- every trainer bonds, and a bigger session bonds more
  {
    const char *names[3] = { "bag (ATK)", "reaction (SPE)", "ball (DEF)" };
    for (int t = 0; t < 3; t++) {
      uint8_t small = 0, big = 0;
      {
        Pet p; p.begin(); p.dbgHatchAs(25, false);
        p.ivAtk = p.ivDef = p.ivSpe = p.ivHp = 31;
        p.bond = 0;   // bondToday is private and starts at 0 on a fresh Pet
        if (t == 0) p.trainStrength(4); else if (t == 1) p.trainSpeed(2); else p.playResult(2);
        small = p.bond;
      }
      {
        Pet p; p.begin(); p.dbgHatchAs(25, false);
        p.ivAtk = p.ivDef = p.ivSpe = p.ivHp = 31;
        p.bond = 0;   // bondToday is private and starts at 0 on a fresh Pet
        if (t == 0) p.trainStrength(80); else if (t == 1) p.trainSpeed(40); else p.playResult(40);
        big = p.bond;
      }
      printf("  %-15s bond: small session +%u, full session +%u\n", names[t], small, big);
      if (!small) { printf("FAIL: %s gave no bond at all\n", names[t]); bad = 1; }
      if (big <= small) { printf("FAIL: %s does not bond more for a bigger session\n", names[t]); bad = 1; }
    }
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad;
}
