// The save backup. A run is weeks of real time, so the thing that matters is
// not that export/import runs -- it is that NOTHING is quietly left out, and
// that a bad blob cannot destroy the save it was meant to protect.
//
// The completeness check at the bottom is the important one: it compares the
// backup's field table against the keys the firmware ACTUALLY wrote, so adding
// a field and forgetting the backup fails here instead of silently dropping it
// from every player's export.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "save.h"
#include <cstdio>
#include <set>
#include <string>
uint32_t g_seed=7; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  // --- a save worth losing: a raised creature, a full party, a filled box,
  // both ladders, a Pokedex with holes in it, a named trainer
  Pet pet;
  pet.begin();
  pet.dbgHatchAs(6,true);
  pet.ageMinutes = 72UL*MINUTES_PER_LEVEL;
  pet.ivAtk=31; pet.ivDef=7; pet.ivSpe=22; pet.ivHp=19;
  pet.trAtk=64; pet.trDef=31; pet.trSpe=90;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.rename("SCORCH");
  pet.avatar = 6;        // past the old four, so a stale & 3 mask would break it
  pet.badges = 0x00BF; pet.badgesHard = 0x000A;
  pet.streak = 12; pet.bestStreak = 19; pet.totalMedals = 41;
  pet.gameHi = 33; pet.strHi = 21; pet.spdHi = 9;
  pet.bond = 77; pet.careMistakes = 2; pet.weight = 55;
  pet.dexReg[0] = 0x5A; pet.dexReg[9] = 0xC3; pet.dexShinyReg[3] = 0x11;
  for (int i=0;i<PARTY_SLOTS;i++){ PartyMon m; m.dex=20+i*7; m.level=40+i;
    m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20+i; m.shiny=(i==2);
    snprintf(m.nick,sizeof(m.nick),"P%d",i);
    m.moves[0]=1+i; m.moves[1]=9; party.replaceAt(i,m); }
  for (int i=0;i<BOX_SLOTS;i++){ PartyMon m; m.dex=1+i*3; m.level=10+i;
    m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=11; party.box[i]=m; }
  party.boxSave();
  // renameTrainer() persists, and save() writes every field -- so this is also
  // what commits everything set above. save() itself is private on purpose.
  pet.renameTrainer("DYLAN");

  // --- COMPLETENESS. Everything the firmware stored must be in the table.
  // This is what stops the backup rotting: add a key to pet.cpp and forget
  // save.cpp, and this fails instead of quietly dropping it from every export.
  //
  // It runs HERE, against the store the firmware just wrote -- not after a
  // restore. After an import the store only holds what the backup put back, so
  // a forgotten key would be missing from both sides and the check would pass
  // itself. It did exactly that until this was moved.
  {
    std::set<std::string> inTable;
    for (uint16_t i=0;i<SAVE_FIELD_COUNT;i++) inTable.insert(SAVE_FIELDS[i].key);
    int missing = 0;
    for (auto &kv : nvs()) {
      if (inTable.count(kv.first)) continue;
      printf("      key '%s' is stored but NOT backed up\n", kv.first.c_str());
      missing++;
    }
    ck(missing==0, "every key the firmware writes is in the backup table");
    ck(inTable.size()==SAVE_FIELD_COUNT, "and the table has no duplicates");
  }

  static uint8_t buf[2048];
  size_t n = saveExport(buf, sizeof(buf));
  ck(n > 0, "a save exports");
  ck(n < sizeof(buf), "and fits a sensible buffer");
  printf("      (%u bytes)\n", (unsigned)n);

  // --- a mangled blob must never be applied
  {
    std::vector<uint8_t> t(buf, buf+n);
    t[0] = 'X';
    ck(!saveImport(t.data(), t.size()), "a blob with the wrong magic is refused");
    t = std::vector<uint8_t>(buf, buf+n);
    t[4] = SAVE_VERSION + 9;
    ck(!saveImport(t.data(), t.size()), "and one from a future version");
    t = std::vector<uint8_t>(buf, buf+n);
    t[SAVE_HDR + 3] ^= 0xFF;                   // flip a byte in the middle
    ck(!saveImport(t.data(), t.size()), "a single flipped byte fails the checksum");
    t = std::vector<uint8_t>(buf, buf+n-1);    // chopped short
    ck(!saveImport(t.data(), t.size()), "and a truncated blob is refused");
    ck(!saveImport(buf, 3), "as is one too short to hold a header");
    // and none of that touched the live save
    ck(nvs().count("party") == 1, "a refused import leaves the save alone");
  }

  // --- the real thing: wipe everything, restore, and compare
  pet.factoryReset();
  ck(nvs().empty() || nvs().count("party")==0, "the wipe really emptied NVS");
  ck(saveImport(buf, n), "the backup imports");

  Pet p2; Party q2;
  p2.begin(); q2.begin();
  ck(p2.speciesId==6 && p2.shiny, "the creature is back, shiny and all");
  ck(p2.ageMinutes==72UL*MINUTES_PER_LEVEL, "at the age it was");
  ck(p2.ivAtk==31 && p2.ivDef==7 && p2.ivSpe==22 && p2.ivHp==19, "with its IVs");
  ck(p2.trAtk==64 && p2.trDef==31 && p2.trSpe==90, "and its training");
  ck(!strcmp(p2.trainerName,"DYLAN"), "the trainer name survives");
  ck(!strcmp(p2.nick,"SCORCH"), "so does the nickname");
  ck(p2.avatar==6, "and the avatar, including one past the original four");
  ck(p2.badges==0x00BF && p2.badgesHard==0x000A, "both badge ladders");
  ck(p2.streak==12 && p2.bestStreak==19 && p2.totalMedals==41, "streak and medals");
  ck(p2.gameHi==33 && p2.strHi==21 && p2.spdHi==9, "every minigame record");
  ck(p2.bond==77 && p2.careMistakes==2 && p2.weight==55, "bond, mistakes, weight");
  // A superset, not an equality: begin() re-registers the LIVE species on every
  // load (pet.cpp:1011, seeding the dex for saves older than it existed), so
  // Charizard's bit is set on top of whatever was restored. What the backup
  // owes us is that no bit is LOST.
  ck((p2.dexReg[0] & 0x5A)==0x5A && p2.dexReg[9]==0xC3 &&
     (p2.dexShinyReg[3] & 0x11)==0x11, "the Pokedex, holes and all");
  ck((p2.dexReg[0] & ~0x5A) == 0x20,
     "and the only extra bit is the live creature re-registering itself");
  bool moves = true;
  for (int i=0;i<MOVE_SLOTS;i++) if (p2.moves[i]!=pet.moves[i]) moves=false;
  ck(moves, "and the moveset");

  ck(q2.count()==PARTY_SLOTS, "the whole party is back");
  bool party_ok = true;
  for (int i=0;i<PARTY_SLOTS;i++){
    const PartyMon &m = q2.slots[i];
    if (m.dex != 20+i*7 || m.level != 40+i) party_ok = false;
    if (m.moves[0] != 1+i || m.moves[1] != 9) party_ok = false;
    char want[8]; snprintf(want,sizeof(want),"P%d",i);
    if (strcmp(m.nick, want)) party_ok = false;
  }
  ck(party_ok, "with every level, moveset and nickname");
  ck(q2.slots[2].shiny, "and a banked shiny is still shiny");
  ck(q2.boxCount()==BOX_SLOTS, "the box comes back full");
  ck(q2.box[7].dex==1+7*3 && q2.box[7].level==17, "with the right creatures in it");

  // --- a restore must not leave anything of whatever was there before
  {
    Pet p3; p3.begin();
    p3.badges = 0xFFFF;
    p3.renameTrainer("GHOST");
    ck(saveImport(buf, n), "a second restore over a different save");
    Pet p4; p4.begin();
    ck(!strcmp(p4.trainerName,"DYLAN") && p4.badges==0x00BF,
       "overwrites it rather than merging with it");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
