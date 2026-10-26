// After a gym battle, saves must still reach NVS.
//
// buildSquad() used to take the live pet into the fight as
//
//     Pet tmp = pet;      // a copy: the real pet is untouched
//
// Pet holds a Preferences, and Arduino's Preferences is a bare
// { _handle, _started } pair with the implicit copy constructor and a
// destructor that nvs_close()s. So the copy SHARED the live pet's NVS handle,
// and when it went out of scope it CLOSED it. Every pet.save() after that --
// the badge just won, the training reward, everything until the next reboot --
// failed quietly against a closed handle. On the board: win a gym, let the
// battery die or reset, and come back to the pre-battle state. That is the
// "after an arena fight the screen goes black and the progress is lost" report.
//
// The emulator's Preferences ignored begin()/end(), so it could not see this.
// It models handles now (tools/emu/Preferences.h), and this test drives the
// real gym path, then reads NVS through a FRESH Pet the way a reboot would.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "moves.h"
#include "battle.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed=4242; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop();
void battleTap(int16_t,int16_t);
void startTrainerBattle(uint8_t idx, bool hard);
extern Pet pet;
extern bool battleOpen, btlOver, btlWon, btlPetIn;
extern uint8_t btlMsgCount;
extern uint32_t btlWinUntil;
extern Combatant btlYou;
extern uint16_t squadMask;
#define BTL_CELL_X(i) (69 + ((i) % 2) * 168)
#define BTL_CELL_Y(i) (274 + ((i) / 2) * 52)
extern uint8_t btlMenu;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  setup();
  for (int i = 0; i < 3; i++) loop();
  pet.dbgHatchAs(6, false);                  // CHARIZARD
  pet.ageMinutes = 100UL * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.saveNow();

  // The precondition, so the check below cannot pass for the wrong reason:
  // before any battle a save reaches NVS.
  pet.rename("BEFORE");
  { Pet q; q.begin(); ck(!strcmp(q.nick, "BEFORE"), "before a battle, a save reaches NVS"); }

  bool had = pet.hasBadge(0, 0, false);
  squadMask = 0xFFFF;
  startTrainerBattle(0, false);                // BROCK
  ck(battleOpen, "the gym battle starts");
  ck(btlPetIn, "with the LIVE pet in the squad (the path that copied it)");

  // fight it out through the real tap handler
  int guard = 0;
  while (battleOpen && guard++ < 300) {
    if (btlWinUntil || btlMsgCount) { battleTap(233, 320); continue; }
    int slot = -1;
    for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
    if (slot < 0) break;
    if (btlMenu == 0) battleTap(233, BTL_CELL_Y(0) + 20);   // FIGHT
    battleTap(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
  }
  ck(!battleOpen && btlWon, "a L100 creature beats BROCK and leaves the fight");
  ck(!had && pet.hasBadge(0, 0, false), "the badge is awarded in RAM");

  // reboot: what does the board come up with?
  {
    Pet q; q.begin();
    ck(q.hasBadge(0, 0, false), "the badge just won SURVIVES a reboot");
    // the win's training reward is deferred inside Pet and written by the
    // badge's own save: it must be on flash just the same
    ck(q.trAtk == pet.trAtk && q.trDef == pet.trDef && q.trSpe == pet.trSpe,
       "and so does the training reward the win paid");
  }
  // a REMATCH: no badge to carry the reward, so the battle flushes it itself
  {
    uint8_t before = pet.trAtk + pet.trDef + pet.trSpe;
    startTrainerBattle(0, false);
    int g2 = 0;
    while (battleOpen && g2++ < 300) {
      if (btlWinUntil || btlMsgCount) { battleTap(233, 320); continue; }
      int slot = -1;
      for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
      if (slot < 0) break;
      if (btlMenu == 0) battleTap(233, BTL_CELL_Y(0) + 20);
      battleTap(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
    }
    ck(btlWon && pet.trAtk + pet.trDef + pet.trSpe > before, "a rematch win pays training");
    Pet q; q.begin();
    ck(q.trAtk == pet.trAtk && q.trDef == pet.trDef && q.trSpe == pet.trSpe,
       "and a rematch's reward survives a reboot too, with no badge to carry it");
  }
  pet.rename("AFTER");
  {
    Pet q; q.begin();
    ck(!strcmp(q.nick, "AFTER"), "a save made AFTER the battle still reaches NVS");
  }
  // and the copy never touched the stored creature's level
  { Pet q; q.begin(); ck(q.level() == 100, "the stored creature keeps its own level"); }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
