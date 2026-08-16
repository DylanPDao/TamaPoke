// The peer-to-peer protocol, with two Links cross-wired in one process. The
// radio cannot be tested here, but the handshake, the version refusal, the
// squad exchange and the turn flow all can -- and those are where a desync
// would come from.
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
static int dropped = 0;
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

int main(){
  A.send=toB; B.send=toA;
  A.begin(true,"HOST"); B.begin(false,"GUEST");
  A.addMon(mon(6,50,"BLAZE")); A.addMon(mon(9,50,"SHELL"));
  B.addMon(mon(65,50,"SPOON"));
  A.start();
  ck(A.state==LINK_READY && B.state==LINK_READY, "both reach READY from one hello");
  ck(B.theirsN==2 && A.theirsN==1, "each ends up with the other's squad");
  ck(B.theirs[0].dex==6 && A.theirs[0].dex==65, "and the right creatures in it");
  ck(!strcmp(B.theirs[1].name,"SHELL"), "names survive the wire");

  // a creature restored from the wire must fight identically
  Combatant back; linkMonTo(back, B.theirs[0]);
  ck(back.dex==6 && back.level==50 && back.hp==back.maxHp,
     "a wire creature restores at full health");
  ck(back.base[SI_ATK]==A.mine[0].base[SI_ATK], "with its stats intact");

  // the guest sends a move; only the host may act on it
  B.sendMove(2);
  ck(A.pendingMove==3, "the host receives the guest's move (+1 encoded)");
  ck(B.state==LINK_WAITING, "and the guest waits rather than resolving");
  uint8_t blob[4]={1,2,3,4};
  A.sendResult(blob,4);
  ck(B.state==LINK_READY, "a result returns the guest to choosing");
  ck(B.resultNew && B.resultN==4 && B.result[0]==1, "and carries the payload");

  // the real payload: what the guest actually renders a turn from
  ck(sizeof(LinkResult) <= LINK_MAX_PAYLOAD, "a result fits in one packet");
  LinkResult r{}; 
  r.hostHp=111; r.guestHp=222; r.hostMove=33; r.guestMove=44;
  r.hostDmg=9; r.guestDmg=8; r.hostIdx=1; r.guestIdx=0; r.guestAil=AIL_BURN;
  B.resultNew=false;
  A.sendResult((const uint8_t*)&r,(uint8_t)sizeof(r));
  LinkResult got{}; memcpy(&got,B.result,sizeof(got));
  ck(B.resultNew && B.resultN==sizeof(r), "a full result arrives whole");
  ck(got.hostHp==111 && got.guestHp==222 && got.guestAil==AIL_BURN,
     "health and ailments survive the wire");
  ck(got.hostIdx==1 && got.guestIdx==0, "so does which creature is out");

  // the guest never resolves anything: a result it did not ask for is still
  // applied, but a MOVE aimed at it is not -- only the host acts on moves
  B.pendingMove=0;
  B.onPacket((const uint8_t[]){LM_MOVE,1,2},3);
  ck(B.pendingMove==0, "the guest ignores MOVE packets");

  // the host's own results are never accepted back
  A.state=LINK_READY; A.onPacket((const uint8_t[]){LM_RESULT,0},2);
  ck(A.state==LINK_READY, "the host ignores RESULT packets");

  // and the ending agrees from both sides
  A.sendEnd(true);
  ck(A.state==LINK_DONE && B.state==LINK_DONE, "both see the end");
  ck(A.youWon && !B.youWon, "and agree on who won");

  // --- a protocol mismatch must REFUSE, never proceed
  Link C, D; C.send=[](void*,const uint8_t*b,uint8_t n){}; D.send=nullptr;
  C.begin(true,"H"); 
  uint8_t badHello[2+1+LINK_NAME_LEN]={LM_HELLO,(uint8_t)(1+LINK_NAME_LEN),(uint8_t)(LINK_PROTO+7)};
  C.onPacket(badHello,sizeof(badHello));
  ck(C.state==LINK_REFUSED, "a version mismatch is refused, not tolerated");
  // and a refused link accepts nothing further
  uint8_t sq[2+1+sizeof(LinkMon)]={LM_SQUAD,(uint8_t)(1+sizeof(LinkMon)),0};
  C.onPacket(sq,sizeof(sq));
  ck(C.theirsN==0 && C.state==LINK_REFUSED, "and stays refused");

  // malformed frames must be dropped, not half-parsed
  Link E; E.send=nullptr; E.begin(true,"E");
  uint8_t runt[1]={LM_HELLO};
  E.onPacket(runt,1);
  uint8_t lying[3]={LM_SQUAD,200,0};        // claims 200 bytes, carries 1
  E.onPacket(lying,3);
  ck(E.theirsN==0, "a truncated or lying frame is dropped");
  uint8_t junk[3]={99,1,0};
  E.onPacket(junk,3);
  ck(E.state==LINK_LISTENING, "an unknown type is ignored without desyncing");

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
