// Hoopa + Halloween: the decoration window, the one-time unlock, the owed egg,
// the form switch and what it changes. Drives the real Pet.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "battle.h"
#include <cstdio>
#include <ctime>
uint32_t g_seed=23; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// an epoch for a calendar date (the RTC is set in local time, read as UTC)
static uint32_t at(int y,int m,int d,int h=12){
  struct tm t={}; t.tm_year=y-1900; t.tm_mon=m-1; t.tm_mday=d; t.tm_hour=h;
  return (uint32_t)timegm(&t);
}

int main(){
  // --- the decoration window, every year
  {
    Pet p; p.begin();
    p.hoopaUnlocked = true;   // this block is about the decoration only
    p.checkCalendar(at(2026,10,23)); ck(!p.halloween, "Oct 23: no pumpkins yet");
    p.checkCalendar(at(2026,10,24)); ck(p.halloween, "Oct 24: the window opens");
    p.checkCalendar(at(2026,11,7));  ck(p.halloween, "Nov 7: still on");
    p.checkCalendar(at(2026,11,8));  ck(!p.halloween, "Nov 8: packed away");
    p.checkCalendar(at(2027,10,31)); ck(p.halloween, "and it comes back next year");
    p.checkCalendar(0);              ck(p.halloween, "an unset clock changes nothing");
  }

  // --- the unlock: not before Halloween 2026, then forever
  {
    Pet p; p.begin(); p.setKidsMode(true);
    p.hoopaUnlocked = false; p.hoopaPending = false;
    p.newEgg();
    if (p.awaitingStarter()) p.chooseStarter(4);
    p.checkCalendar(at(2026,10,30,23));
    ck(!p.hoopaUnlocked, "Oct 30 2026: nothing happens");
    ck(!p.eggIsHoopa(), "the egg is an ordinary egg");
    p.checkCalendar(at(2026,10,31,0));
    ck(p.hoopaUnlocked, "Oct 31 2026 00:00: the ring opens");
    ck(p.hoopaNews, "and the UI is told once");
    ck(p.eggIsHoopa(), "the waiting egg is the HOOPA egg on the spot");
    ck(!p.hoopaPending, "so nothing is owed");
    p.checkCalendar(at(2026,12,25));
    ck(p.hoopaUnlocked, "unlocked stays unlocked");
    // the region pill cannot swap it away
    p.setRegion(0);
    ck(p.eggIsHoopa(), "switching the egg's region keeps HOOPA");
    p.eggTap(); p.eggTap(); p.eggTap();   // three taps hatch it, like a finger would
    ck(p.isHoopa(), "and it hatches a HOOPA");
    ck(p.form == FORM_CONFINED, "confined at first");
  }

  // --- a live creature is never displaced: the NEXT egg is HOOPA
  {
    Pet p; p.begin(); p.setKidsMode(true);
    p.dbgHatchAs(25,false);
    p.hoopaUnlocked = false; p.hoopaPending = false;
    p.checkCalendar(at(2026,10,31,9));
    ck(p.hoopaUnlocked && p.hoopaPending, "with PIKACHU live, a HOOPA egg is owed");
    ck(p.speciesId == 25, "PIKACHU is untouched");
    p.newEgg();
    ck(p.eggIsHoopa(), "the next egg is HOOPA");
    ck(!p.hoopaPending, "and the debt is paid");
    p.eggTap(); p.eggTap(); p.eggTap();
    p.newEgg();
    ck(!p.eggIsHoopa() || p.eggRarity()==R_LEGENDARIO, "after that the egg is an ordinary roll again");
  }

  // --- the form switch
  {
    Pet p; p.begin(); p.setKidsMode(true);
    p.dbgHatchAs(720,false);
    p.bond = 0; p.ageMinutes = 0;
    ck(!p.canChangeForm(), "a freshly hatched HOOPA cannot change form yet");
    p.ageMinutes = 3UL*MINUTES_PER_LEVEL; p.bond = 1;
    ck(p.canChangeForm(), "at level 3 with a first bit of bond it can");
    uint16_t atkC = p.atkStat();
    p.toggleForm();
    ck(p.form == FORM_UNBOUND, "toggling unbinds it");
    ck(p.atkStat() > atkC, "unbound hits harder (160 vs 110 base ATK)");
    ck(p.dex().type2 == T_DARK, "and is PSYCHIC/DARK");
    Combatant c; combatantFromPet(c, p);
    ck(c.type2 == T_DARK, "the battle engine sees the unbound typing");
    ck(c.dex == 720, "while the dex number (sprite, name, pokedex) stays 720");
    p.toggleForm();
    ck(p.form == FORM_CONFINED && p.dex().type2 == T_GHOST, "and it can be confined again");
    p.sleeping = true;
    ck(!p.canChangeForm(), "not while asleep");
    p.sleeping = false;
    p.dbgHatchAs(25,false);
    ck(!p.canChangeForm() && p.form == FORM_CONFINED, "a new hatch is never unbound, and only HOOPA has the button");
  }

  // --- persistence
  {
    Pet p; p.begin(); p.dbgHatchAs(720,false);
    p.hoopaUnlocked = true; p.hoopaPending = true; p.form = FORM_UNBOUND; p.saveNow();
    Pet q; q.begin();
    ck(q.hoopaUnlocked && q.hoopaPending && q.form == FORM_UNBOUND, "unlock, debt and form survive a reload");
  }

  printf(bad ? "FAILURES\n" : "all good\n");
  return bad ? 1 : 0;
}
