// How many times an action writes the save, and whether what must survive a
// power cut does.
//
// Pet::save() is ~52 NVS keys, and on the board a flash write stalls BOTH cores
// for up to a second. It used to run synchronously on almost every action, and
// several times on some: the first care of the day was registerCare()'s save,
// then checkMedals()'s if a medal landed, then the caller's own; startRetire()
// saved three times. Care is now deferred to the flush loop() does when the
// panel dims (pendingSave), and only what the player would mourn writes on the
// spot. This counts Pet::saveCount around each action and, for every immediate
// one, reads NVS back through a FRESH Pet -- a reboot -- without any flush.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=77; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
static uint32_t g_ms=1000; uint32_t millis(){return g_ms;}
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static Pet *P;
static uint32_t mark;
static void start(){ mark = P->saveCount; }
static uint32_t saves(){ return P->saveCount - mark; }

static void fresh(Pet &p, int16_t dex){
  if (p.awaitingStarter()) p.chooseStarter(4);
  p.dbgHatchAs(dex, false);
  p.sleeping = false;
  p.fullness = p.joy = p.energy = p.hygiene = 80;
  p.flushSave();
}

int main(){
  Pet p; p.begin(); P = &p;
  fresh(p, 4);                              // CHARMANDER

  // --- care: deferred --------------------------------------------------------
  // A NEW DAY, so feed() takes the full registerCare() path (streak, bond and,
  // at streak 7, a medal) -- the worst case the old code saved three times for.
  p.streak = 6; p.lastCareDay = 20000; p.lastSeenEpoch = 20001UL * 86400 + 3600;
  p.medals = 0;
  start();
  p.feedBerry(0);
  printf("     feed on a new day, medal included: %u saves\n", saves());
  ck(saves() == 0, "the first feed of the day, medal and all, writes nothing on the spot");
  ck(p.streak == 7 && p.hasMedal(MED_STREAK7), "but it did count the day and award the medal");
  ck(p.savePending(), "and left the save pending for loop() to flush");
  start();
  p.clean(); p.play(); p.feedCandy(); p.caress();
  p.playResult(12); p.trainStrength(30); p.trainSpeed(20);
  p.toggleLight(); p.toggleLight();
  p.setScreenOff(true); p.setScreenOff(false);
  ck(saves() == 0, "clean, play, candy, caress, three minigame results, the light "
                   "and the screen write nothing on the spot");
  start();
  p.flushSave();
  ck(saves() == 1, "and ONE flush writes all of it");
  { Pet q; q.begin();
    ck(q.streak == 7 && q.hasMedal(MED_STREAK7) && q.hygiene == 100,
       "which a reboot then reads back"); }
  start();
  p.flushSave();
  ck(saves() == 0, "a flush with nothing pending writes nothing");

  // --- immediate: one write each, and it survives a reboot unflushed ---------
  start(); p.rename("KEEP");
  ck(saves() == 1, "rename: one save");
  { Pet q; q.begin(); ck(!strcmp(q.nick, "KEEP"), "rename survives a reboot"); }

  start(); p.renameTrainer("ASH");
  ck(saves() == 1, "trainer name: one save");
  { Pet q; q.begin(); ck(!strcmp(q.trainerName, "ASH"), "the trainer name survives a reboot"); }

  // A gym win does winBadge() then rewardTraining(): the badge writes at once,
  // the training reward rides on the next save.
  p.trAtk = p.trDef = p.trSpe = 0;
  start();
  p.winBadge(0, 0, false);
  uint8_t which = 0; uint8_t got = p.rewardTraining(5, which);
  printf("     gym win (badge + training): %u saves\n", saves());
  ck(saves() == 1, "a gym win: one save, the badge's");
  { Pet q; q.begin(); ck(q.hasBadge(0, 0, false), "the badge survives a reboot with no flush"); }
  ck(got > 0 && p.savePending(), "the training reward is pending, not lost");
  p.flushSave();

  // evolution
  p.ageMinutes = 20UL * MINUTES_PER_LEVEL; p.careMistakes = 0;
  p.fullness = p.joy = p.energy = p.hygiene = 90;
  while (p.hasLearnOffer()) p.declineLearn();
  ck(p.canEvolveNow(), "precondition: CHARMANDER can evolve");
  start(); p.evolve();
  ck(saves() == 1, "evolve: one save");
  { Pet q; q.begin(); ck(q.speciesId == 5, "the evolution survives a reboot"); }

  // learning a move is the player's decision: immediate
  while (p.hasLearnOffer()) { start(); p.declineLearn(); ck(saves() == 1, "declining a move: one save"); }

  // an early retire: was three saves, and lastEnd must be the FINAL value
  start(); p.startRetire();
  printf("     early retire: %u saves\n", saves());
  ck(saves() == 1, "an early retire: ONE save (it was three)");
  { Pet q; q.begin(); ck(q.lastEnd == CER_RELEASE, "and it stores the corrected ending, not FAREWELL"); }

  // the ceremony ends -> newEgg(): one save
  g_ms += CEREMONY_MS + 10;
  start(); p.update(g_ms);
  ck(p.isEgg() && saves() == 1, "the ceremony ending in a new egg: one save");

  // a crack is deferred, the hatch is not
  start(); p.eggTap(); p.eggTap();
  ck(saves() == 0, "the first two cracks write nothing");
  p.eggTap();
  ck(!p.isEgg() && saves() == 1, "the hatching crack: one save (its medal check included)");
  { Pet q; q.begin(); ck(!q.isEgg() && q.speciesId == p.speciesId, "the hatch survives a reboot"); }

  // the other two endings
  fresh(p, 25);
  start(); p.release();
  ck(saves() == 1, "release: one save");
  { Pet q; q.begin(); ck(q.lastEnd == CER_RELEASE, "release survives a reboot"); }
  g_ms += CEREMONY_MS + 10; p.update(g_ms); p.eggTap(); p.eggTap(); p.eggTap();
  p.sleeping = false;
  start(); p.startRunaway();
  ck(saves() == 1, "runaway: one save");
  g_ms += CEREMONY_MS + 10; p.update(g_ms); p.eggTap(); p.eggTap(); p.eggTap();
  p.sleeping = false;
  start(); p.startFarewell();
  ck(saves() == 1, "farewell: one save");
  { Pet q; q.begin(); ck(q.lastEnd == CER_FAREWELL, "the farewell survives a reboot"); }
  g_ms += CEREMONY_MS + 10; p.update(g_ms); p.eggTap(); p.eggTap(); p.eggTap();

  // revive
  PartyMon m; m.dex = 6; m.level = 40; m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
  start(); p.reviveFrom(m);
  ck(saves() == 1, "revive: one save");
  { Pet q; q.begin(); ck(q.speciesId == 6 && q.frozen, "the revive survives a reboot"); }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
