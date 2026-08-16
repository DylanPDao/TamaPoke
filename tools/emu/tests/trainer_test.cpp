// The trainer's name belongs to the player, not the creature: it must outlive
// every ending, and the shared keyboard must write it to the right place.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
uint32_t g_seed=6; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
static uint32_t g_ms=0; uint32_t millis(){return g_ms;}
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
int main(){
  Pet p; p.begin();
  if (p.awaitingStarter()) p.chooseStarter(4);
  if (p.isEgg()) p.dbgHatchAs(4,false);
  p.renameTrainer("DYLAN");
  p.rename("EMBER");
  ck(!strcmp(p.trainerName,"DYLAN"), "the trainer name is stored");
  ck(!strcmp(p.nick,"EMBER"), "and the creature's nickname is separate");

  p.newEgg();
  ck(!strcmp(p.trainerName,"DYLAN"), "survives newEgg()");
  // the creature's nickname is cleared at HATCH, not at newEgg -- it lingers
  // through the egg stage, which is the existing behaviour, not a bug
  p.dbgHatchAs(7, false);
  ck(p.nick[0]==0, "while the creature's nickname goes when the next one hatches");
  ck(!strcmp(p.trainerName,"DYLAN"), "and the trainer name still does not");

  Pet q; q.begin();
  ck(!strcmp(q.trainerName,"DYLAN"), "survives a save/load round trip");

  // and every ending
  const char *names[]={"farewell","runaway","release"};
  for (int e=0;e<3;e++){
    Pet r; r.begin();
    if (r.isEgg()) r.dbgHatchAs(4,false);
    r.renameTrainer("DYLAN");
    r.ageMinutes = 4UL*24*60;
    if (e==0) r.startFarewell(); else if (e==1) r.startRunaway(); else r.release();
    g_ms += 60000; r.update(g_ms);
    char msg[64]; snprintf(msg,sizeof(msg),"survives a %s",names[e]);
    ck(!strcmp(r.trainerName,"DYLAN"), msg);
  }
  // long names must be cut, not overflow
  p.renameTrainer("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
  ck(strlen(p.trainerName) <= 11, "an over-long name is truncated safely");
  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
