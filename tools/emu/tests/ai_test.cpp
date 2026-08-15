// Does the hard-mode AI actually play better? A "smart" chooser that does not
// beat random is not smart, so this measures it head to head rather than
// asserting the code merely runs.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "trainers.h"
#include "dex.h"
#include "types.h"
#include <cstdio>
uint32_t g_seed=4242; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static void mk(Combatant &c,int16_t dex,uint8_t lvl){
  Pet p; p.dbgHatchAs(dex,false);
  p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=31;
  p.ageMinutes=(uint32_t)(lvl-1)*MINUTES_PER_LEVEL;
  p.relearnFromLevel(); combatantFromPet(c,p);
}
// returns true if side A won
static bool duel(int16_t da,int16_t db,uint8_t lvl,bool aSmart,bool bSmart){
  Combatant A,B; mk(A,da,lvl); mk(B,db,lvl);
  TurnLog lg; int t=0;
  while(!A.fainted()&&!B.fainted()&&t<300){
    t++;
    uint8_t ma=aiChooseMove(A,B,aSmart), mb=aiChooseMove(B,A,bSmart);
    Combatant *f=&A,*s=&B; uint8_t mf=ma,ms=mb;
    if(!battleMovesFirst(A,ma,B,mb)){f=&B;s=&A;mf=mb;ms=ma;}
    battleAct(*f,*s,mf,lg);
    if(!s->fainted()) battleAct(*s,*f,ms,lg);
    battleEndTurn(A,lg); battleEndTurn(B,lg);
  }
  return !A.fainted();
}
int main(){
  // mirror matches: same species both sides, so only the choosing differs
  const int16_t roster[]={6,9,3,65,68,143,25,94};
  int smartWins=0,total=0;
  for(int i=0;i<8;i++)
    for(int r=0;r<25;r++){
      if(duel(roster[i],roster[i],50,true,false)) smartWins++;
      total++;
    }
  int pct=smartWins*100/total;
  printf("     mirror matches, smart vs random: %d/%d = %d%%\n",smartWins,total,pct);
  ck(pct>60,"the hard AI beats the random one in a mirror match");

  // it must take a kill when one is available
  Combatant me,foe; mk(me,6,50); mk(foe,3,50);
  foe.hp=6;                         // one hit from fainting
  uint8_t pick=aiChooseMove(me,foe,true);
  uint16_t dmg=battleDamage(me,foe,pick,false,236);
  printf("     foe at 6hp: picks %s for ~%u\n",MOVE_TBL[pick].name,dmg);
  ck(dmg>=foe.hp,"it takes the kill when one is on the table");

  // it must not throw a move the target is immune to
  Combatant norm,ghost; mk(norm,143,50); mk(ghost,94,50);
  bool everImmune=false;
  for(int i=0;i<40;i++){
    uint8_t p2=aiChooseMove(norm,ghost,true);
    if(MOVE_TBL[p2].cat!=MC_STATUS && typeEffVsDex(MOVE_TBL[p2].type,ghost.dex)==0) everImmune=true;
  }
  ck(!everImmune,"it never picks a move the target is immune to");

  // and easy mode must stay dumb, or hard mode means nothing
  Combatant e1,e2; mk(e1,6,50); mk(e2,3,50);
  bool varied=false; uint8_t first=aiChooseMove(e1,e2,false);
  for(int i=0;i<40 && !varied;i++) if(aiChooseMove(e1,e2,false)!=first) varied=true;
  ck(varied,"easy mode still picks at random");

  printf("%s\n",bad?"FAILURES":"all good");
  return bad?1:0;
}
