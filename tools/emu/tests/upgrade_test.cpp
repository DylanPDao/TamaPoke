// Does an OLD save survive an update?
//
// This is the question every existing player asks, and the answer has to be
// proven rather than asserted: people have weeks of real time in these saves,
// and "install without erase" is the path the web installer recommends.
//
// So this builds a save the way an OLDER build would have written it -- a short
// Pokedex bitmap, a party blob with the pre-moves record size, and none of the
// keys added since (box, region, per-region badges, movesets) -- and then loads
// it with the current code and checks nothing was lost or misread.
//
// The dangerous one is the party. Party::begin() infers the old record size
// from the blob length, and a misread there does not fail loudly: it invents a
// party out of misaligned bytes, and a stray byte is often a valid dex number.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "save.h"
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <vector>
uint32_t g_seed=13; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// A PartyMon as it was BEFORE moves[] was appended: identical fields in
// identical order, just without the trailing moves[]. It has to be an exact
// PREFIX of the current struct or the length-based migration cannot work -- and
// getting `medals` wrong here is what made this test fail against correct code
// the first time, by putting nick[] two bytes out.
struct OldPartyMon {
  int16_t dex;
  uint16_t level;
  uint16_t medals;
  uint8_t ivAtk, ivDef, ivSpe, ivHp;
  uint8_t trAtk, trDef, trSpe;
  uint8_t shiny;
  char nick[12];
};

int main(){
  // If this ever fails, the migration cannot work at all: Party::begin() infers
  // the old record size from the blob length and copies that many bytes into
  // the front of each new record.
  ck(sizeof(OldPartyMon) < sizeof(PartyMon),
     "the old record is shorter than the new one");
  ck(offsetof(OldPartyMon, nick) == offsetof(PartyMon, nick),
     "and every field before moves[] sits at the same offset");

  // --- write a save the way an older build would have
  {
    Preferences p;
    p.begin("tamapoke", false);
    p.clear();
    p.putBool("init", true);
    p.putUChar("full", 71); p.putUChar("joy", 63); p.putUChar("ene", 55);
    p.putUChar("hyg", 88);
    p.putShort("dexn", 59);                 // an Arcanine
    p.putUInt("age", 39UL * MINUTES_PER_LEVEL);   // level() is 1 + age/rate
    p.putUChar("ivat", 27); p.putUChar("ivdf", 14);
    p.putUChar("ivsp", 30); p.putUChar("ivhp", 9);
    p.putUChar("tatk", 55); p.putUChar("tdef", 12); p.putUChar("tspe", 61);
    p.putUShort("strk", 9); p.putUShort("bstrk", 14);
    p.putUShort("tmedal", 23);
    p.putUShort("badg", 0x001F);             // five Kanto badges, easy
    p.putUShort("badh", 0x0003);
    p.putString("nick", "BLAZE");
    p.putUChar("bond", 64);

    // the Pokedex as 19 bytes, which is what 151 species needed
    uint8_t oldDex[19] = {0};
    // byte b bit k is dex b*8 + k + 1, so dex 151 is byte 18 bit 6
    oldDex[0] = 0xFF; oldDex[1] = 0xFF; oldDex[7] = 0x40; oldDex[18] = 0x40;
    p.putBytes("dexreg", oldDex, sizeof(oldDex));
    uint8_t oldShiny[19] = {0};
    oldShiny[3] = 0x08;
    p.putBytes("dexsh", oldShiny, sizeof(oldShiny));

    // a party in the OLD record layout, no moves[]
    OldPartyMon op[PARTY_SLOTS];
    memset(op, 0, sizeof(op));
    for (int i = 0; i < 4; i++) {
      op[i].dex = (int16_t)(20 + i * 11);
      op[i].level = (uint16_t)(44 + i);
      op[i].ivAtk = op[i].ivDef = op[i].ivSpe = op[i].ivHp = (uint8_t)(15 + i);
      op[i].trAtk = 30; op[i].shiny = (i == 1); op[i].medals = (uint16_t)(3 + i);
      snprintf(op[i].nick, sizeof(op[i].nick), "OLD%d", i);
    }
    p.putBytes("party", op, sizeof(op));
    p.end();
    printf("      wrote an old save: %zu-byte party records, 19-byte dex\n",
           sizeof(OldPartyMon));
  }

  // --- now load it with the CURRENT code, as an update would
  Pet pet; Party pty;
  pet.begin(); pty.begin();

  ck(pet.speciesId == 59, "the creature survives the update");
  ck(pet.level() == 40, "at the level it was");
  ck(pet.ivAtk == 27 && pet.ivDef == 14 && pet.ivSpe == 30 && pet.ivHp == 9,
     "with its IVs intact");
  ck(pet.trAtk == 55 && pet.trDef == 12 && pet.trSpe == 61, "and its training");
  ck(!strcmp(pet.nick, "BLAZE"), "the nickname survives");
  ck(pet.streak == 9 && pet.bestStreak == 14 && pet.totalMedals == 23,
     "streak and medals survive");
  ck(pet.bond == 64, "and the bond");

  // badges: the old key holds Kanto and must still read as Kanto
  ck(pet.badgeMask(0, false) == 0x001F, "the Kanto badges are still there");
  ck(pet.badgeMask(0, true) == 0x0003, "including the hard-mode ones");
  ck(pet.badgeMask(1, false) == 0 && pet.badgeMask(2, false) == 0,
     "and the new regions start empty rather than inheriting them");

  // the Pokedex: a 19-byte blob must land in the front of the 49-byte array
  ck(pet.isRegistered(1) && pet.isRegistered(8) && pet.isRegistered(16),
     "the old Pokedex bits keep their meaning");
  ck(pet.isRegistered(151), "including the very last Kanto species");
  ck(pet.isShinyRegistered(28), "and the shiny registrations");
  bool strayGen2 = false;
  for (int d = 152; d <= DEX_COUNT; d++) if (pet.isRegistered(d)) strayGen2 = true;
  ck(!strayGen2, "and nothing in Johto or Hoenn is falsely registered");

  // THE DANGEROUS ONE: a party blob of the old stride
  ck(pty.count() == 4, "the party still has four members");
  bool ok = true;
  for (int i = 0; i < 4; i++) {
    const PartyMon &m = pty.slots[i];
    char want[8]; snprintf(want, sizeof(want), "OLD%d", i);
    if (m.dex != 20 + i * 11 || m.level != 44 + i) ok = false;
    if (strcmp(m.nick, want)) ok = false;
    if (m.ivAtk != 15 + i) ok = false;
  }
  ck(ok, "every banked creature keeps its species, level, IVs and nickname");
  ck(pty.slots[2].medals == 5, "and the medals it earned in life");
  bool freshMoves = true;
  for (int i = 0; i < 4; i++)
    if (pty.slots[i].moves[0] || pty.slots[i].moves[3]) freshMoves = false;
  ck(freshMoves, "the moveset a pre-moves save never had comes up empty, not garbage");
  ck(pty.slots[1].shiny, "a banked shiny is still shiny");
  ck(pty.boxCount() == 0, "the box, which did not exist then, comes up empty");

  // the new settings take their defaults rather than garbage
  ck(pet.region < REGION_COUNT, "the egg region defaults sanely");

  // --- and the whole thing can then be backed up and restored
  {
    static uint8_t buf[2048];
    size_t n = saveExport(buf, sizeof(buf));
    ck(n > 0, "an upgraded save exports");
    pet.factoryReset();
    ck(saveImport(buf, n), "and imports again");
    Pet p2; Party q2; p2.begin(); q2.begin();
    ck(p2.speciesId == 59 && !strcmp(p2.nick, "BLAZE") && q2.count() == 4,
       "with the upgraded contents intact");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
