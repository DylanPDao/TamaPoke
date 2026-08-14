#!/usr/bin/env python3
"""Genera moves.h (tabla de movimientos + learnsets) desde dex_moves.py.

  python3 tools/gen_moves.py

Reads the hand-authored move list in dex_moves.py and the learnsets fetched
into dex_learnsets.py by fetch_pokeapi.py. Run fetch_pokeapi.py first if
dex_learnsets.py is missing or the move list changed.

Learnsets are stored CSR-style: one flat LEARN_TBL of (move, level) pairs
plus a LEARN_OFS index, so species n owns LEARN_OFS[n] .. LEARN_OFS[n+1].
That is ~4.5 KB of flash for all 151, against ~20 KB for a fixed 2D array.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from dex_moves import (MOVES, MC_PHYS, MC_SPEC, MC_STATUS, EF_STAGE,
                       ST_ATK, ST_DEF, ST_SPA, ST_SPD, ST_SPE, TG_SELF, TG_FOE,
                       AIL_NONE, AIL_PARA, AIL_BURN, AIL_POISON, AIL_SLEEP,
                       AIL_FREEZE, AIL_CONFUSE)

# The ailment pair is optional in MOVES: only the ~17 moves that inflict one
# spell it out, the rest are (AIL_NONE, 0). Keeps the table readable and means
# adding an ailment touches one line rather than all 77.
def unpack(m):
    return tuple(m) + ((AIL_NONE, 0) if len(m) == 11 else ())
from dex_learnsets import LEARNSETS

DEX_COUNT = 151
MAX_NAME = 12  # four move buttons across a 466 px round panel at text size 2


def ident(name):
    """DOUBLE-EDGE -> MV_DOUBLE_EDGE"""
    out = ''.join(c if c.isalnum() else '_' for c in name)
    return 'MV_' + out.upper()


def main():
    # --- validation: a bad row here is a silent wrong-damage bug on hardware
    seen = set()
    for name, slug, typ, cat, power, acc, eff, param, mask, stages, tgt, _ail, _ach in map(unpack, MOVES):
        if len(name) > MAX_NAME:
            sys.exit('nombre demasiado largo (%d > %d): %s' % (len(name), MAX_NAME, name))
        if ident(name) in seen:
            sys.exit('nombre duplicado: %s' % name)
        seen.add(ident(name))
        if cat == MC_STATUS and power:
            sys.exit('%s: un movimiento de estado no puede tener potencia' % name)
        if cat != MC_STATUS and not power and eff not in (4, 5):  # FIXED_LVL / FIXED
            sys.exit('%s: potencia 0 sin dano fijo' % name)
        if (eff == EF_STAGE) != bool(mask):
            sys.exit('%s: EF_STAGE y statMask no concuerdan' % name)
        if mask and not stages:
            sys.exit('%s: statMask sin stages' % name)
        if not 0 <= acc <= 100:
            sys.exit('%s: precision fuera de rango' % name)

    idx = {name: i + 1 for i, (name, *_) in enumerate(MOVES)}  # 0 = MV_NONE
    by_slug = {slug: name for name, slug, *_ in MOVES if slug}

    o = []
    o.append('#pragma once\n#include <stdint.h>\n#include "dex.h"\n\n')
    o.append('// GENERADO por tools/gen_moves.py desde tools/dex_moves.py - no editar\n\n')

    o.append('// Categoria. No hay IV de ataque especial: los movimientos\n'
             '// especiales usan ivAtk/trAtk contra bSpA de la especie, y la\n'
             '// defensa especial ivDef/trDef contra bSpD. El reparto\n'
             '// fisico/especial vive en la especie, no en el individuo.\n')
    o.append('enum : uint8_t { MC_PHYS = %d, MC_SPEC = %d, MC_STATUS = %d };\n\n'
             % (MC_PHYS, MC_SPEC, MC_STATUS))

    o.append('// Efectos. param lleva la carga util de cada uno.\n')
    o.append('enum : uint8_t {\n'
             '  EF_NONE = 0,\n'
             '  EF_STAGE,       // statMask + stages sobre target\n'
             '  EF_RECOIL,      // param = denominador (3 -> 1/3 del dano hecho)\n'
             '  EF_DRAIN,       // param = % del dano hecho que se cura\n'
             '  EF_FIXED_LVL,   // dano = nivel del atacante, ignora stats\n'
             '  EF_FIXED,       // param = dano fijo\n'
             '  EF_PRIORITY,    // param = prioridad, actua antes\n'
             '  EF_NEVER_MISS,  // no tira precision\n'
             '  EF_MULTI,       // 2-5 golpes, power es por golpe\n'
             '  EF_HEAL,        // param = % de vitalidad maxima curada\n'
             '  EF_RECHARGE,    // pierde el turno siguiente, expuesto\n'
             '  EF_CHARGE,      // turno 1 carga; param 1 = invulnerable mientras\n'
             '};\n\n')

    o.append('// Mascara de stats para EF_STAGE: un solo delta se aplica a\n'
             '// todos los bits activos (DRAGON DANCE = ATK|SPE +1).\n')
    o.append('enum : uint8_t { ST_ATK = %d, ST_DEF = %d, ST_SPA = %d, ST_SPD = %d, ST_SPE = %d };\n'
             % (ST_ATK, ST_DEF, ST_SPA, ST_SPD, ST_SPE))
    o.append('enum : uint8_t { TG_SELF = %d, TG_FOE = %d };\n\n' % (TG_SELF, TG_FOE))
    o.append('// Status ailments. Battle-only: they live in the battle state and\n'
             '// clear when it ends, so nothing is written to the pet or replayed\n'
             '// by the RTC offline catch-up.\n'
             'enum : uint8_t {\n'
             '  AIL_NONE = %d, AIL_PARA = %d, AIL_BURN = %d, AIL_POISON = %d,\n'
             '  AIL_SLEEP = %d, AIL_FREEZE = %d, AIL_CONFUSE = %d,\n};\n\n'
             % (AIL_NONE, AIL_PARA, AIL_BURN, AIL_POISON, AIL_SLEEP, AIL_FREEZE, AIL_CONFUSE))

    o.append('struct MoveEntry {\n'
             '  const char *name;\n'
             '  uint8_t type;      // PkType\n'
             '  uint8_t cat;       // MC_*\n'
             '  uint8_t power;     // 0 en estado y en dano fijo\n'
             '  uint8_t acc;       // porcentaje; 0 = no puede fallar\n'
             '  uint8_t effect;    // EF_*\n'
             '  int8_t  param;     // carga util del efecto\n'
             '  uint8_t statMask;  // ST_* (solo EF_STAGE)\n'
             '  int8_t  stages;    // +/- niveles (solo EF_STAGE)\n'
             '  uint8_t target;    // TG_*\n'
             '  uint8_t ailment;   // AIL_*, 0 = none\n'
             '  uint8_t ailChance; // percent, 0 = never\n'
             '};\n\n')

    o.append('enum MoveId : uint8_t {\n  MV_NONE = 0,\n')
    for name, *_ in MOVES:
        o.append('  %s,\n' % ident(name))
    o.append('};\n')
    o.append('#define MOVE_COUNT %d\n\n' % (len(MOVES) + 1))

    def cname(m):
        return 'MC_PHYS' if m == MC_PHYS else ('MC_SPEC' if m == MC_SPEC else 'MC_STATUS')

    def maskname(m):
        if not m:
            return '0'
        bits = [n for n, b in (('ST_ATK', ST_ATK), ('ST_DEF', ST_DEF), ('ST_SPA', ST_SPA),
                               ('ST_SPD', ST_SPD), ('ST_SPE', ST_SPE)) if m & b]
        return ' | '.join(bits)

    EFN = {0: 'EF_NONE', 1: 'EF_STAGE', 2: 'EF_RECOIL', 3: 'EF_DRAIN', 4: 'EF_FIXED_LVL',
           5: 'EF_FIXED', 6: 'EF_PRIORITY', 7: 'EF_NEVER_MISS', 8: 'EF_MULTI',
           9: 'EF_HEAL', 10: 'EF_RECHARGE', 11: 'EF_CHARGE'}

    o.append('static const MoveEntry MOVE_TBL[MOVE_COUNT] = {\n')
    o.append('  { "-", T_NORMAL, MC_STATUS, 0, 0, EF_NONE, 0, 0, 0, TG_SELF, AIL_NONE, 0 },  // 0: sin usar\n')
    AILN = {AIL_NONE: 'AIL_NONE', AIL_PARA: 'AIL_PARA', AIL_BURN: 'AIL_BURN',
            AIL_POISON: 'AIL_POISON', AIL_SLEEP: 'AIL_SLEEP',
            AIL_FREEZE: 'AIL_FREEZE', AIL_CONFUSE: 'AIL_CONFUSE'}
    for i, m in enumerate(MOVES):
        (name, slug, typ, cat, power, acc, eff, param, mask, stages, tgt,
         ail, ach) = unpack(m)
        o.append('  { "%s", T_%s, %s, %d, %d, %s, %d, %s, %d, %s, %s, %d },  // %d\n'
                 % (name, typ.upper(), cname(cat), power, acc, EFN[eff], param,
                    maskname(mask), stages, 'TG_SELF' if tgt == TG_SELF else 'TG_FOE',
                    AILN[ail], ach, i + 1))
    o.append('};\n\n')

    # --- learnsets, CSR ---------------------------------------------------
    o.append('// Learnsets en formato CSR: la especie n ocupa\n'
             '// LEARN_TBL[LEARN_OFS[n] .. LEARN_OFS[n + 1]).\n'
             '// nivel 0 = MT, sin requisito. El nivel se guarda pero todavia\n'
             '// no filtra nada: lo decide la fase de UI.\n')
    o.append('struct LearnEntry { uint8_t move; uint8_t level; };\n\n')

    flat, ofs = [], [0]
    empties = []
    for n in range(0, DEX_COUNT + 1):
        rows = LEARNSETS.get(n, []) if n else []
        for slug, lv in rows:
            flat.append((idx[by_slug[slug]], min(lv, 255), by_slug[slug]))
        if n and not rows:
            empties.append(n)
        ofs.append(len(flat))

    o.append('static const LearnEntry LEARN_TBL[%d] = {\n' % len(flat))
    line = []
    for mid, lv, nm in flat:
        line.append('{ %3d, %3d },' % (mid, lv))
        if len(line) == 6:
            o.append('  ' + ' '.join(line) + '\n')
            line = []
    if line:
        o.append('  ' + ' '.join(line) + '\n')
    o.append('};\n\n')

    o.append('static const uint16_t LEARN_OFS[DEX_COUNT + 2] = {\n')
    for i in range(0, len(ofs), 12):
        o.append('  ' + ' '.join('%5d,' % v for v in ofs[i:i + 12]) + '\n')
    o.append('};\n\n')

    o.append('// Cuantos movimientos puede usar la especie, y cual es el i-esimo.\n'
             '// DITTO y las dos crisalidas no aprenden ninguno de la lista: para\n'
             '// ellos moveOf() devuelve MV_STRUGGLE en vez de nada.\n')
    o.append('static inline uint8_t learnCount(int16_t dex) {\n'
             '  if (dex < 1 || dex > DEX_COUNT) return 0;\n'
             '  return (uint8_t)(LEARN_OFS[dex + 1] - LEARN_OFS[dex]);\n'
             '}\n\n')
    o.append('static inline uint8_t learnMove(int16_t dex, uint8_t i) {\n'
             '  if (i >= learnCount(dex)) return MV_STRUGGLE;\n'
             '  return LEARN_TBL[LEARN_OFS[dex] + i].move;\n'
             '}\n\n')
    o.append('static inline uint8_t learnLevel(int16_t dex, uint8_t i) {\n'
             '  if (i >= learnCount(dex)) return 0;\n'
             '  return LEARN_TBL[LEARN_OFS[dex] + i].level;\n'
             '}\n')

    path = os.path.join(os.path.dirname(__file__), '..', 'moves.h')
    open(path, 'w').write(''.join(o))

    tbl_bytes = len(flat) * 2 + len(ofs) * 2
    print('movimientos: %d (+MV_NONE)' % len(MOVES))
    print('learnsets: %d filas, %.1f de media, %d bytes de tabla'
          % (len(flat), len(flat) / DEX_COUNT, tbl_bytes))
    if empties:
        print('sin movimientos (usaran STRUGGLE): %s' % empties)
    print('guardado %s' % os.path.normpath(path))


if __name__ == '__main__':
    main()
