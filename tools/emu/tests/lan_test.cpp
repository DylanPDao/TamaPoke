// The LAN battle as the sketch actually wires it: what squad you fight with,
// what the guest is allowed to do, and whether a result off the wire lands on
// the right creature. The protocol itself is link_test's job; this is the half
// that lives in TamaPoke.ino and that link_test cannot reach.
//
// The radio is still untested -- it needs two boards. Everything on either side
// of it is checked here.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include "link.h"
#include <cstdio>
uint32_t g_seed=5; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}

void setup(); void render(); void battleTap(int16_t,int16_t);
extern Pet pet;
extern Link lan;
extern bool battleOpen, btlLink, btlLinkHost, btlOver, btlWon, lanOpen;
extern Combatant btlYou, btlFoe;
extern Combatant btlSquad[];
extern uint8_t btlSquadN, btlSquadAt, btlFoeAt, btlMenu, btlMsgCount;
extern uint8_t btlMyAct, btlFoeSquadN;
extern Combatant btlFoeSquad[];
extern uint16_t squadMask;
void startLinkBattle();

// The sketch never sets a transport itself -- linkNowBegin does, and there is
// no radio here -- so one is wired in to see what the UI actually sends.
static int sent=0; static uint8_t lastPkt[64]; static uint8_t lastLen=0;
static void capture(void*, const uint8_t*b, uint8_t n){
  sent++; lastLen = n < sizeof(lastPkt) ? n : sizeof(lastPkt); memcpy(lastPkt,b,lastLen);
}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

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
  setup();
  for (int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  // a party that is deliberately NOT the squad we advertise
  for (int i=0;i<PARTY_SLOTS;i++){ PartyMon m; m.dex=20+i; m.level=30;
    m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20; party.replaceAt(i,m); }
  squadMask = 0xFFFF;

  lan.begin(true,"HOST");
  snprintf(lan.peerName,sizeof(lan.peerName),"%s","RIVAL");
  lan.addMon(mon(9,50,"SHELL"));
  lan.addMon(mon(143,50,"SNORE"));
  lan.theirs[0]=mon(6,50,"BLAZE");
  lan.theirs[1]=mon(65,50,"SPOON");
  lan.theirsN=2;
  lan.state=LINK_READY;
  lan.send=capture;

  startLinkBattle();
  ck(battleOpen && btlLink && btlLinkHost, "the host starts a linked battle");
  // The party above is full of level-30 nobodies and the live pet is level 1.
  // If the squad were rebuilt from squadMask we would fight with those instead
  // of what the peer was told we have -- which is exactly the desync to avoid.
  ck(btlSquadN==2, "the squad is the advertised one, not the current party");
  ck(btlYou.dex==9 && btlYou.level==50, "and leads with what we sent");
  ck(!strcmp(btlSquad[1].name,"SNORE"), "including the rest of it");
  ck(btlFoe.dex==6 && btlFoe.hp==btlFoe.maxHp, "the rival leads with theirs, at full health");

  // --- the peer's team is LIVE, not rebuilt each time it is looked at
  ck(btlFoeSquadN==2, "the peer's whole team is held as combatants");
  btlFoeSquad[1].hp = 7;
  ck(btlFoeSquad[1].hp==7 && btlFoeSquad[1].maxHp>7,
     "so a creature that switches out can stay hurt");

  // --- the guest ASKS to switch; it never switches on its own, because the
  // host is the only side that may spend a turn
  btlLinkHost = false;
  lan.state = LINK_READY;
  btlMenu = 2;                       // the POKEMON list
  uint8_t was = btlSquadAt;
  sent = 0;
  battleTap(69 + 168 + 10, 286 + 10);   // cell 1 of the switch grid
  ck(btlSquadAt==was, "the guest does not switch locally");
  ck(sent==1 && lastPkt[0]==LM_ACT, "it sends the request instead");
  ck(LINK_ACT_IS_SWITCH(lastPkt[3]) && LINK_ACT_SLOT(lastPkt[3])==1,
     "and the request names the slot it wants");

  // --- the host LATCHES its own action instead of throwing it away
  btlLinkHost = true;
  btlLink = true;
  btlOver = false;
  btlMsgCount = 0;
  lan.pendingAct = 0;
  btlMyAct = 0;
  btlMenu = 1;                       // the move grid
  uint16_t hpWas = btlFoe.hp;
  battleTap(69 + 10, 286 + 10);      // move slot 0
  ck(btlMyAct != 0, "the host latches its move while the rival is still choosing");
  ck(btlFoe.hp == hpWas, "and nothing is resolved yet");
  lan.pendingAct = LINK_ACT_MOVE(0);
  render();                          // btlLinkPoll spots that both are in
  ck(btlMyAct == 0 && !lan.pendingAct,
     "and the turn goes as soon as the rival's action lands");

  // --- a peer that goes quiet ends the fight rather than hanging on it
  btlOver = false; btlMsgCount = 0;
  lan.state = LINK_LOST;
  render();
  ck(btlOver, "a lost peer ends the battle instead of waiting forever");
  lan.state = LINK_READY;
  btlOver = false; btlMsgCount = 0;

  // --- a result off the wire moves the guest's battle, and only once
  btlLink = true; btlLinkHost = false; btlMenu = 0; btlMsgCount = 0;
  btlYou = btlSquad[btlSquadAt];
  LinkResult r{};
  r.hostHp = btlFoe.maxHp/2; r.guestHp = btlYou.maxHp/4;
  r.hostMove = 1; r.guestMove = 1; r.hostDmg = 5; r.guestDmg = 7;
  r.hostIdx = 0; r.guestIdx = 0;
  r.guestAil = AIL_BURN;
  memcpy(lan.result,&r,sizeof(r)); lan.resultN=sizeof(r); lan.resultNew=true;
  render();
  ck(btlYou.hp==r.guestHp && btlFoe.hp==r.hostHp, "the guest takes the host's numbers");
  ck(btlYou.ailment==AIL_BURN, "and the ailment it was handed");
  ck(!lan.resultNew, "a result is consumed once");
  uint16_t hpNow = btlYou.hp;
  render();
  ck(btlYou.hp==hpNow, "so a second frame does not replay the turn");

  // --- the rival's next creature arrives when the host says so
  r.hostIdx = 1; r.hostHp = 60;
  memcpy(lan.result,&r,sizeof(r)); lan.resultNew=true;
  render();
  ck(btlFoeAt==1 && btlFoe.dex==65, "the rival's next creature comes off the wire");

  // --- and the guest never decides the ending
  lan.youWon = true; lan.state = LINK_DONE;
  render();
  ck(btlOver && btlWon, "the guest takes the ending from the host");

  // a short or empty result must be dropped, not read past the end
  btlOver=false; lan.state=LINK_READY;
  uint16_t before = btlYou.hp;
  lan.resultN = 3; lan.resultNew = true;
  render();
  ck(!lan.resultNew && btlYou.hp==before, "a runt result is dropped, not half-applied");

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
