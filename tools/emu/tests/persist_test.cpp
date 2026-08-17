// Player-wide progress must outlive the creature that earned it. Badges, the
// avatar, the streak and the Pokedex all belong to the player, not the pet, so
// none of the three endings may take them.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
uint32_t g_seed=9; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
static uint32_t g_ms=0; uint32_t millis(){return g_ms;}
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static void award(Pet &p){
  p.winBadge(0,0,false); p.winBadge(0,1,false); p.winBadge(0,0,true);
  p.winBadge(2,3,false);   // and one in another region's ladder
  p.avatar = 2; p.streak = 9; p.bestStreak = 11; p.totalMedals = 5;
}
static bool intact(Pet &p){
  return p.hasBadge(0,0,false) && p.hasBadge(0,1,false) && p.hasBadge(0,0,true)
      && p.hasBadge(2,3,false)
      && p.avatar==2 && p.streak==9 && p.bestStreak==11 && p.totalMedals==5;
}

int main(){
  Pet p; p.begin();
  if (p.awaitingStarter()) p.chooseStarter(4);
  if (p.isEgg()) p.dbgHatchAs(4,false);
  award(p);
  ck(intact(p), "progress is set");

  // 1. a new egg (what every ending eventually calls)
  p.newEgg();
  ck(intact(p), "survives newEgg()");

  // 2. a reload from NVS
  Pet q; q.begin();
  ck(intact(q), "survives a save/load round trip");

  // 3. each of the three endings, end to end
  const char *names[] = {"farewell","runaway","release"};
  for (int e=0;e<3;e++){
    Pet r; r.begin();
    if (r.isEgg()) r.dbgHatchAs(4,false);
    award(r);
    r.ageMinutes = 4UL*24*60;
    if (e==0) r.startFarewell(); else if (e==1) r.startRunaway(); else r.release();
    g_ms += 60000;                       // let the ceremony expire
    r.update(g_ms);
    char msg[64]; snprintf(msg,sizeof(msg),"survives a %s",names[e]);
    ck(intact(r), msg);
    Pet after; after.begin();
    snprintf(msg,sizeof(msg),"...and is still there after reloading (%s)",names[e]);
    ck(intact(after), msg);
  }
  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
