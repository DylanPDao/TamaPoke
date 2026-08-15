// Joy from the ball game, and that leaving early still banks it.
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
  return pet.joy > 40 ? 0 : 1;
}
