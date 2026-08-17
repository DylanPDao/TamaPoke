#pragma once
#include <stdint.h>
#include "dex.h"

// The gym ladders: 8 leaders, the Elite 4 and the Champion for each region,
// with their real teams and levels -- Kanto from FireRed/LeafGreen, Johto from
// Gold/Silver/Crystal, Hoenn from Ruby/Sapphire/Emerald.
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
// The ladders are levelled to this game's curve, where 100 is the ceiling.
#define MAX_TRAINER_LEVEL 100

struct TrainerMon {
  uint16_t dex;      // NOT uint8_t: Hoenn runs to 386
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

static const Trainer TRAINERS_KANTO[TRAINER_COUNT] = {
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


// Johto: Gold/Silver/Crystal. The levels run lower than Kanto's early on and
// the Elite 4 sits at 40-50, which lands a Johto run comfortably inside a
// three-day life the same way Kanto's does.
static const Trainer TRAINERS_JOHTO[TRAINER_COUNT] = {
  { "FALKNER",  "VIOLET",     T_FLYING,   2, { {16,7},{17,9} } },
  { "BUGSY",    "AZALEA",     T_BUG,      3, { {11,14},{14,14},{123,16} } },
  { "WHITNEY",  "GOLDENROD",  T_NORMAL,   2, { {35,18},{241,20} } },
  { "MORTY",    "ECRUTEAK",   T_GHOST,    4, { {92,21},{93,21},{94,25},{93,23} } },
  { "CHUCK",    "CIANWOOD",   T_FIGHTING, 2, { {57,27},{62,30} } },
  { "JASMINE",  "OLIVINE",    T_STEEL,    3, { {81,30},{81,30},{208,35} } },
  { "PRYCE",    "MAHOGANY",   T_ICE,      3, { {86,27},{87,29},{221,31} } },
  { "CLAIR",    "BLACKTHORN", T_DRAGON,   4, { {148,37},{148,37},{148,37},{230,40} } },
  { "WILL",     "ELITE 4",    T_PSYCHIC,  5, { {178,40},{124,41},{103,41},{80,41},{178,42} } },
  { "KOGA",     "ELITE 4",    T_POISON,   5, { {168,40},{49,41},{205,43},{89,42},{169,44} } },
  { "BRUNO",    "ELITE 4",    T_FIGHTING, 5, { {237,42},{106,42},{107,42},{95,43},{68,46} } },
  { "KAREN",    "ELITE 4",    T_DARK,     5, { {197,42},{45,42},{94,45},{198,44},{229,47} } },
  { "LANCE",    "CHAMPION",   T_DRAGON,   6, { {130,44},{149,47},{149,47},{142,46},{6,46},{149,50} } },
};

// Hoenn: EMERALD, consistently. That choice follows from Juan being the eighth
// leader -- in Ruby/Sapphire that seat is Wallace's and Steven is the champion,
// while in Emerald Juan takes the gym and Wallace the title. Mixing the two
// would have given a ladder that exists in neither game. Emerald's Steven is a
// post-game rematch at level 77 and is deliberately not here.
static const Trainer TRAINERS_HOENN[TRAINER_COUNT] = {
  { "ROXANNE",  "RUSTBORO",   T_ROCK,     3, { {74,12},{74,12},{299,15} } },
  { "BRAWLY",   "DEWFORD",    T_FIGHTING, 3, { {66,16},{307,16},{296,19} } },
  { "WATTSON",  "MAUVILLE",   T_ELECTRIC, 4, { {100,20},{309,20},{82,22},{310,24} } },
  { "FLANNERY", "LAVARIDGE",  T_FIRE,     4, { {322,24},{218,24},{323,26},{324,29} } },
  { "NORMAN",   "PETALBURG",  T_NORMAL,   4, { {327,27},{288,27},{264,29},{289,31} } },
  { "WINONA",   "FORTREE",    T_FLYING,   5, { {333,29},{357,29},{279,30},{227,31},{334,33} } },
  { "TATE",     "MOSSDEEP",   T_PSYCHIC,  4, { {344,41},{178,41},{337,42},{338,42} } },
  { "JUAN",     "SOOTOPOLIS", T_WATER,    5, { {370,41},{340,41},{364,43},{342,43},{230,46} } },
  { "SIDNEY",   "ELITE 4",    T_DARK,     5, { {262,46},{275,48},{332,46},{342,48},{359,49} } },
  { "PHOEBE",   "ELITE 4",    T_GHOST,    5, { {356,48},{354,49},{302,50},{354,49},{356,51} } },
  { "GLACIA",   "ELITE 4",    T_ICE,      5, { {364,50},{362,50},{364,52},{362,52},{365,53} } },
  { "DRAKE",    "ELITE 4",    T_DRAGON,   5, { {372,52},{334,54},{230,53},{330,53},{373,55} } },
  { "WALLACE",  "CHAMPION",   T_WATER,    6, { {321,57},{73,55},{272,56},{340,56},{130,56},{350,58} } },
};

// One ladder per region, in the same order as REGIONS in dex.h.
struct TrainerSet {
  const Trainer *list;
  const char *region;
};
#define GYM_REGIONS 3
static const TrainerSet TRAINER_SETS[GYM_REGIONS] = {
  { TRAINERS_KANTO, "KANTO" },
  { TRAINERS_JOHTO, "JOHTO" },
  { TRAINERS_HOENN, "HOENN" },
};

// Hard mode reruns the same ladder with perfect IVs and a smarter AI, so the
// teams need no second table -- only the difficulty flag changes.
#define HARD_IV 31
#define EASY_IV 16
