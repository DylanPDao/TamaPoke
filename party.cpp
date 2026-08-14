#include "party.h"
#include "dex.h"

Party party;

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
  } else if (stored && stored % PARTY_SLOTS == 0 && stored < sizeof(slots)) {
    size_t oldStride = stored / PARTY_SLOTS;
    uint8_t old[sizeof(slots)];
    prefs.getBytes("party", old, stored);
    for (int i = 0; i < PARTY_SLOTS; i++)
      memcpy(&slots[i], old + i * oldStride, oldStride);
    save();   // rewrite in the current layout so this only happens once
  }
  // a blob written by an older/newer build could hold nonsense; drop anything
  // that is not a real Pokedex number rather than indexing DEX_TBL with it
  for (auto &s : slots) {
    if (s.dex < 1 || s.dex > DEX_COUNT) s.dex = 0;
    s.nick[sizeof(s.nick) - 1] = 0;
  }
}

void Party::save() {
  prefs.putBytes("party", slots, sizeof(slots));
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
