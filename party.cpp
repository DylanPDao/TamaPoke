#include "party.h"
#include <stdlib.h>
#include <string.h>
#include "dex.h"
#include "moves.h"   // MOVE_COUNT
#include "pet.h"     // MAX_LEVEL

Party party;

// One rule for every record read off NVS, party and box alike. A blob written
// by an older or newer build, or restored by IMPORT, can hold anything that
// passed its CRC: a dex that is not in this build (dropped rather than indexing
// DEX_TBL with it), a move index past MOVE_TBL (MOVE_TBL[0] is the "-" filler,
// so 0 is the safe value), or a level the stat formula and the battle's uint8_t
// were never meant to see. The nick is terminated whatever was stored.
static void sanitize(PartyMon &s) {
  if (s.dex < 1 || s.dex > DEX_COUNT) s.dex = 0;
  if (s.level < 1) s.level = 1;
  if (s.level > MAX_LEVEL) s.level = MAX_LEVEL;
  for (int i = 0; i < MOVE_SLOTS; i++)
    if (s.moves[i] >= MOVE_COUNT) s.moves[i] = 0;
  s.nick[sizeof(s.nick) - 1] = 0;
}

// Same NVS namespace as the pet on purpose: WIPE (Pet::factoryReset) calls
// clear() on it, and a factory reset that left the party behind would be a lie.
void Party::begin() {
  // Start from empty: getBytes() leaves the destination untouched when the key
  // is missing, so without this a reload after a wipe would keep showing the
  // old party out of RAM.
  for (auto &s : slots) s = PartyMon();
  prefs.begin("tamapoke", false);
  // The blob is raw structs, so growing PartyMon (moves[] was appended in v1.9)
  // changes its stride. Reading an older, shorter blob straight into the new
  // array would land slot 1 onward at the wrong offset and quietly invent a
  // party out of misaligned bytes -- and the dex-range check below would not
  // reliably catch it, since a stray byte is often a valid Pokedex number.
  // So migrate by length: copy each old record into the front of the new one
  // and leave moves[] zeroed for the learnset to fill in.
  size_t stored = prefs.getBytesLength("party");
  if (stored == sizeof(slots)) {
    prefs.getBytes("party", slots, sizeof(slots));
  } else if (stored > sizeof(slots) && stored % sizeof(PartyMon) == 0) {
    // A blob from a build with MORE SLOTS at our own stride. Only this case is
    // unambiguous: stored/sizeof(PartyMon) records, each laid out as we lay them
    // out, so the first PARTY_SLOTS of them are ours to keep. Anything else that
    // is merely "too long" could equally be the same slot count at a BIGGER
    // stride, where a prefix read would land slot 1 at the wrong offset and
    // invent a party out of misaligned bytes -- so that is left empty instead.
    //
    // getBytes copies NOTHING when the stored blob exceeds the buffer, so this
    // has to go through a temporary of the stored size.
    uint8_t *tmp = (uint8_t *)malloc(stored);
    if (tmp) {
      if (prefs.getBytes("party", tmp, stored) == stored)
        memcpy(slots, tmp, sizeof(slots));
      free(tmp);
    }
  } else if (stored && stored % PARTY_SLOTS == 0 && stored < sizeof(slots)) {
    size_t oldStride = stored / PARTY_SLOTS;
    uint8_t old[sizeof(slots)];
    prefs.getBytes("party", old, stored);
    for (int i = 0; i < PARTY_SLOTS; i++)
      memcpy(&slots[i], old + i * oldStride, oldStride);
    save();   // rewrite in the current layout so this only happens once
  }
  // a blob written by an older/newer build could hold nonsense
  for (auto &s : slots) sanitize(s);
  // The box is a separate key and simply absent on an older save, which leaves
  // it zeroed -- exactly what an empty box is.
  for (auto &s : box) s = PartyMon();
  size_t boxStored = prefs.getBytesLength("box");
  if (boxStored == sizeof(box)) {
    prefs.getBytes("box", box, sizeof(box));
  } else if (boxStored > sizeof(box) && boxStored % sizeof(PartyMon) == 0) {
    // Same as the party above: a later build with more box slots. Keep the
    // first BOX_SLOTS rather than dropping the whole box on the floor, which is
    // what happened before -- getBytes refuses an oversized blob outright.
    uint8_t *tmp = (uint8_t *)malloc(boxStored);
    if (tmp) {
      if (prefs.getBytes("box", tmp, boxStored) == boxStored)
        memcpy(box, tmp, sizeof(box));
      free(tmp);
    }
  }
  for (auto &s : box) sanitize(s);
}

void Party::save() {
  prefs.putBytes("party", slots, sizeof(slots));
}

void Party::boxSave() {
  prefs.putBytes("box", box, sizeof(box));
}

uint8_t Party::boxCount() const {
  uint8_t n = 0;
  for (auto &s : box)
    if (!s.empty()) n++;
  return n;
}

int Party::boxFirstFree() const {
  for (int i = 0; i < BOX_SLOTS; i++)
    if (box[i].empty()) return i;
  return -1;
}

bool Party::boxAdd(const PartyMon &m) {
  int i = boxFirstFree();
  if (i < 0) return false;
  box[i] = m;
  boxSave();
  return true;
}

void Party::boxReleaseAt(uint8_t i) {
  if (i >= BOX_SLOTS) return;
  box[i] = PartyMon();
  boxSave();
}

void Party::swapPartyBox(uint8_t partyIdx, uint8_t boxIdx) {
  if (partyIdx >= PARTY_SLOTS || boxIdx >= BOX_SLOTS) return;
  PartyMon t = slots[partyIdx];
  slots[partyIdx] = box[boxIdx];
  box[boxIdx] = t;
  save();
  boxSave();
}

uint8_t Party::count() const {
  uint8_t n = 0;
  for (auto &s : slots)
    if (!s.empty()) n++;
  return n;
}

int Party::firstFree() const {
  for (int i = 0; i < PARTY_SLOTS; i++)
    if (slots[i].empty()) return i;
  return -1;
}

bool Party::add(const PartyMon &m) {
  int i = firstFree();
  if (i < 0) return false;
  slots[i] = m;
  save();
  return true;
}

void Party::replaceAt(uint8_t i, const PartyMon &m) {
  if (i >= PARTY_SLOTS) return;
  slots[i] = m;
  save();
}

void Party::releaseAt(uint8_t i) {
  if (i >= PARTY_SLOTS) return;
  slots[i] = PartyMon();
  save();
}

// Mirrors calcStat() in pet.cpp: base + level + IV contribution + training.
// Kept in step with it by hand; there is no shared home for it that both the
// live pet and a frozen party member could use without dragging Pet in here.
static uint16_t calcStat(uint8_t base, uint8_t iv, uint16_t lvl, uint8_t tr) {
  return (uint16_t)base + lvl + (uint32_t)iv * lvl / 100 + tr;
}

uint16_t Party::atkOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bAtk, m.ivAtk, m.level, m.trAtk);
}
uint16_t Party::defOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bDef, m.ivDef, m.level, m.trDef);
}
uint16_t Party::speOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bSpe, m.ivSpe, m.level, m.trSpe);
}
uint16_t Party::vitOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bHp, m.ivHp, m.level, 10);
}
// Special reuses the physical IV and training, same rule as Pet::spaStat().
uint16_t Party::spaOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bSpA, m.ivAtk, m.level, m.trAtk);
}
uint16_t Party::spdOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bSpD, m.ivDef, m.level, m.trDef);
}
