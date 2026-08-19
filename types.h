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

// One colour per type, for the chips on move rows and the battle grid. The move
// list used to print the type as grey 6px text, which on a 466px round panel at
// arm's length is unreadable -- and type is the single most important thing
// about a move in a game whose whole combat model is the type chart.
//
// These are the standard series type colours converted to RGB565. They are
// hand-written rather than generated because they are not derived from anything:
// DexEntry.accent is per SPECIES and says nothing about a move.
static const uint16_t TYPE_COL[TYPE_COUNT] = {
  0xAD4F, 0xF406, 0x6C9E,     // NORMAL, FIRE, WATER
  0xFE86, 0x7E4A, 0x9EDB,     // ELECTRIC, GRASS, ICE
  0xC185, 0xA214, 0xE60D,     // FIGHTING, POISON, GROUND
  0xAC9E, 0xFAD1, 0xADC4,     // FLYING, PSYCHIC, BUG
  0xBD07, 0x72D3, 0x71DF,     // ROCK, GHOST, DRAGON
  0x72C9, 0xBDDA, 0xECD5,     // DARK, STEEL, FAIRY
};

// Whether a type's colour is light enough to need dark text on it. Precomputed
// from the same luminance the eye uses (0.299/0.587/0.114) so the label never
// comes out grey-on-yellow.
static const uint8_t TYPE_COL_LIGHT[TYPE_COUNT] = { 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 1, 1 };

static inline uint16_t typeColor(uint8_t t) {
  return t < TYPE_COUNT ? TYPE_COL[t] : 0x8410;
}
static inline bool typeColorIsLight(uint8_t t) {
  return t < TYPE_COUNT && TYPE_COL_LIGHT[t];
}
