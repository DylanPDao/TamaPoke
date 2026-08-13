#pragma once
#include <stdint.h>
#include "dex.h"

// Type effectiveness helpers. The chart itself (TYPE_FX) and the PkType enum
// are generated into dex.h by tools/gen_dex.py from tools/dex_types.py.
//
// Everything is integer percentages: 100 = neutral, 200 = super effective,
// 50 = resisted, 0 = immune, and 400 / 25 for the doubled-up dual-type cases.
// No floats anywhere, so this is cheap on the MCU.

// Effectiveness of an attacking type against a defender with one or two types.
// Pass T_NONE as def2 for single-typed defenders.
static inline uint16_t typeEffPct(uint8_t atk, uint8_t def1, uint8_t def2) {
  if (atk >= TYPE_COUNT || def1 >= TYPE_COUNT) return 100;
  uint16_t e = TYPE_FX[atk][def1];
  e *= (def2 < TYPE_COUNT) ? TYPE_FX[atk][def2] : 10;
  return e;  // tenths x tenths = percent
}

// Same, but for a species straight out of the Pokedex.
static inline uint16_t typeEffVsDex(uint8_t atk, int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return 100;
  return typeEffPct(atk, DEX_TBL[dex].type1, DEX_TBL[dex].type2);
}

// Same-Type Attack Bonus: 1.5x when the move matches one of the user's types.
static inline bool hasStab(int16_t dex, uint8_t moveType) {
  if (dex < 1 || dex > DEX_COUNT) return false;
  return DEX_TBL[dex].type1 == moveType || DEX_TBL[dex].type2 == moveType;
}

// Short display name for a type. English in every language, matching how the
// species names in DEX_TBL are already English regardless of the UI language.
static inline const char *typeName(uint8_t t) {
  static const char *const N[TYPE_COUNT] = {
    "NORMAL", "FIRE", "WATER", "ELECTRIC", "GRASS", "ICE", "FIGHTING",
    "POISON", "GROUND", "FLYING", "PSYCHIC", "BUG", "ROCK", "GHOST",
    "DRAGON", "DARK", "STEEL", "FAIRY",
  };
  return (t < TYPE_COUNT) ? N[t] : "?";
}
