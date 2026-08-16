// A revived companion is FROZEN: it does not age, cannot evolve, and cannot be
// lost. Those three are the whole feature, so each is checked directly.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "dex.h"
#include <cstdio>
uint32_t g_seed=11; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
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
  PartyMon m; m.dex=6; m.level=61; m.shiny=1;
  m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=24; m.trAtk=m.trDef=m.trSpe=35;
  m.moves[0]=1; m.moves[1]=2;
  snprintf(m.nick,sizeof(m.nick),"BLAZE");

  p.reviveFrom(m);
  ck(p.speciesId==6 && p.level()==61, "comes back at the level it was banked at");
  ck(p.shiny && !strcmp(p.nick,"BLAZE"), "keeps its shininess and its name");
  ck(p.moves[0]==1 && p.moves[1]==2, "and its moveset");
  ck(p.frozen, "and is marked frozen");

  // it must not age, however long passes
  uint8_t lvl = p.level();
  for (int i=0;i<400;i++){ g_ms += 60000; p.update(g_ms);
    p.fullness=p.joy=p.energy=p.hygiene=100; }
  printf("     after ~400 game-minutes: level %u (was %u)\n", p.level(), lvl);
  ck(p.level()==lvl, "does not age");

  // and cannot be taken away
  p.ageMinutes = 10UL*24*60;
  ck(!p.canFarewellNow(), "is never offered a farewell");
  p.fullness=p.joy=p.energy=p.hygiene=0;
  for (int i=0;i<200;i++){ g_ms += 60000; p.update(g_ms); }
  ck(!p.canRunawayNow(), "cannot run away even when wholly neglected");
  ck(!p.canEvolveNow(), "cannot evolve past the form it was banked in");

  // it survives a reload, still frozen
  Pet q; q.begin();
  ck(q.frozen && q.speciesId==6, "stays frozen across a reload");

  // and a brand new egg is a normal life again
  q.newEgg();
  ck(!q.frozen, "a new egg is not frozen");
  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
