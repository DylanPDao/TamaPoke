// The box is a separate NVS key on purpose. This checks the swap is a real
// exchange in both directions, that it persists, and -- most importantly --
// that a save written before the box existed still loads its party intact.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "dex.h"
#include <cstdio>
uint32_t g_seed=3; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static PartyMon mk(int dex,int lvl){ PartyMon m; m.dex=dex; m.level=lvl;
  m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20; return m; }

int main(){
  // a save from BEFORE the box existed: party key only, no box key
  { Preferences seed; seed.begin("tamapoke", false);
    PartyMon old[PARTY_SLOTS];
    for (int i=0;i<PARTY_SLOTS;i++) old[i]=mk(1+i*20, 30+i);
    seed.putBytes("party", old, sizeof(old));
    seed.end(); }
  Party p; p.begin();
  bool kept = true;
  for (int i=0;i<PARTY_SLOTS;i++) if (p.slots[i].dex != 1+i*20) kept=false;
  ck(kept, "a pre-box save keeps its whole party");
  ck(p.boxCount()==0, "and comes up with an empty box, not garbage");

  // deposit: party slot 0 <-> empty box slot 0
  int16_t was = p.slots[0].dex;
  p.swapPartyBox(0, 0);
  ck(p.box[0].dex==was && p.slots[0].empty(), "swapping into an empty box slot deposits");
  // withdraw: the same call the other way round
  p.swapPartyBox(0, 0);
  ck(p.slots[0].dex==was && p.box[0].empty(), "and swapping back withdraws");

  // a real exchange, both occupied
  p.box[3] = mk(150, 70);
  int16_t a = p.slots[2].dex, b = p.box[3].dex;
  p.swapPartyBox(2, 3);
  ck(p.slots[2].dex==b && p.box[3].dex==a, "two occupied slots exchange");

  // and it survives a reload
  Party q; q.begin();
  ck(q.slots[2].dex==b && q.box[3].dex==a, "the swap persists across a reload");

  ck(p.boxFirstFree()==0, "boxFirstFree finds the hole");
  for (int i=0;i<BOX_SLOTS;i++) p.box[i]=mk(19,5);
  ck(p.boxFirstFree()==-1 && !p.boxAdd(mk(1,1)), "a full box refuses more");

  // a farewell with a full party must reach the box rather than being stuck
  { Party r; r.begin();
    for (int i=0;i<PARTY_SLOTS;i++) r.slots[i]=mk(1+i,40);
    for (int i=0;i<BOX_SLOTS;i++) r.box[i]=PartyMon();
    r.save(); r.boxSave();
    PartyMon newcomer = mk(150, 73);
    bool toParty = r.add(newcomer);
    bool toBox = toParty ? false : r.boxAdd(newcomer);
    ck(!toParty && toBox, "a full party sends the newcomer to the box");
    ck(r.box[0].dex==150, "and it is really there");
    // a full party AND a full box is the only case that should refuse
    for (int i=0;i<BOX_SLOTS;i++) r.box[i]=mk(19,5);
    ck(!r.add(newcomer) && !r.boxAdd(newcomer),
       "only a full party AND a full box refuses, which is when the player picks");
  }

  // withdrawing into a party that has room must not need a party slot picked
  { Party r; r.begin();
    for (int i=0;i<PARTY_SLOTS;i++) r.slots[i]=PartyMon();
    for (int i=0;i<BOX_SLOTS;i++) r.box[i]=PartyMon();
    r.slots[0]=mk(6,50); r.box[0]=mk(9,40);
    r.save(); r.boxSave();
    int free = r.firstFree();
    ck(free==1, "the first free party slot is found");
    r.swapPartyBox((uint8_t)free, 0);
    ck(r.slots[1].dex==9 && r.box[0].empty(),
       "withdrawing into a free slot moves it without displacing anyone");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
