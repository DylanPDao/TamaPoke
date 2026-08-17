// The peer-to-peer protocol, with two Links cross-wired in one process. The
// radio cannot be tested here, but the handshake, the version refusal, the
// squad exchange, the turn flow and every piece of validation can -- and those
// are where a desync would come from. lossy_test covers the same protocol over
// a channel that drops and duplicates.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "link.h"
#include <cstdio>
uint32_t g_seed=17; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static Link A, B;
// each Link's transport hands the bytes straight to the other
static void toB(void*, const uint8_t*b, uint8_t n){ B.onPacket(b,n); }
static void toA(void*, const uint8_t*b, uint8_t n){ A.onPacket(b,n); }

static LinkMon mon(int16_t dex, uint8_t lvl, const char *nm){
  Pet p; p.dbgHatchAs(dex,false);
  p.ivAtk=p.ivDef=p.ivSpe=p.ivHp=25;
  p.ageMinutes=(uint32_t)(lvl-1)*MINUTES_PER_LEVEL;
  p.relearnFromLevel();
  Combatant c; combatantFromPet(c,p);
  snprintf(c.name,sizeof(c.name),"%s",nm);
  LinkMon m; linkMonFrom(m,c); return m;
}

// a well-formed hello, so a test can vary exactly one field of it
static void hello(Link &to, uint8_t proto, bool host, uint8_t count,
                  uint16_t id, uint16_t build){
  uint8_t p[2+7+LINK_NAME_LEN];
  memset(p,0,sizeof(p));
  p[0]=LM_HELLO; p[1]=7+LINK_NAME_LEN;
  p[2]=proto; p[3]=host?1:0; p[4]=count;
  p[5]=(uint8_t)(id&0xFF); p[6]=(uint8_t)(id>>8);
  p[7]=(uint8_t)(build&0xFF); p[8]=(uint8_t)(build>>8);
  to.onPacket(p,sizeof(p));
}

int main(){
  A.send=toB; B.send=toA;
  A.begin(true,"HOST"); B.begin(false,"GUEST");
  A.id=0x1111; B.id=0x2222;
  A.addMon(mon(6,50,"BLAZE")); A.addMon(mon(9,50,"SHELL"));
  B.addMon(mon(65,50,"SPOON"));
  A.start();
  ck(A.state==LINK_READY && B.state==LINK_READY, "both reach READY from one hello");
  ck(B.theirsN==2 && A.theirsN==1, "each ends up with the other's squad");
  ck(B.theirs[0].dex==6 && A.theirs[0].dex==65, "and the right creatures in it");
  ck(!strcmp(B.theirs[1].name,"SHELL"), "names survive the wire");
  ck(A.isHost && !B.isHost, "the roles stay as they were offered");

  // a creature restored from the wire must fight identically
  Combatant back; linkMonTo(back, B.theirs[0]);
  ck(back.dex==6 && back.level==50 && back.hp==back.maxHp,
     "a wire creature restores at full health");
  ck(back.base[SI_ATK]==A.mine[0].base[SI_ATK], "with its stats intact");

  // --- the turn flow
  ck(A.turn==0 && B.turn==0, "both start on turn zero");
  B.sendAct(LINK_ACT_MOVE(0));
  ck(A.hasPeerAct(), "the host receives the guest's action");
  ck(LINK_ACT_SLOT(A.pendingAct)==0 && !LINK_ACT_IS_SWITCH(A.pendingAct),
     "move slot 0 survives -- it is not confused with silence");
  ck(B.state==LINK_WAITING, "and the guest waits rather than resolving");

  A.pendingAct = 0;
  B.state = LINK_READY;
  B.sendAct(LINK_ACT_SWITCH_TO(3));
  ck(LINK_ACT_IS_SWITCH(A.pendingAct) && LINK_ACT_SLOT(A.pendingAct)==3,
     "a switch is carried in the same message as a move");

  uint8_t blob[4]={1,2,3,4};
  A.sendResult(blob,4);
  ck(B.state==LINK_READY, "a result returns the guest to choosing");
  ck(B.resultNew && B.resultN==4 && B.result[0]==1, "and carries the payload");
  ck(A.turn==1 && B.turn==1, "and both advance exactly one turn");

  // a stale action -- the resend of a turn already resolved -- is not a choice
  A.pendingAct = 0;
  uint8_t stale[4]={LM_ACT,2,0,LINK_ACT_MOVE(1)};   // turn 0, already done
  A.onPacket(stale,4);
  ck(!A.hasPeerAct(), "a resent action from a finished turn is ignored");

  // the real payload: what the guest actually renders a turn from
  ck(sizeof(LinkResult) <= LINK_MAX_PAYLOAD, "a result fits in one packet");
  LinkResult r{};
  r.hostHp=111; r.guestHp=222; r.hostMove=33; r.guestMove=44;
  r.hostDmg=9; r.guestDmg=8; r.hostIdx=1; r.guestIdx=0; r.guestAil=AIL_BURN;
  B.resultNew=false;
  A.pendingAct = LINK_ACT_MOVE(0);
  A.sendResult((const uint8_t*)&r,(uint8_t)sizeof(r));
  LinkResult got{}; memcpy(&got,B.result,sizeof(got));
  ck(B.resultNew && B.resultN==sizeof(r), "a full result arrives whole");
  ck(got.hostHp==111 && got.guestHp==222 && got.guestAil==AIL_BURN,
     "health and ailments survive the wire");
  ck(got.hostIdx==1 && got.guestIdx==0, "so does which creature is out");
  ck(!A.pendingAct, "resolving clears the action, so it cannot be spent twice");

  // the guest never acts on an action; the host never accepts a result
  B.pendingAct=0;
  B.onPacket((const uint8_t[]){LM_ACT,2,0,1},4);
  ck(B.pendingAct==0, "the guest ignores ACT packets");
  uint8_t was=A.turn;
  A.onPacket((const uint8_t[]){LM_RESULT,1,0},3);
  ck(A.turn==was, "the host ignores RESULT packets");

  // and the ending agrees from both sides
  A.sendEnd(true);
  ck(A.state==LINK_DONE && B.state==LINK_DONE, "both see the end");
  ck(A.youWon && !B.youWon, "and agree on who won");

  // --- roles: two hosts, or two guests, must resolve to exactly one host
  {
    Link C,D; static Link *pC=&C,*pD=&D;
    C.send=[](void*,const uint8_t*b,uint8_t n){ pD->onPacket(b,n); };
    D.send=[](void*,const uint8_t*b,uint8_t n){ pC->onPacket(b,n); };
    C.begin(true,"C"); D.begin(true,"D");        // BOTH offered to host
    C.id=10; D.id=99;
    C.addMon(mon(6,50,"C1")); D.addMon(mon(9,50,"D1"));
    C.start(); D.start();
    ck(C.isHost != D.isHost, "two hosts settle on exactly one");
    ck(D.isHost, "and it is the higher id");
  }
  {
    Link C,D; static Link *pC=&C,*pD=&D;
    C.send=[](void*,const uint8_t*b,uint8_t n){ pD->onPacket(b,n); };
    D.send=[](void*,const uint8_t*b,uint8_t n){ pC->onPacket(b,n); };
    C.begin(false,"C"); D.begin(false,"D");      // BOTH only wanted to join
    C.id=10; D.id=99;
    C.addMon(mon(6,50,"C1")); D.addMon(mon(9,50,"D1"));
    C.start();
    ck(C.isHost != D.isHost, "two guests settle on exactly one host too");
  }
  {
    // identical ids cannot be told apart, so it is refused rather than guessed
    Link C; C.send=[](void*,const uint8_t*,uint8_t){};
    C.begin(true,"C"); C.id=0x4242;
    hello(C, LINK_PROTO, true, 1, 0x4242, linkBuildTag());
    ck(C.state==LINK_REFUSED, "two devices with the same id refuse rather than guess");
  }

  // --- compatibility
  {
    Link C; C.send=[](void*,const uint8_t*,uint8_t){};
    C.begin(true,"C"); C.id=1;
    hello(C, LINK_PROTO+7, false, 1, 2, linkBuildTag());
    ck(C.state==LINK_REFUSED, "a protocol mismatch is refused, not tolerated");
    hello(C, LINK_PROTO, false, 1, 2, linkBuildTag());
    ck(C.state==LINK_REFUSED, "and a refused link stays refused");
  }
  {
    // same protocol, different tables: a move index would name a different
    // move on each screen, so this must refuse as loudly as a version mismatch
    Link C; C.send=[](void*,const uint8_t*,uint8_t){};
    C.begin(true,"C"); C.id=1;
    hello(C, LINK_PROTO, false, 1, 2, (uint16_t)(linkBuildTag()^0xBEEF));
    ck(C.state==LINK_REFUSED, "a different build's tables are refused");
  }
  ck(linkBuildTag()==linkBuildTag(), "the build tag is stable within a build");

  // --- malformed frames must be dropped, not half-parsed
  {
    Link E; E.send=nullptr; E.begin(true,"E");
    E.onPacket((const uint8_t[]){LM_HELLO},1);
    E.onPacket((const uint8_t[]){LM_SQUAD,200,0},3);   // claims 200, carries 1
    ck(E.theirsN==0, "a truncated or lying frame is dropped");
    E.onPacket((const uint8_t[]){99,1,0},3);
    ck(E.state==LINK_LISTENING, "an unknown type is ignored without desyncing");
    E.onPacket((const uint8_t[]){LM_HELLO,3,LINK_PROTO,0,1},5);   // short hello
    ck(E.state==LINK_LISTENING, "and a short hello is not half-read");
  }

  // --- NOTHING off the wire may be trusted to index a table
  {
    LinkMon junk{};
    junk.dex=9999; junk.level=250; junk.maxHp=0; junk.moves[0]=250;
    Combatant c; linkMonTo(c,junk);
    ck(c.dex>=1 && c.dex<=DEX_COUNT, "a nonsense dex is clamped into the table");
    ck(c.level>=1 && c.level<=MAX_LEVEL, "so is a nonsense level");
    ck(c.moves[0]<MOVE_COUNT, "and a nonsense move index");
    ck(c.maxHp>=1 && c.hp==c.maxHp, "a creature always has at least 1 HP");

    LinkMon neg{}; neg.dex=-5; neg.level=0; neg.maxHp=10;
    linkMonTo(c,neg);
    ck(c.dex>=1 && c.level>=1, "including a negative dex and a level of zero");

    // a name off the wire need not be terminated; reading it as a C string
    // would run off the end of the struct
    LinkMon nm{}; nm.dex=6; nm.level=5; nm.maxHp=10;
    for (int i=0;i<LINK_NAME_LEN;i++) nm.name[i]='X';
    linkMonTo(c,nm);
    ck(strlen(c.name)<sizeof(c.name), "an unterminated wire name is terminated");
    ck(linkSafeMove(255)==0 && linkSafeMove(1)==1, "linkSafeMove bounds a move index");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
