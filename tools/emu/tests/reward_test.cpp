// What beating a gym leader is worth. The badge was the whole reward, which
// made the ladder a one-way checklist -- a leader you can already beat had
// nothing to offer. A win now trains the creature that fought.
//
// The rules that matter are the ones that stop it being either useless or a
// loophole: it can never cross the IV-bound cap, and it never lands on a stat
// that is already there (a random grant that evaporated would read as a bug
// rather than as luck).
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
uint32_t g_seed=11; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  // --- it trains something, and the total really moves
  {
    Pet p; p.begin(); p.dbgHatchAs(6,false);
    p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=31;
    p.trAtk=p.trDef=p.trSpe=0;
    uint8_t which=9;
    uint8_t got = p.rewardTraining(7, which);
    ck(got==7, "a win trains by the amount it is worth");
    ck(which<3, "into one of the three stats");
    ck(p.trAtk+p.trDef+p.trSpe==7, "and the training really lands");
  }

  // --- the IV-bound cap is never crossed
  {
    Pet p; p.begin(); p.dbgHatchAs(6,false);
    p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=8;      // a mediocre individual, low ceiling
    p.trAtk = p.trMaxAtk() - 2;
    p.trDef = p.trMaxDef() - 2;
    p.trSpe = p.trMaxSpe() - 2;
    uint8_t which=0;
    uint8_t got = p.rewardTraining(50, which);
    ck(got==2, "a big reward is trimmed to the headroom that was left");
    ck(p.trAtk<=p.trMaxAtk() && p.trDef<=p.trMaxDef() && p.trSpe<=p.trMaxSpe(),
       "and no stat ends up past its IV-bound ceiling");
  }

  // --- a fully trained creature says so rather than silently eating the win
  {
    Pet p; p.begin(); p.dbgHatchAs(6,false);
    p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=20;
    p.trAtk=p.trMaxAtk(); p.trDef=p.trMaxDef(); p.trSpe=p.trMaxSpe();
    uint8_t which=0;
    ck(p.rewardTraining(9, which)==0, "a maxed creature gains nothing");
    ck(p.trAtk==p.trMaxAtk(), "and is not pushed over");
  }

  // --- THE IMPORTANT ONE: it never picks a stat that is already capped.
  // Random across all three would waste the reward two thirds of the time here,
  // and the player would just see a win that did nothing.
  {
    Pet p; p.begin(); p.dbgHatchAs(6,false);
    p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=25;
    int wasted = 0, toSpe = 0;
    for (int i = 0; i < 60; i++) {
      p.trAtk = p.trMaxAtk();            // ATK and DEF full every time
      p.trDef = p.trMaxDef();
      p.trSpe = 0;                       // only SPE has room
      uint8_t which=0;
      uint8_t got = p.rewardTraining(4, which);
      if (!got) wasted++;
      if (which==2) toSpe++;
    }
    ck(wasted==0, "with one stat free, no win is ever wasted");
    ck(toSpe==60, "every reward goes to the stat that has room");
  }

  // --- and with room everywhere it really does vary
  {
    Pet p; p.begin(); p.dbgHatchAs(6,false);
    p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=31;
    bool hit[3] = {false,false,false};
    for (int i = 0; i < 90; i++) {
      p.trAtk=p.trDef=p.trSpe=0;
      uint8_t which=0;
      p.rewardTraining(3, which);
      if (which<3) hit[which]=true;
    }
    ck(hit[0]&&hit[1]&&hit[2], "over many wins all three stats come up");
  }

  // --- an egg has nothing to train
  {
    Pet p; p.begin(); p.newEgg();
    uint8_t which=0;
    ck(p.rewardTraining(5, which)==0, "an egg trains nothing");
  }

  // --- it survives a reload, which is the point of saving it
  {
    Pet p; p.begin(); p.dbgHatchAs(25,false);
    p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=31;
    p.trAtk=p.trDef=p.trSpe=0;
    uint8_t which=0;
    p.rewardTraining(6, which);
    uint8_t a=p.trAtk, d=p.trDef, s=p.trSpe;
    Pet q; q.begin();
    ck(q.trAtk==a && q.trDef==d && q.trSpe==s, "the training is persisted");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
