#!/usr/bin/env python3
"""Genera dex.h (tabla de la Pokedex para el firmware) desde dex_data.py.

  python3 tools/gen_dex.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from dex_data import DEX, TYPE_ACCENTS, CLASSIC, RARE, LEGENDARY
from dex_stats import BASE_STATS
from dex_types import TYPES, TYPE_ORDER, CHART


def rgb565(hexcol):
    r, g, b = int(hexcol[1:3], 16), int(hexcol[3:5], 16), int(hexcol[5:7], 16)
    return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)


# bioma de fondo por tipo (la luz la pone la hora real del RTC)
# 0 PRADERA, 1 PLAYA, 2 BOSQUE, 3 VOLCAN, 4 MONTANA, 5 NIEVE
TYPE_BIOME = {
    'agua': 1, 'planta': 2, 'bicho': 2, 'fuego': 3,
    'roca': 4, 'tierra': 4, 'dragon': 1, 'hielo': 5,  # los dragones gen1 (Dratini) viven en el agua
    'normal': 0, 'electrico': 0, 'lucha': 0, 'veneno': 0,
    'psiquico': 0, 'fantasma': 0,
}

# excepciones por dex# (el tipo no basta): fosiles marinos roca/agua -> playa
BIOME_OVERRIDE = {138: 1, 139: 1, 140: 1, 141: 1}  # Omanyte, Omastar, Kabuto, Kabutops


def main():
    out = []
    out.append("#pragma once\n#include <stdint.h>\n\n")
    out.append("// GENERADO por tools/gen_dex.py desde tools/dex_data.py - no editar\n\n")
    out.append("#define DEX_COUNT 151\n")
    out.append("#define DEX_EEVEE 133  // rama al azar: 134/135/136\n\n")
    out.append("// The 18 current types. See tools/dex_types.py for why this game uses the\n"
               "// modern chart rather than the Gen 1 one.\n"
               "enum PkType : uint8_t {\n  ")
    out.append(", ".join("T_" + t.upper() for t in TYPE_ORDER))
    out.append(",\n  T_NONE = 255\n};\n#define TYPE_COUNT %d\n\n" % len(TYPE_ORDER))

    # effectiveness matrix, stored as tenths so it stays integer maths on the
    # MCU: 0 = immune, 5 = half, 10 = neutral, 20 = double
    out.append("// Type chart in TENTHS (0 immune, 5 not-very, 10 neutral, 20 super).\n"
               "// Multiplying the two defender columns gives a percentage directly:\n"
               "// 20*20 = 400 (4x), 10*10 = 100 (1x), 5*10 = 50 (0.5x).\n")
    out.append("static const uint8_t TYPE_FX[TYPE_COUNT][TYPE_COUNT] = {\n")
    nonneutral = 0
    for atk in TYPE_ORDER:
        row = []
        for dfn in TYPE_ORDER:
            m = CHART.get(atk, {}).get(dfn, 1)
            if m != 1:
                nonneutral += 1
            row.append(str(int(m * 10)))
        out.append("  { %s },  // %s\n" % (", ".join(f"{v:>2}" for v in row), atk))
    out.append("};\n\n")
    print(f"tabla de tipos: {len(TYPE_ORDER)}x{len(TYPE_ORDER)}, {nonneutral} celdas no neutras")
    out.append(
        "// rareza: 0 = solo por evolucion, 1 = comun, 2 = raro, 3 = legendario\n"
        "enum : uint8_t { R_EVO = 0, R_COMUN, R_RARO, R_LEGENDARIO };\n\n"
        "struct DexEntry {\n"
        "  const char *name;\n"
        "  uint8_t evolvesTo;    // numero de dex, 0 = forma final\n"
        "  uint8_t evolveLevel;\n"
        "  uint8_t rarity;       // sale de huevo si > 0\n"
        "  uint16_t accent;      // color RGB565 del tipo para la UI\n"
        "  uint8_t bHp, bAtk, bDef, bSpe;  // base stats actuales (PokeAPI), no los de gen 1\n"
        "  uint8_t bSpA, bSpD;   // ataque/defensa especial: el reparto fisico-especial\n"
        "                        // vive en la especie, el individuo solo tira 4 IV\n"
        "  uint8_t biome;        // 0 pradera 1 playa 2 bosque 3 volcan 4 montana 5 nieve\n"
        "  uint8_t type1, type2;  // current typing; type2 = T_NONE if single-typed\n"
        "};\n\n")
    # formas base = las que no son evolucion de nadie (las ramas de Eevee si lo son)
    evolved = {evo for *_, evo, _lvl in [(d[4], d[5]) for d in DEX] for evo in [_[0] for _ in [(d[4],) for d in DEX]]}
    evolved = {d[4] for d in DEX if d[4]} | {135, 136}
    rarities = []
    out.append("static const DexEntry DEX_TBL[DEX_COUNT + 1] = {\n")
    out.append('  { "?", 0, 0, 0, 0x2946, 50, 50, 50, 50, 50, 50, 0 },  // 0: sin usar\n')
    for num, slug, display, typ, evo, lvl in DEX:
        acc = rgb565(TYPE_ACCENTS[typ])
        if num in evolved:
            rar = 'R_EVO'
        elif num in LEGENDARY:
            rar = 'R_LEGENDARIO'
        elif num in RARE:
            rar = 'R_RARO'
        else:
            rar = 'R_COMUN'
        rarities.append(rar)
        hp, atk, df, spe, spa, spd = BASE_STATS[num]
        bio = BIOME_OVERRIDE.get(num, TYPE_BIOME[typ])
        t1, t2 = TYPES[num]
        c1 = "T_" + t1.upper()
        c2 = ("T_" + t2.upper()) if t2 else "T_NONE"
        dual = f"/{t2}" if t2 else ""
        out.append(f'  {{ "{display}", {evo}, {lvl}, {rar}, 0x{acc:04X}, {hp}, {atk}, {df}, {spe},'
                   f' {spa}, {spd}, {bio}, {c1}, {c2} }},  // {num} {t1}{dual}\n')
    out.append("};\n\n")
    out.append("// el primer huevo de la partida: iniciales clasicos\n")
    out.append("static const int16_t CLASSIC_DEX[] = { %s };\n" % ", ".join(map(str, CLASSIC)))
    out.append(f"#define NUM_CLASSIC_DEX {len(CLASSIC)}\n")
    from collections import Counter
    c = Counter(rarities)
    print(f"bases: {c['R_COMUN']} comunes, {c['R_RARO']} raras, {c['R_LEGENDARIO']} legendarias, {c['R_EVO']} solo-evolucion")

    path = os.path.join(os.path.dirname(__file__), '..', 'dex.h')
    open(path, 'w').write(''.join(out))
    print(f"guardado {os.path.normpath(path)} ({len(DEX)} especies)")


if __name__ == '__main__':
    main()
