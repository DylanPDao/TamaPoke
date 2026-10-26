// A gym fight outside Kanto is fought against, and awards, its OWN region.
//
// btlRegion -- which ladder the running fight belongs to -- was declared and
// never assigned, so it stayed 0. startTrainerBattle() read the first foe
// through TRAINERS (gymRegion), but every later foe, the badge and the win
// screen read BTL_TRAINERS (btlRegion). Fighting Falkner opened with his first
// creature, then sent out Brock's second, and the win set a KANTO badge -- so
// gymUnlocked() never opened Johto's second leader. Every existing test fought
// region 0, the one value where the two happened to agree.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "moves.h"
#include "battle.h"
#include "trainers.h"
#include <cstdio>
uint32_t g_seed=99; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop();
void battleTap(int16_t,int16_t);
void startTrainerBattle(uint8_t idx, bool hard);
extern Pet pet;
extern bool battleOpen, btlWon;
extern uint8_t btlMsgCount, btlMenu, btlFoeAt, gymRegion;
extern uint32_t btlWinUntil;
extern Combatant btlYou, btlFoe;
extern uint16_t squadMask;
#define BTL_CELL_X(i) (69 + ((i) % 2) * 168)
#define BTL_CELL_Y(i) (274 + ((i) / 2) * 52)

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  setup();
  for (int i = 0; i < 3; i++) loop();
  pet.dbgHatchAs(6, false);
  pet.ageMinutes = 100UL * MINUTES_PER_LEVEL;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();

  const uint8_t R = 1;                          // Johto
  const Trainer &lead = TRAINER_SETS[R].list[0];
  const Trainer &kanto = TRAINER_SETS[0].list[0];
  ck(lead.count >= 2, "the region's first leader brings a second creature to check");
  // the check below can only tell the regions apart if the rosters differ there
  ck(lead.team[1].dex != kanto.team[1].dex, "and it differs from Kanto's at that slot");

  squadMask = 0xFFFF;
  gymRegion = R;
  startTrainerBattle(0, false);
  ck(battleOpen, "the Johto gym fight starts");
  ck(btlFoe.dex == lead.team[0].dex, "the first foe is the Johto leader's lead");

  int16_t second = 0;
  int guard = 0;
  while (battleOpen && guard++ < 300) {
    if (btlFoeAt == 1 && !second) second = btlFoe.dex;
    if (btlWinUntil || btlMsgCount) { battleTap(233, 320); continue; }
    int slot = -1;
    for (int i = 0; i < MOVE_SLOTS; i++) if (btlYou.moves[i]) { slot = i; break; }
    if (slot < 0) break;
    if (btlMenu == 0) battleTap(233, BTL_CELL_Y(0) + 20);
    battleTap(BTL_CELL_X(slot) + 40, BTL_CELL_Y(slot) + 20);
  }
  printf("     second foe dex=%d (Johto wants %d, Kanto has %d)\n",
         second, lead.team[1].dex, kanto.team[1].dex);
  ck(second == lead.team[1].dex, "the SECOND foe is also the Johto leader's, not Brock's");
  ck(btlWon, "the fight is won");
  ck(pet.hasBadge(R, 0, false), "the badge lands in JOHTO");
  ck(!pet.hasBadge(0, 0, false), "and not in Kanto");
  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
