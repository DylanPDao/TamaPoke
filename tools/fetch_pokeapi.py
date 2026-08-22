#!/usr/bin/env python3
"""Pull the 151 from PokeAPI: 6 base stats and the learnsets for our move list.

  python3 tools/fetch_pokeapi.py

Writes two generated files, both committed:
  tools/dex_stats.py      num -> (hp, atk, def, spe, spa, spd)
  tools/dex_learnsets.py  num -> [(move slug, level), ...] for MOVES only

Responses are cached under tools/pokeapi_cache/ (gitignored), so a second run
costs nothing. Delete the directory to force a refetch.

Why 6 stats when the pet only rolls 4 IVs: the special split lives on the
SPECIES, not the individual. Alakazam has 50 Attack and 135 Special Attack --
without bSpA it would be a terrible attacker, which is wrong. Special moves
run off ivAtk/trAtk against bSpA, special defence off ivDef/trDef against
bSpD, so no new IVs and no save migration. See dex_moves.py.
"""
import json
import os
import ssl
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(__file__))
from dex_moves import MOVES

# The python.org macOS builds ship no CA bundle, so a plain urlopen against
# HTTPS dies with CERTIFICATE_VERIFY_FAILED. certifi comes with those same
# builds; fall back to the system default when it is genuinely absent.
try:
    import certifi
    SSL_CTX = ssl.create_default_context(cafile=certifi.where())
except ImportError:
    SSL_CTX = None

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, 'pokeapi_cache')
API = 'https://pokeapi.co/api/v2/pokemon/%d'
# Derived from dex_data.py rather than written here, so a hardcoded number
# cannot fall behind the table again. gen_moves.py had its own DEX_COUNT = 151
# once and emitted a Kanto-sized LEARN_OFS against a dex that had grown, which
# would have read off the end of the array for every species past 151.
def _dex_count():
    import sys, os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from dex_data import DEX
    return max(d[0] for d in DEX)


DEX_COUNT = _dex_count()

# Which ways of learning a move count as "this species can use it". Level-up
# plus TM: that is how you would actually build a set, and level-up alone
# leaves several species without even a same-type attack.
METHODS = ('level-up', 'machine')

# Version group the level-up levels are read from, PER GENERATION -- a Johto or
# Hoenn species has no FireRed/LeafGreen learnset at all, so reading them all
# from Kanto would silently drop every gate for two thirds of the dex.
# Each is the last game of its own generation, where the gates are properly
# spread and not yet flattened by later rebalancing.
def level_vg(num):
    if num <= 151:
        return 'firered-leafgreen'
    if num <= 251:
        return 'crystal'
    return 'emerald'


def fetch(num):
    path = os.path.join(CACHE, '%03d.json' % num)
    if os.path.exists(path):
        with open(path) as f:
            return json.load(f)
    os.makedirs(CACHE, exist_ok=True)
    # PokeAPI 403s the default python-urllib agent, same as SpriteCollab does
    # in pack_pmd.py
    req = urllib.request.Request(API % num, headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(req, timeout=30, context=SSL_CTX) as r:
        data = json.load(r)
    # keep only what we need; the raw payloads are ~200 KB each
    slim = {
        'name': data['name'],
        'stats': {s['stat']['name']: s['base_stat'] for s in data['stats']},
        'moves': [
            {
                'name': m['move']['name'],
                'details': [
                    (d['move_learn_method']['name'], d['level_learned_at'],
                     d['version_group']['name'])
                    for d in m['version_group_details']
                ],
            }
            for m in data['moves']
        ],
    }
    with open(path, 'w') as f:
        json.dump(slim, f)
    time.sleep(0.15)  # be polite to a free API
    return slim


def main():
    wanted = {slug for _name, slug, *_ in MOVES if slug}  # None = not learnable
    stats, learn = {}, {}
    missing = set(wanted)

    for num in range(1, DEX_COUNT + 1):
        d = fetch(num)
        s = d['stats']
        stats[num] = (s['hp'], s['attack'], s['defense'], s['speed'],
                      s['special-attack'], s['special-defense'])

        rows = []
        for m in d['moves']:
            if m['name'] not in wanted:
                continue
            missing.discard(m['name'])
            # A move's LEVEL GATE wins over its TM entry. The old rule took the
            # lowest of the two and scored a TM as 0, so anything ever sold as a
            # TM lost its gate -- Charizard's Flamethrower is TM38 in gen 1, so
            # it came out ungated, and 1907 of 2281 rows collapsed to level 0.
            #
            # Levels are read from ONE version group, not min()'d across all of
            # them: evolved forms relearn their basics at level 1 on evolving in
            # some games, and the minimum flattened Charizard's whole set to 1.
            # FireRed/LeafGreen is the pick -- Kanto, all 151, properly gated.
            lvlup = [lvl for meth, lvl, vg in m['details']
                     if meth == 'level-up' and vg == level_vg(num) and lvl > 0]
            if not lvlup:   # not in that game (or only at level 1): any game
                lvlup = [lvl for meth, lvl, _vg in m['details']
                         if meth == 'level-up' and lvl > 1]
            is_tm = any(meth == 'machine' for meth, _lvl, _vg in m['details'])
            has_lvl1 = any(meth == 'level-up' and lvl <= 1
                           for meth, lvl, _vg in m['details'])
            if lvlup:
                lv = min(lvlup)
            elif has_lvl1:
                lv = 1      # a starting move
            elif is_tm:
                lv = 0      # TM only: no gate, taught on demand
            else:
                lv = None
            if lv is not None:
                rows.append((m['name'], lv))
        rows.sort(key=lambda r: (r[1], r[0]))
        learn[num] = rows
        if num % 25 == 0:
            print('  ...%d/%d' % (num, DEX_COUNT))

    hdr = '# GENERADO por tools/fetch_pokeapi.py desde PokeAPI - no editar\n'

    with open(os.path.join(HERE, 'dex_stats.py'), 'w') as f:
        f.write(hdr)
        f.write('# num -> (hp, atk, def, vel, spa, spd). Los dos ultimos son\n'
                '# nuevos: el reparto fisico/especial vive en la especie, no en\n'
                '# el individuo (ver fetch_pokeapi.py y dex_moves.py).\n')
        f.write('BASE_STATS = {\n')
        for num in range(1, DEX_COUNT + 1):
            f.write('    %d: %r,\n' % (num, stats[num]))
        f.write('}\n')

    with open(os.path.join(HERE, 'dex_learnsets.py'), 'w') as f:
        f.write(hdr)
        f.write('# num -> [(slug del movimiento, nivel)], solo los de dex_moves.py.\n'
                '# nivel 0 = MT, sin requisito de nivel. El nivel se guarda pero\n'
                '# todavia no se usa para nada: si la seleccion de movimientos lo\n'
                '# aprovecha o no se decide en la fase de UI.\n')
        f.write('LEARNSETS = {\n')
        for num in range(1, DEX_COUNT + 1):
            f.write('    %d: %r,\n' % (num, learn[num]))
        f.write('}\n')

    total = sum(len(v) for v in learn.values())
    thin = [n for n in learn if len(learn[n]) < 4]
    print('stats: %d especies x 6' % len(stats))
    print('learnsets: %d filas, %.1f de media' % (total, total / DEX_COUNT))
    if missing:
        print('AVISO: %d movimientos que nadie aprende: %s'
              % (len(missing), ', '.join(sorted(missing))))
    if thin:
        print('AVISO: %d especies con menos de 4 movimientos: %s'
              % (len(thin), thin))


if __name__ == '__main__':
    main()
