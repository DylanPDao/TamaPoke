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

  // Fixed-damage moves obey immunity too. battleAct() reads the type chart
  // before it asks for damage, so SEISMIC TOSS into a GHOST does nothing in a
  // real turn -- but battleDamage() returned the fixed amount before looking,
  // and the AI scored it as a hit (KO bonus and all) into a target it could
  // not touch. Each case pairs the fixed move with a feeble EMBER that DOES
  // land, so a chooser trusting the old estimate prefers the dud.
  {
    const char *fixedNames[] = { "SEISMIC TOSS", "NIGHT SHADE", "DRAGON RAGE" };
    uint8_t ember = 0;
    for (int i = 1; i < MOVE_COUNT; i++) if (!strcmp(MOVE_TBL[i].name, "EMBER")) ember = (uint8_t)i;
    ck(ember != 0, "EMBER is in the table");
    for (const char *nm : fixedNames) {
      uint8_t fx = 0;
      for (int i = 1; i < MOVE_COUNT; i++) if (!strcmp(MOVE_TBL[i].name, nm)) fx = (uint8_t)i;
      // a target immune to this move's type that EMBER still reaches
      int16_t vs = 0, ok = 0;
      for (int16_t d = 1; d <= DEX_COUNT && !vs; d++)
        if (typeEffVsDex(MOVE_TBL[fx].type, d) == 0 && typeEffVsDex(T_FIRE, d) > 0) vs = d;
      for (int16_t d = 1; d <= DEX_COUNT && !ok; d++)
        if (typeEffVsDex(MOVE_TBL[fx].type, d) > 0 && typeEffVsDex(T_FIRE, d) > 0) ok = d;
      char w[96];
      snprintf(w, sizeof(w), "%s: there is an immune target to test against", nm);
      ck(fx && vs && ok, w);
      if (!fx || !vs || !ok) continue;
      Combatant me, imm, hit;
      mk(me, 143, 50); mk(imm, vs, 50); mk(hit, ok, 50);
      me.base[SI_SPA] = 5;                      // EMBER barely scratches
      for (int k = 0; k < MOVE_SLOTS; k++) me.moves[k] = 0;
      me.moves[0] = fx; me.moves[1] = ember;
      imm.hp = 30; hit.hp = 30;                 // within reach of the fixed amount
      snprintf(w, sizeof(w), "%s does nothing to %s, in the estimate too",
               nm, DEX_TBL[vs].name);
      ck(battleDamage(me, imm, fx, false, 236) == 0, w);
      TurnLog tl; Combatant tgt = imm;
      battleAct(me, tgt, fx, tl);
      snprintf(w, sizeof(w), "%s: the estimate agrees with what battleAct does", nm);
      ck(tl.immune && tgt.hp == imm.hp, w);
      snprintf(w, sizeof(w), "%s: the hard AI does not throw it at an immune target", nm);
      ck(aiChooseMove(me, imm, true) == ember, w);
      // the companion: where it CAN land, it is still the better pick, so the
      // check above is about immunity and not about never choosing it at all
      snprintf(w, sizeof(w), "%s: but still picks it where it lands", nm);
      ck(battleDamage(me, hit, fx, false, 236) > 0 && aiChooseMove(me, hit, true) == fx, w);
    }
  }

  // Scores are int32_t, and the best one has to be too. A hit worth more than
  // 32767 used to wrap the int16_t `bestScore` negative, and any move at all --
  // even a stat boost -- then looked better than it.
  {
    uint8_t psy = 0, sd = 0;
    for (int i = 1; i < MOVE_COUNT; i++) {
      if (!strcmp(MOVE_TBL[i].name, "PSYCHIC")) psy = (uint8_t)i;
      if (!strcmp(MOVE_TBL[i].name, "SWORDS DANCE")) sd = (uint8_t)i;
    }
    Combatant me, wall; mk(me, 65, 100); mk(wall, 143, 100);
    wall.maxHp = wall.hp = 65535;               // so it is no KO either way
    wall.base[SI_SPD] = 1;
    for (int k = 0; k < MOVE_SLOTS; k++) me.moves[k] = 0;
    me.moves[0] = psy; me.moves[1] = sd;
    uint16_t dmg = 0;
    for (uint16_t a = 1; a < 2000; a++) {
      me.base[SI_SPA] = a;
      dmg = battleDamage(me, wall, psy, false, 236);
      if (dmg > 33000) break;
    }
    printf("     a %u-point PSYCHIC against SWORDS DANCE\n", dmg);
    ck(psy && sd && dmg > 32767 && dmg < 65000, "a hit too big for an int16_t score");
    ck(aiChooseMove(me, wall, true) == psy, "it is still the best move, not wrapped below a boost");
  }

  // and easy mode must stay dumb, or hard mode means nothing
  Combatant e1,e2; mk(e1,6,50); mk(e2,3,50);
  bool varied=false; uint8_t first=aiChooseMove(e1,e2,false);
  for(int i=0;i<40 && !varied;i++) if(aiChooseMove(e1,e2,false)!=first) varied=true;
  ck(varied,"easy mode still picks at random");

  printf("%s\n",bad?"FAILURES":"all good");
  return bad?1:0;
}
