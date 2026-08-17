// The link over a radio that misbehaves. link_test uses a perfect transport,
// which is exactly the condition that never holds on a real ESP-NOW broadcast.
// This one drops, duplicates and reorders frames on purpose, and asserts the
// two sides still finish the same fight.
//
// This is the whole reason the transport is a function pointer. None of it
// needs a board -- only the radio itself does.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "link.h"
#include <cstdio>
#include <vector>
uint32_t g_seed=31; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// ---- a deliberately awful radio -------------------------------------------
static Link A, B;
static int dropEvery = 0;      // drop every Nth frame; 0 = drop nothing
static int dupEvery = 0;       // deliver every Nth frame twice
static bool deaf = false;      // nothing gets through at all
static int seen = 0, dropped = 0;

static void deliver(Link &to, const uint8_t *b, uint8_t n) {
  to.onPacket(b, n);
}
static void wire(Link &to, const uint8_t *b, uint8_t n) {
  seen++;
  if (deaf) { dropped++; return; }
  if (dropEvery && (seen % dropEvery) == 0) { dropped++; return; }
  // copy first: onPacket may send, which can clobber the caller's buffer
  uint8_t tmp[260]; if (n > sizeof(tmp)) return;
  memcpy(tmp, b, n);
  deliver(to, tmp, n);
  if (dupEvery && (seen % dupEvery) == 0) deliver(to, tmp, n);
}
static void toB(void*, const uint8_t*b, uint8_t n){ wire(B, b, n); }
static void toA(void*, const uint8_t*b, uint8_t n){ wire(A, b, n); }

static LinkMon mon(int16_t dex, uint8_t lvl, const char *nm){
  Pet p; p.dbgHatchAs(dex,false);
  p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=25;
  p.ageMinutes=(uint32_t)(lvl-1)*MINUTES_PER_LEVEL;
  p.relearnFromLevel();
  Combatant c; combatantFromPet(c,p);
  snprintf(c.name,sizeof(c.name),"%s",nm);
  LinkMon m; linkMonFrom(m,c); return m;
}

// Both sides tick on the same simulated clock, which is what lets a resend
// actually happen: nothing here retries unless time passes.
static uint32_t T = 0;
static void run(uint32_t ms, uint32_t step = 100) {
  for (uint32_t t = 0; t < ms; t += step) { T += step; A.tick(T); B.tick(T); }
}

static void pair(bool hostA, bool hostB) {
  A.send=toB; B.send=toA;
  A.begin(hostA,"A"); B.begin(hostB,"B");
  A.id = 0x1111; B.id = 0x2222;
  A.addMon(mon(6,50,"BLAZE")); A.addMon(mon(9,50,"SHELL"));
  B.addMon(mon(65,50,"SPOON"));
  A.tick(T); B.tick(T);
  A.start();
}

int main(){
  // --- pairing survives a lossy channel ------------------------------------
  seen=dropped=0; dropEvery=3; dupEvery=0;
  pair(true,false);
  run(4000);
  ck(A.state==LINK_READY && B.state==LINK_READY,
     "pairing completes with every third frame dropped");
  ck(A.theirsN==1 && B.theirsN==2, "and both squads arrive whole");
  ck(B.theirs[0].dex==6 && B.theirs[1].dex==9, "with nothing missing in the middle");
  printf("      (%d frames, %d dropped)\n", seen, dropped);

  // --- a squad is never half-accepted --------------------------------------
  // The old code set theirsN from the highest index seen, so losing packet 0
  // left a creature with dex 0 that the battle would happily fight.
  {
    Link C, D;
    static Link *pC=&C, *pD=&D;
    static int k = 0;
    C.send=[](void*,const uint8_t*b,uint8_t n){ if (++k==2) return; pD->onPacket(b,n); };
    D.send=[](void*,const uint8_t*b,uint8_t n){ pC->onPacket(b,n); };
    C.begin(true,"C"); D.begin(false,"D");
    C.id=1; D.id=2;
    C.addMon(mon(6,50,"ONE")); C.addMon(mon(9,50,"TWO")); D.addMon(mon(25,50,"PIKA"));
    C.start();
    ck(D.state != LINK_READY, "a squad with a hole in it is not READY");
    for (int i=0;i<40;i++){ T+=200; C.tick(T); D.tick(T); }
    ck(D.state==LINK_READY && D.theirs[0].dex==6 && D.theirs[1].dex==9,
       "and the resend fills the hole");
  }

  // --- a whole fight over a lossy link -------------------------------------
  seen=dropped=0; dropEvery=3; dupEvery=5;
  pair(true,false);
  run(4000);
  ck(A.ready() && B.ready(), "a fight can start");
  int turns = 0;
  uint8_t hp = 200;
  for (int i = 0; i < 12 && A.state==LINK_READY; i++) {
    B.sendAct(1);                       // the guest chooses
    run(3000);                          // resends until the host hears it
    if (!A.hasPeerAct()) break;
    LinkResult r{};
    hp = (uint8_t)(hp > 20 ? hp - 20 : 0);
    r.hostHp = hp; r.guestHp = hp; r.hostMove = 1; r.guestMove = 1;
    A.sendResult((const uint8_t*)&r, sizeof(r));
    run(3000);                          // resends until the guest hears it
    if (B.resultNew) { B.resultNew = false; turns++; }
  }
  ck(turns == 12, "twelve turns complete over a lossy, duplicating link");
  ck(A.turn == B.turn, "and both sides agree which turn they are on");
  printf("      (%d frames, %d dropped, turn=%u)\n", seen, dropped, A.turn);

  // --- a duplicate action must not spend two turns --------------------------
  seen=dropped=0; dropEvery=0; dupEvery=1;    // EVERY frame arrives twice
  pair(true,false);
  run(3000);
  ck(A.ready() && B.ready(), "pairing survives every frame arriving twice");
  uint8_t t0 = A.turn;
  B.sendAct(1);
  run(1000);
  ck(A.hasPeerAct() && A.turn==t0, "a doubled action is still one action");
  LinkResult r{}; r.hostHp=90; r.guestHp=90;
  A.sendResult((const uint8_t*)&r, sizeof(r));
  run(1000);
  ck(A.turn == (uint8_t)(t0+1), "the host advances exactly one turn");
  ck(B.turn == A.turn, "and so does the guest, despite the duplicates");

  // --- a missed result is recovered from the next one ----------------------
  // LinkResult is absolute, not a delta, so a lost turn costs its narration
  // and nothing else.
  {
    seen=dropped=0; dropEvery=0; dupEvery=0;
    pair(true,false);
    run(3000);
    B.sendAct(1); run(600);
    deaf = true;                        // the guest hears nothing at all
    LinkResult r1{}; r1.hostHp=80; r1.guestHp=80;
    A.sendResult((const uint8_t*)&r1, sizeof(r1));
    deaf = false;
    uint8_t missed = A.turn;
    B.resultNew = false;
    // the host moves on; the guest never saw turn `missed`
    A.pendingAct = 1;
    LinkResult r2{}; r2.hostHp=55; r2.guestHp=55;
    A.sendResult((const uint8_t*)&r2, sizeof(r2));
    run(200);
    LinkResult got{}; memcpy(&got,B.result,sizeof(got));
    ck(B.resultNew && got.hostHp==55,
       "a guest that missed a turn takes the next one's absolute state");
    ck(B.turn == A.turn, "and is back in step immediately");
    (void)missed;
  }

  // --- silence ends as LOST, never as a hang -------------------------------
  {
    seen=dropped=0; dropEvery=0; dupEvery=0; deaf=true;
    pair(true,false);
    run(LINK_PAIR_TIMEOUT_MS + 2000, 500);
    ck(A.state==LINK_LOST, "a peer that never answers ends as LOST");
    ck(!A.live(), "and the link is not live");
    deaf=false;
  }
  {
    // and mid-fight, on the shorter deadline
    seen=dropped=0; deaf=false;
    pair(true,false);
    run(3000);
    ck(A.ready(), "paired");
    deaf=true;
    run(LINK_BATTLE_TIMEOUT_MS + 2000, 500);
    ck(A.state==LINK_LOST && B.state==LINK_LOST, "both give up mid-fight");
    deaf=false;
  }

  // --- leaving on purpose is instant, not a timeout ------------------------
  {
    pair(true,false);
    run(3000);
    A.sendBye();
    ck(B.state==LINK_LOST, "a goodbye drops the peer at once");
    ck(A.state==LINK_LOST, "and the sender too");
  }

  // --- rematch keeps the squads and resets the turn ------------------------
  {
    pair(true,false);
    run(3000);
    B.sendAct(1); run(600);
    LinkResult r{}; r.hostHp=10; r.guestHp=10;
    A.sendResult((const uint8_t*)&r, sizeof(r));
    run(600);
    A.sendEnd(true);
    ck(A.state==LINK_DONE && B.state==LINK_DONE, "the fight ends");
    uint8_t hadA = A.theirsN, hadB = B.theirsN;
    A.sendRematch();
    run(200);
    ck(A.state==LINK_READY && B.state==LINK_READY, "a rematch reopens both sides");
    ck(A.turn==0 && B.turn==0, "from turn zero");
    ck(A.theirsN==hadA && B.theirsN==hadB, "with the squads still in hand");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
