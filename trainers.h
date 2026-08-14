#pragma once
#include <stdint.h>
#include "dex.h"

// The gym ladder: 8 leaders, the Elite 4 and the Champion, with their Kanto
// (FireRed/LeafGreen) teams and levels.
//
// HAND-AUTHORED -- not generated. The levels are the real ones and they happen
// to fit this game's curve almost exactly: level is age at 1/hour, a pet retires
// at 73 after three days and caps at 100, so Brock at 12-14 is an afternoon and
// Lance at 54-60 is a well-raised creature. Nothing was rescaled.
//
// There is deliberately no gating. A leader always brings its whole team and you
// bring whoever you have, so attrition is the difficulty: one strong creature
// can sweep Brock but will not survive five of Lance's in a row.

#define TRAINER_TEAM_MAX 6

struct TrainerMon {
  uint8_t dex;
  uint8_t level;
};

struct Trainer {
  const char *name;
  const char *place;   // gym town, or the Elite 4 room
  uint8_t type;        // the type they specialise in, for the UI accent
  uint8_t count;
  TrainerMon team[TRAINER_TEAM_MAX];
};

// index 0-7 are the badges, 8-11 the Elite 4, 12 the Champion
#define TRAINER_COUNT 13
#define TRAINER_GYMS 8
#define TRAINER_ELITE4 4

static const Trainer TRAINERS[TRAINER_COUNT] = {
  { "BROCK",    "PEWTER",    T_ROCK,     2, { {74,12},{95,14} } },
  { "MISTY",    "CERULEAN",  T_WATER,    2, { {120,18},{121,21} } },
  { "LT SURGE", "VERMILION", T_ELECTRIC, 3, { {100,21},{25,18},{26,24} } },
  { "ERIKA",    "CELADON",   T_GRASS,    3, { {71,29},{114,24},{45,29} } },
  { "KOGA",     "FUCHSIA",   T_POISON,   4, { {109,37},{89,39},{109,37},{110,43} } },
  { "SABRINA",  "SAFFRON",   T_PSYCHIC,  4, { {64,38},{122,37},{49,38},{65,43} } },
  { "BLAINE",   "CINNABAR",  T_FIRE,     4, { {58,42},{77,40},{78,42},{59,47} } },
  { "GIOVANNI", "VIRIDIAN",  T_GROUND,   5, { {111,45},{51,42},{31,44},{34,45},{112,50} } },
  { "LORELEI",  "ELITE 4",   T_ICE,      5, { {87,52},{91,51},{80,52},{124,54},{131,54} } },
  { "BRUNO",    "ELITE 4",   T_FIGHTING, 5, { {95,51},{107,53},{106,53},{95,54},{68,56} } },
  { "AGATHA",   "ELITE 4",   T_GHOST,    5, { {94,54},{42,54},{93,53},{24,56},{94,58} } },
  { "LANCE",    "ELITE 4",   T_DRAGON,   5, { {130,56},{148,54},{148,54},{142,58},{149,60} } },
  { "RIVAL",    "CHAMPION",  T_NORMAL,   6, { {18,61},{65,59},{112,61},{59,63},{103,61},{9,65} } },
};

// Hard mode reruns the same ladder with perfect IVs and a smarter AI, so the
// teams need no second table -- only the difficulty flag changes.
#define HARD_IV 31
#define EASY_IV 16
