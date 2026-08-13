#pragma once
#include <stdint.h>

// GENERADO por tools/gen_dex.py desde tools/dex_data.py - no editar

#define DEX_COUNT 151
#define DEX_EEVEE 133  // rama al azar: 134/135/136

// The 18 current types. See tools/dex_types.py for why this game uses the
// modern chart rather than the Gen 1 one.
enum PkType : uint8_t {
  T_NORMAL, T_FIRE, T_WATER, T_ELECTRIC, T_GRASS, T_ICE, T_FIGHTING, T_POISON, T_GROUND, T_FLYING, T_PSYCHIC, T_BUG, T_ROCK, T_GHOST, T_DRAGON, T_DARK, T_STEEL, T_FAIRY,
  T_NONE = 255
};
#define TYPE_COUNT 18

// Type chart in TENTHS (0 immune, 5 not-very, 10 neutral, 20 super).
// Multiplying the two defender columns gives a percentage directly:
// 20*20 = 400 (4x), 10*10 = 100 (1x), 5*10 = 50 (0.5x).
static const uint8_t TYPE_FX[TYPE_COUNT][TYPE_COUNT] = {
  { 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  5,  0, 10, 10,  5, 10 },  // normal
  { 10,  5,  5, 10, 20, 20, 10, 10, 10, 10, 10, 20,  5, 10,  5, 10, 20, 10 },  // fire
  { 10, 20,  5, 10,  5, 10, 10, 10, 20, 10, 10, 10, 20, 10,  5, 10, 10, 10 },  // water
  { 10, 10, 20,  5,  5, 10, 10, 10,  0, 20, 10, 10, 10, 10,  5, 10, 10, 10 },  // electric
  { 10,  5, 20, 10,  5, 10, 10,  5, 20,  5, 10,  5, 20, 10,  5, 10,  5, 10 },  // grass
  { 10,  5,  5, 10, 20,  5, 10, 10, 20, 20, 10, 10, 10, 10, 20, 10,  5, 10 },  // ice
  { 20, 10, 10, 10, 10, 20, 10,  5, 10,  5,  5,  5, 20,  0, 10, 20, 20,  5 },  // fighting
  { 10, 10, 10, 10, 20, 10, 10,  5,  5, 10, 10, 10,  5,  5, 10, 10,  0, 20 },  // poison
  { 10, 20, 10, 20,  5, 10, 10, 20, 10,  0, 10,  5, 20, 10, 10, 10, 20, 10 },  // ground
  { 10, 10, 10,  5, 20, 10, 20, 10, 10, 10, 10, 20,  5, 10, 10, 10,  5, 10 },  // flying
  { 10, 10, 10, 10, 10, 10, 20, 20, 10, 10,  5, 10, 10, 10, 10,  0,  5, 10 },  // psychic
  { 10,  5, 10, 10, 20, 10,  5,  5, 10,  5, 20, 10, 10,  5, 10, 20,  5,  5 },  // bug
  { 10, 20, 10, 10, 10, 20,  5, 10,  5, 20, 10, 20, 10, 10, 10, 10,  5, 10 },  // rock
  {  0, 10, 10, 10, 10, 10, 10, 10, 10, 10, 20, 10, 10, 20, 10,  5, 10, 10 },  // ghost
  { 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 20, 10,  5,  0 },  // dragon
  { 10, 10, 10, 10, 10, 10,  5, 10, 10, 10, 20, 10, 10, 20, 10,  5, 10,  5 },  // dark
  { 10,  5,  5,  5, 10, 20, 10, 10, 10, 10, 10, 10, 20, 10, 10, 10,  5, 20 },  // steel
  { 10,  5, 10, 10, 10, 10, 20,  5, 10, 10, 10, 10, 10, 10, 20, 20,  5, 10 },  // fairy
};

// rareza: 0 = solo por evolucion, 1 = comun, 2 = raro, 3 = legendario
enum : uint8_t { R_EVO = 0, R_COMUN, R_RARO, R_LEGENDARIO };

struct DexEntry {
  const char *name;
  uint8_t evolvesTo;    // numero de dex, 0 = forma final
  uint8_t evolveLevel;
  uint8_t rarity;       // sale de huevo si > 0
  uint16_t accent;      // color RGB565 del tipo para la UI
  uint8_t bHp, bAtk, bDef, bSpe;  // base stats actuales (PokeAPI), no los de gen 1
  uint8_t bSpA, bSpD;   // ataque/defensa especial: el reparto fisico-especial
                        // vive en la especie, el individuo solo tira 4 IV
  uint8_t biome;        // 0 pradera 1 playa 2 bosque 3 volcan 4 montana 5 nieve
  uint8_t type1, type2;  // current typing; type2 = T_NONE if single-typed
};

static const DexEntry DEX_TBL[DEX_COUNT + 1] = {
  { "?", 0, 0, 0, 0x2946, 50, 50, 50, 50, 50, 50, 0 },  // 0: sin usar
  { "BULBASAUR", 2, 16, R_COMUN, 0x3C49, 45, 49, 49, 45, 65, 65, 2, T_GRASS, T_POISON },  // 1 grass/poison
  { "IVYSAUR", 3, 32, R_EVO, 0x3C49, 60, 62, 63, 60, 80, 80, 2, T_GRASS, T_POISON },  // 2 grass/poison
  { "VENUSAUR", 0, 0, R_EVO, 0x3C49, 80, 82, 83, 80, 100, 100, 2, T_GRASS, T_POISON },  // 3 grass/poison
  { "CHARMANDER", 5, 16, R_COMUN, 0xEA87, 39, 52, 43, 65, 60, 50, 3, T_FIRE, T_NONE },  // 4 fire
  { "CHARMELEON", 6, 36, R_EVO, 0xEA87, 58, 64, 58, 80, 80, 65, 3, T_FIRE, T_NONE },  // 5 fire
  { "CHARIZARD", 0, 0, R_EVO, 0xEA87, 78, 84, 78, 100, 109, 85, 3, T_FIRE, T_FLYING },  // 6 fire/flying
  { "SQUIRTLE", 8, 16, R_COMUN, 0x4C98, 44, 48, 65, 43, 50, 64, 1, T_WATER, T_NONE },  // 7 water
  { "WARTORTLE", 9, 36, R_EVO, 0x4C98, 59, 63, 80, 58, 65, 80, 1, T_WATER, T_NONE },  // 8 water
  { "BLASTOISE", 0, 0, R_EVO, 0x4C98, 79, 83, 100, 78, 85, 105, 1, T_WATER, T_NONE },  // 9 water
  { "CATERPIE", 11, 7, R_COMUN, 0x7CC4, 45, 30, 35, 45, 20, 20, 2, T_BUG, T_NONE },  // 10 bug
  { "METAPOD", 12, 10, R_EVO, 0x7CC4, 50, 20, 55, 30, 25, 25, 2, T_BUG, T_NONE },  // 11 bug
  { "BUTTERFREE", 0, 0, R_EVO, 0x7CC4, 60, 45, 50, 70, 90, 80, 2, T_BUG, T_FLYING },  // 12 bug/flying
  { "WEEDLE", 14, 7, R_COMUN, 0x7CC4, 40, 35, 30, 50, 20, 20, 2, T_BUG, T_POISON },  // 13 bug/poison
  { "KAKUNA", 15, 10, R_EVO, 0x7CC4, 45, 25, 50, 35, 25, 25, 2, T_BUG, T_POISON },  // 14 bug/poison
  { "BEEDRILL", 0, 0, R_EVO, 0x7CC4, 65, 90, 40, 75, 45, 80, 2, T_BUG, T_POISON },  // 15 bug/poison
  { "PIDGEY", 17, 18, R_COMUN, 0x8C4D, 40, 45, 40, 56, 35, 35, 0, T_NORMAL, T_FLYING },  // 16 normal/flying
  { "PIDGEOTTO", 18, 36, R_EVO, 0x8C4D, 63, 60, 55, 71, 50, 50, 0, T_NORMAL, T_FLYING },  // 17 normal/flying
  { "PIDGEOT", 0, 0, R_EVO, 0x8C4D, 83, 80, 75, 101, 70, 70, 0, T_NORMAL, T_FLYING },  // 18 normal/flying
  { "RATTATA", 20, 20, R_COMUN, 0x8C4D, 30, 56, 35, 72, 25, 35, 0, T_NORMAL, T_NONE },  // 19 normal
  { "RATICATE", 0, 0, R_EVO, 0x8C4D, 55, 81, 60, 97, 50, 70, 0, T_NORMAL, T_NONE },  // 20 normal
  { "SPEAROW", 22, 20, R_COMUN, 0x8C4D, 40, 60, 30, 70, 31, 31, 0, T_NORMAL, T_FLYING },  // 21 normal/flying
  { "FEAROW", 0, 0, R_EVO, 0x8C4D, 65, 90, 65, 100, 61, 61, 0, T_NORMAL, T_FLYING },  // 22 normal/flying
  { "EKANS", 24, 22, R_COMUN, 0x8A73, 35, 60, 44, 55, 40, 54, 0, T_POISON, T_NONE },  // 23 poison
  { "ARBOK", 0, 0, R_EVO, 0x8A73, 60, 95, 69, 80, 65, 79, 0, T_POISON, T_NONE },  // 24 poison
  { "PIKACHU", 26, 30, R_COMUN, 0xBCA1, 35, 55, 40, 90, 50, 50, 0, T_ELECTRIC, T_NONE },  // 25 electric
  { "RAICHU", 0, 0, R_EVO, 0xBCA1, 60, 90, 55, 110, 90, 80, 0, T_ELECTRIC, T_NONE },  // 26 electric
  { "SANDSHREW", 28, 22, R_COMUN, 0xB447, 50, 75, 85, 40, 20, 30, 4, T_GROUND, T_NONE },  // 27 ground
  { "SANDSLASH", 0, 0, R_EVO, 0xB447, 75, 100, 110, 65, 45, 55, 4, T_GROUND, T_NONE },  // 28 ground
  { "NIDORAN H", 30, 16, R_COMUN, 0x8A73, 55, 47, 52, 41, 40, 40, 0, T_POISON, T_NONE },  // 29 poison
  { "NIDORINA", 31, 30, R_EVO, 0x8A73, 70, 62, 67, 56, 55, 55, 0, T_POISON, T_NONE },  // 30 poison
  { "NIDOQUEEN", 0, 0, R_EVO, 0x8A73, 90, 92, 87, 76, 75, 85, 0, T_POISON, T_GROUND },  // 31 poison/ground
  { "NIDORAN M", 33, 16, R_COMUN, 0x8A73, 46, 57, 40, 50, 40, 40, 0, T_POISON, T_NONE },  // 32 poison
  { "NIDORINO", 34, 30, R_EVO, 0x8A73, 61, 72, 57, 65, 55, 55, 0, T_POISON, T_NONE },  // 33 poison
  { "NIDOKING", 0, 0, R_EVO, 0x8A73, 81, 102, 77, 85, 85, 75, 0, T_POISON, T_GROUND },  // 34 poison/ground
  { "CLEFAIRY", 36, 30, R_COMUN, 0x8C4D, 70, 45, 48, 35, 60, 65, 0, T_FAIRY, T_NONE },  // 35 fairy
  { "CLEFABLE", 0, 0, R_EVO, 0x8C4D, 95, 70, 73, 60, 95, 90, 0, T_FAIRY, T_NONE },  // 36 fairy
  { "VULPIX", 38, 30, R_COMUN, 0xEA87, 38, 41, 40, 65, 50, 65, 3, T_FIRE, T_NONE },  // 37 fire
  { "NINETALES", 0, 0, R_EVO, 0xEA87, 73, 76, 75, 100, 81, 100, 3, T_FIRE, T_NONE },  // 38 fire
  { "JIGGLYPUFF", 40, 30, R_COMUN, 0x8C4D, 115, 45, 20, 20, 45, 25, 0, T_NORMAL, T_FAIRY },  // 39 normal/fairy
  { "WIGGLYTUFF", 0, 0, R_EVO, 0x8C4D, 140, 70, 45, 45, 85, 50, 0, T_NORMAL, T_FAIRY },  // 40 normal/fairy
  { "ZUBAT", 42, 22, R_COMUN, 0x8A73, 40, 45, 35, 55, 30, 40, 0, T_POISON, T_FLYING },  // 41 poison/flying
  { "GOLBAT", 0, 0, R_EVO, 0x8A73, 75, 80, 70, 90, 65, 75, 0, T_POISON, T_FLYING },  // 42 poison/flying
  { "ODDISH", 44, 21, R_COMUN, 0x3C49, 45, 50, 55, 30, 75, 65, 2, T_GRASS, T_POISON },  // 43 grass/poison
  { "GLOOM", 45, 36, R_EVO, 0x3C49, 60, 65, 70, 40, 85, 75, 2, T_GRASS, T_POISON },  // 44 grass/poison
  { "VILEPLUME", 0, 0, R_EVO, 0x3C49, 75, 80, 85, 50, 110, 90, 2, T_GRASS, T_POISON },  // 45 grass/poison
  { "PARAS", 47, 24, R_COMUN, 0x7CC4, 35, 70, 55, 25, 45, 55, 2, T_BUG, T_GRASS },  // 46 bug/grass
  { "PARASECT", 0, 0, R_EVO, 0x7CC4, 60, 95, 80, 30, 60, 80, 2, T_BUG, T_GRASS },  // 47 bug/grass
  { "VENONAT", 49, 31, R_COMUN, 0x7CC4, 60, 55, 50, 45, 40, 55, 2, T_BUG, T_POISON },  // 48 bug/poison
  { "VENOMOTH", 0, 0, R_EVO, 0x7CC4, 70, 65, 60, 90, 90, 75, 2, T_BUG, T_POISON },  // 49 bug/poison
  { "DIGLETT", 51, 26, R_COMUN, 0xB447, 10, 55, 25, 95, 35, 45, 4, T_GROUND, T_NONE },  // 50 ground
  { "DUGTRIO", 0, 0, R_EVO, 0xB447, 35, 100, 50, 120, 50, 70, 4, T_GROUND, T_NONE },  // 51 ground
  { "MEOWTH", 53, 28, R_COMUN, 0x8C4D, 40, 45, 35, 90, 40, 40, 0, T_NORMAL, T_NONE },  // 52 normal
  { "PERSIAN", 0, 0, R_EVO, 0x8C4D, 65, 70, 60, 115, 65, 65, 0, T_NORMAL, T_NONE },  // 53 normal
  { "PSYDUCK", 55, 33, R_COMUN, 0x4C98, 50, 52, 48, 55, 65, 50, 1, T_WATER, T_NONE },  // 54 water
  { "GOLDUCK", 0, 0, R_EVO, 0x4C98, 80, 82, 78, 85, 95, 80, 1, T_WATER, T_NONE },  // 55 water
  { "MANKEY", 57, 28, R_COMUN, 0xA2A5, 40, 80, 35, 70, 35, 45, 0, T_FIGHTING, T_NONE },  // 56 fighting
  { "PRIMEAPE", 0, 0, R_EVO, 0xA2A5, 65, 105, 60, 95, 60, 70, 0, T_FIGHTING, T_NONE },  // 57 fighting
  { "GROWLITHE", 59, 30, R_RARO, 0xEA87, 55, 70, 45, 60, 70, 50, 3, T_FIRE, T_NONE },  // 58 fire
  { "ARCANINE", 0, 0, R_EVO, 0xEA87, 90, 110, 80, 95, 100, 80, 3, T_FIRE, T_NONE },  // 59 fire
  { "POLIWAG", 61, 25, R_COMUN, 0x4C98, 40, 50, 40, 90, 40, 40, 1, T_WATER, T_NONE },  // 60 water
  { "POLIWHIRL", 62, 36, R_EVO, 0x4C98, 65, 65, 65, 90, 50, 50, 1, T_WATER, T_NONE },  // 61 water
  { "POLIWRATH", 0, 0, R_EVO, 0x4C98, 90, 95, 95, 70, 70, 90, 1, T_WATER, T_FIGHTING },  // 62 water/fighting
  { "ABRA", 64, 16, R_COMUN, 0xD28F, 25, 20, 15, 90, 105, 55, 0, T_PSYCHIC, T_NONE },  // 63 psychic
  { "KADABRA", 65, 40, R_EVO, 0xD28F, 40, 35, 30, 105, 120, 70, 0, T_PSYCHIC, T_NONE },  // 64 psychic
  { "ALAKAZAM", 0, 0, R_EVO, 0xD28F, 55, 50, 45, 120, 135, 95, 0, T_PSYCHIC, T_NONE },  // 65 psychic
  { "MACHOP", 67, 28, R_COMUN, 0xA2A5, 70, 80, 50, 35, 35, 35, 0, T_FIGHTING, T_NONE },  // 66 fighting
  { "MACHOKE", 68, 40, R_EVO, 0xA2A5, 80, 100, 70, 45, 50, 60, 0, T_FIGHTING, T_NONE },  // 67 fighting
  { "MACHAMP", 0, 0, R_EVO, 0xA2A5, 90, 130, 80, 55, 65, 85, 0, T_FIGHTING, T_NONE },  // 68 fighting
  { "BELLSPROUT", 70, 21, R_COMUN, 0x3C49, 50, 75, 35, 40, 70, 30, 2, T_GRASS, T_POISON },  // 69 grass/poison
  { "WEEPINBELL", 71, 36, R_EVO, 0x3C49, 65, 90, 50, 55, 85, 45, 2, T_GRASS, T_POISON },  // 70 grass/poison
  { "VICTREEBEL", 0, 0, R_EVO, 0x3C49, 80, 105, 65, 70, 100, 70, 2, T_GRASS, T_POISON },  // 71 grass/poison
  { "TENTACOOL", 73, 30, R_COMUN, 0x4C98, 40, 40, 35, 70, 50, 100, 1, T_WATER, T_POISON },  // 72 water/poison
  { "TENTACRUEL", 0, 0, R_EVO, 0x4C98, 80, 70, 65, 100, 80, 120, 1, T_WATER, T_POISON },  // 73 water/poison
  { "GEODUDE", 75, 25, R_COMUN, 0x9407, 40, 80, 100, 20, 30, 30, 4, T_ROCK, T_GROUND },  // 74 rock/ground
  { "GRAVELER", 76, 40, R_EVO, 0x9407, 55, 95, 115, 35, 45, 45, 4, T_ROCK, T_GROUND },  // 75 rock/ground
  { "GOLEM", 0, 0, R_EVO, 0x9407, 80, 120, 130, 45, 55, 65, 4, T_ROCK, T_GROUND },  // 76 rock/ground
  { "PONYTA", 78, 40, R_RARO, 0xEA87, 50, 85, 55, 90, 65, 65, 3, T_FIRE, T_NONE },  // 77 fire
  { "RAPIDASH", 0, 0, R_EVO, 0xEA87, 65, 100, 70, 105, 80, 80, 3, T_FIRE, T_NONE },  // 78 fire
  { "SLOWPOKE", 80, 37, R_COMUN, 0x4C98, 90, 65, 65, 15, 40, 40, 1, T_WATER, T_PSYCHIC },  // 79 water/psychic
  { "SLOWBRO", 0, 0, R_EVO, 0x4C98, 95, 75, 110, 30, 100, 80, 1, T_WATER, T_PSYCHIC },  // 80 water/psychic
  { "MAGNEMITE", 82, 30, R_COMUN, 0xBCA1, 25, 35, 70, 45, 95, 55, 0, T_ELECTRIC, T_STEEL },  // 81 electric/steel
  { "MAGNETON", 0, 0, R_EVO, 0xBCA1, 50, 60, 95, 70, 120, 70, 0, T_ELECTRIC, T_STEEL },  // 82 electric/steel
  { "FARFETCHD", 0, 0, R_RARO, 0x8C4D, 52, 90, 55, 60, 58, 62, 0, T_NORMAL, T_FLYING },  // 83 normal/flying
  { "DODUO", 85, 31, R_COMUN, 0x8C4D, 35, 85, 45, 75, 35, 35, 0, T_NORMAL, T_FLYING },  // 84 normal/flying
  { "DODRIO", 0, 0, R_EVO, 0x8C4D, 60, 110, 70, 110, 60, 60, 0, T_NORMAL, T_FLYING },  // 85 normal/flying
  { "SEEL", 87, 34, R_COMUN, 0x4C98, 65, 45, 55, 45, 45, 70, 1, T_WATER, T_NONE },  // 86 water
  { "DEWGONG", 0, 0, R_EVO, 0x4C98, 90, 70, 80, 70, 70, 95, 1, T_WATER, T_ICE },  // 87 water/ice
  { "GRIMER", 89, 38, R_RARO, 0x8A73, 80, 80, 50, 25, 40, 50, 0, T_POISON, T_NONE },  // 88 poison
  { "MUK", 0, 0, R_EVO, 0x8A73, 105, 105, 75, 50, 65, 100, 0, T_POISON, T_NONE },  // 89 poison
  { "SHELLDER", 91, 30, R_COMUN, 0x4C98, 30, 65, 100, 40, 45, 25, 1, T_WATER, T_NONE },  // 90 water
  { "CLOYSTER", 0, 0, R_EVO, 0x4C98, 50, 95, 180, 70, 85, 45, 1, T_WATER, T_ICE },  // 91 water/ice
  { "GASTLY", 93, 25, R_COMUN, 0x6AD3, 30, 35, 30, 80, 100, 35, 0, T_GHOST, T_POISON },  // 92 ghost/poison
  { "HAUNTER", 94, 40, R_EVO, 0x6AD3, 45, 50, 45, 95, 115, 55, 0, T_GHOST, T_POISON },  // 93 ghost/poison
  { "GENGAR", 0, 0, R_EVO, 0x6AD3, 60, 65, 60, 110, 130, 75, 0, T_GHOST, T_POISON },  // 94 ghost/poison
  { "ONIX", 0, 0, R_RARO, 0x9407, 35, 45, 160, 70, 30, 45, 4, T_ROCK, T_GROUND },  // 95 rock/ground
  { "DROWZEE", 97, 26, R_COMUN, 0xD28F, 60, 48, 45, 42, 43, 90, 0, T_PSYCHIC, T_NONE },  // 96 psychic
  { "HYPNO", 0, 0, R_EVO, 0xD28F, 85, 73, 70, 67, 73, 115, 0, T_PSYCHIC, T_NONE },  // 97 psychic
  { "KRABBY", 99, 28, R_COMUN, 0x4C98, 30, 105, 90, 50, 25, 25, 1, T_WATER, T_NONE },  // 98 water
  { "KINGLER", 0, 0, R_EVO, 0x4C98, 55, 130, 115, 75, 50, 50, 1, T_WATER, T_NONE },  // 99 water
  { "VOLTORB", 101, 30, R_COMUN, 0xBCA1, 40, 30, 50, 100, 55, 55, 0, T_ELECTRIC, T_NONE },  // 100 electric
  { "ELECTRODE", 0, 0, R_EVO, 0xBCA1, 60, 50, 70, 150, 80, 80, 0, T_ELECTRIC, T_NONE },  // 101 electric
  { "EXEGGCUTE", 103, 30, R_COMUN, 0x3C49, 60, 40, 80, 40, 60, 45, 2, T_GRASS, T_PSYCHIC },  // 102 grass/psychic
  { "EXEGGUTOR", 0, 0, R_EVO, 0x3C49, 95, 95, 85, 55, 125, 75, 2, T_GRASS, T_PSYCHIC },  // 103 grass/psychic
  { "CUBONE", 105, 28, R_COMUN, 0xB447, 50, 50, 95, 35, 40, 50, 4, T_GROUND, T_NONE },  // 104 ground
  { "MAROWAK", 0, 0, R_EVO, 0xB447, 60, 80, 110, 45, 50, 80, 4, T_GROUND, T_NONE },  // 105 ground
  { "HITMONLEE", 0, 0, R_RARO, 0xA2A5, 50, 120, 53, 87, 35, 110, 0, T_FIGHTING, T_NONE },  // 106 fighting
  { "HITMONCHAN", 0, 0, R_RARO, 0xA2A5, 50, 105, 79, 76, 35, 110, 0, T_FIGHTING, T_NONE },  // 107 fighting
  { "LICKITUNG", 0, 0, R_RARO, 0x8C4D, 90, 55, 75, 30, 60, 75, 0, T_NORMAL, T_NONE },  // 108 normal
  { "KOFFING", 110, 35, R_COMUN, 0x8A73, 40, 65, 95, 35, 60, 45, 0, T_POISON, T_NONE },  // 109 poison
  { "WEEZING", 0, 0, R_EVO, 0x8A73, 65, 90, 120, 60, 85, 70, 0, T_POISON, T_NONE },  // 110 poison
  { "RHYHORN", 112, 42, R_RARO, 0xB447, 80, 85, 95, 25, 30, 30, 4, T_GROUND, T_ROCK },  // 111 ground/rock
  { "RHYDON", 0, 0, R_EVO, 0xB447, 105, 130, 120, 40, 45, 45, 4, T_GROUND, T_ROCK },  // 112 ground/rock
  { "CHANSEY", 0, 0, R_RARO, 0x8C4D, 250, 5, 5, 50, 35, 105, 0, T_NORMAL, T_NONE },  // 113 normal
  { "TANGELA", 0, 0, R_RARO, 0x3C49, 65, 55, 115, 60, 100, 40, 2, T_GRASS, T_NONE },  // 114 grass
  { "KANGASKHAN", 0, 0, R_RARO, 0x8C4D, 105, 95, 80, 90, 40, 80, 0, T_NORMAL, T_NONE },  // 115 normal
  { "HORSEA", 117, 32, R_COMUN, 0x4C98, 30, 40, 70, 60, 70, 25, 1, T_WATER, T_NONE },  // 116 water
  { "SEADRA", 0, 0, R_EVO, 0x4C98, 55, 65, 95, 85, 95, 45, 1, T_WATER, T_NONE },  // 117 water
  { "GOLDEEN", 119, 33, R_COMUN, 0x4C98, 45, 67, 60, 63, 35, 50, 1, T_WATER, T_NONE },  // 118 water
  { "SEAKING", 0, 0, R_EVO, 0x4C98, 80, 92, 65, 68, 65, 80, 1, T_WATER, T_NONE },  // 119 water
  { "STARYU", 121, 30, R_COMUN, 0x4C98, 30, 45, 55, 85, 70, 55, 1, T_WATER, T_NONE },  // 120 water
  { "STARMIE", 0, 0, R_EVO, 0x4C98, 60, 75, 85, 115, 100, 85, 1, T_WATER, T_PSYCHIC },  // 121 water/psychic
  { "MR. MIME", 0, 0, R_RARO, 0xD28F, 40, 45, 65, 90, 100, 120, 0, T_PSYCHIC, T_FAIRY },  // 122 psychic/fairy
  { "SCYTHER", 0, 0, R_RARO, 0x7CC4, 70, 110, 80, 105, 55, 80, 2, T_BUG, T_FLYING },  // 123 bug/flying
  { "JYNX", 0, 0, R_RARO, 0x4DB8, 65, 50, 35, 95, 115, 95, 5, T_ICE, T_PSYCHIC },  // 124 ice/psychic
  { "ELECTABUZZ", 0, 0, R_RARO, 0xBCA1, 65, 83, 57, 105, 95, 85, 0, T_ELECTRIC, T_NONE },  // 125 electric
  { "MAGMAR", 0, 0, R_RARO, 0xEA87, 65, 95, 57, 93, 100, 85, 3, T_FIRE, T_NONE },  // 126 fire
  { "PINSIR", 0, 0, R_RARO, 0x7CC4, 65, 125, 100, 85, 55, 70, 2, T_BUG, T_NONE },  // 127 bug
  { "TAUROS", 0, 0, R_RARO, 0x8C4D, 75, 100, 95, 110, 40, 70, 0, T_NORMAL, T_NONE },  // 128 normal
  { "MAGIKARP", 130, 20, R_COMUN, 0x4C98, 20, 10, 55, 80, 15, 20, 1, T_WATER, T_NONE },  // 129 water
  { "GYARADOS", 0, 0, R_EVO, 0x4C98, 95, 125, 79, 81, 60, 100, 1, T_WATER, T_FLYING },  // 130 water/flying
  { "LAPRAS", 0, 0, R_RARO, 0x4C98, 130, 85, 80, 60, 85, 95, 1, T_WATER, T_ICE },  // 131 water/ice
  { "DITTO", 0, 0, R_RARO, 0x8C4D, 48, 48, 48, 48, 48, 48, 0, T_NORMAL, T_NONE },  // 132 normal
  { "EEVEE", 134, 30, R_COMUN, 0x8C4D, 55, 55, 50, 55, 45, 65, 0, T_NORMAL, T_NONE },  // 133 normal
  { "VAPOREON", 0, 0, R_EVO, 0x4C98, 130, 65, 60, 65, 110, 95, 1, T_WATER, T_NONE },  // 134 water
  { "JOLTEON", 0, 0, R_EVO, 0xBCA1, 65, 65, 60, 130, 110, 95, 0, T_ELECTRIC, T_NONE },  // 135 electric
  { "FLAREON", 0, 0, R_EVO, 0xEA87, 65, 130, 60, 65, 95, 110, 3, T_FIRE, T_NONE },  // 136 fire
  { "PORYGON", 0, 0, R_RARO, 0x8C4D, 65, 60, 70, 40, 85, 75, 0, T_NORMAL, T_NONE },  // 137 normal
  { "OMANYTE", 139, 40, R_RARO, 0x9407, 35, 40, 100, 35, 90, 55, 1, T_ROCK, T_WATER },  // 138 rock/water
  { "OMASTAR", 0, 0, R_EVO, 0x9407, 70, 60, 125, 55, 115, 70, 1, T_ROCK, T_WATER },  // 139 rock/water
  { "KABUTO", 141, 40, R_RARO, 0x9407, 30, 80, 90, 55, 55, 45, 1, T_ROCK, T_WATER },  // 140 rock/water
  { "KABUTOPS", 0, 0, R_EVO, 0x9407, 60, 115, 105, 80, 65, 70, 1, T_ROCK, T_WATER },  // 141 rock/water
  { "AERODACTYL", 0, 0, R_RARO, 0x9407, 80, 105, 65, 130, 60, 75, 4, T_ROCK, T_FLYING },  // 142 rock/flying
  { "SNORLAX", 0, 0, R_RARO, 0x8C4D, 160, 110, 65, 30, 65, 110, 0, T_NORMAL, T_NONE },  // 143 normal
  { "ARTICUNO", 0, 0, R_LEGENDARIO, 0x4DB8, 90, 85, 100, 85, 95, 125, 5, T_ICE, T_FLYING },  // 144 ice/flying
  { "ZAPDOS", 0, 0, R_LEGENDARIO, 0xBCA1, 90, 90, 85, 100, 125, 90, 0, T_ELECTRIC, T_FLYING },  // 145 electric/flying
  { "MOLTRES", 0, 0, R_LEGENDARIO, 0xEA87, 90, 100, 90, 90, 125, 85, 3, T_FIRE, T_FLYING },  // 146 fire/flying
  { "DRATINI", 148, 30, R_RARO, 0x5A98, 41, 64, 45, 50, 50, 50, 1, T_DRAGON, T_NONE },  // 147 dragon
  { "DRAGONAIR", 149, 55, R_EVO, 0x5A98, 61, 84, 65, 70, 70, 70, 1, T_DRAGON, T_NONE },  // 148 dragon
  { "DRAGONITE", 0, 0, R_EVO, 0x5A98, 91, 134, 95, 80, 100, 100, 1, T_DRAGON, T_FLYING },  // 149 dragon/flying
  { "MEWTWO", 0, 0, R_LEGENDARIO, 0xD28F, 106, 110, 90, 130, 154, 90, 0, T_PSYCHIC, T_NONE },  // 150 psychic
  { "MEW", 0, 0, R_LEGENDARIO, 0xD28F, 100, 100, 100, 100, 100, 100, 0, T_PSYCHIC, T_NONE },  // 151 psychic
};

// el primer huevo de la partida: iniciales clasicos
static const int16_t CLASSIC_DEX[] = { 1, 4, 7, 25, 133 };
#define NUM_CLASSIC_DEX 5
