// A whole gym run: squad chaining on both sides, badge award, and the ladder's
// difficulty curve measured rather than assumed.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "trainers.h"
#include "dex.h"
#include <cstdio>
uint32_t g_seed = 999;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0, g_touchY=0; volatile bool g_touchDown=false;
bool wasPressed=false;
uint32_t millis(){return 0;}
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}

static void foeOf(Combatant &c, uint8_t dex, uint8_t lvl, uint8_t iv) {
  Pet f; f.dbgHatchAs(dex,false);
  f.ivAtk=f.ivDef=f.ivSpe=f.ivHp=iv;
  f.ageMinutes=(uint32_t)(lvl?lvl-1:0)*MINUTES_PER_LEVEL;
  f.relearnFromLevel(); combatantFromPet(c,f);
}
// one 1v1, returns true if `you` survived
static bool duel(Combatant &you, Combatant &foe) {
  TurnLog lg; int t=0;
  while(!you.fainted() && !foe.fainted() && t<300){
    t++;
    uint8_t my=you.moves[random(MOVE_SLOTS)], mf=foe.moves[random(MOVE_SLOTS)];
    Combatant *a=&you,*b=&foe; uint8_t ma=my,mb=mf;
    if(!battleMovesFirst(you,my,foe,mf)){a=&foe;b=&you;ma=mf;mb=my;}
    battleAct(*a,*b,ma,lg);
    if(!b->fainted()) battleAct(*b,*a,mb,lg);
    battleEndTurn(you,lg); battleEndTurn(foe,lg);
  }
  return !you.fainted();
}
int main(){
  printf("%-10s %-9s %s\n","TRAINER","TEAM","solo win-rate for one L%d creature");
  for (int lvl : {40, 60, 73, 100}) {
    printf("\n--- your creature: CHARIZARD L%d, perfect IVs ---\n", lvl);
    for (int ti=0; ti<TRAINER_COUNT; ti++){
      const Trainer &t=TRAINERS[ti];
      int wins=0, runs=40;
      for(int r=0;r<runs;r++){
        Pet p; p.dbgHatchAs(6,false);
        p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=31;
        p.ageMinutes=(uint32_t)(lvl-1)*MINUTES_PER_LEVEL;
        p.relearnFromLevel();
        Combatant you; combatantFromPet(you,p);
        bool alive=true;
        for(int k=0;k<t.count && alive;k++){
          Combatant foe; foeOf(foe,t.team[k].dex,t.team[k].level,EASY_IV);
          alive=duel(you,foe);
        }
        if(alive) wins++;
      }
      printf("  %-10s x%d  top L%-3d  %3d%%\n", t.name, t.count,
             [&]{uint8_t m=0;for(int k=0;k<t.count;k++)if(t.team[k].level>m)m=t.team[k].level;return m;}(),
             wins*100/runs);
    }
  }
  return 0;
}
