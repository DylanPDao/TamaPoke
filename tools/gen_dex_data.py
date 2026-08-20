#!/usr/bin/env python3
"""Build the Pokedex source data for dex 1..N from PokeAPI.

    python3 tools/gen_dex_data.py --check      # compare against the hand data
    python3 tools/gen_dex_data.py --emit 386   # write the new dex_data/dex_types

dex_data.py and dex_types.py were hand-written for the 151. Extending them to
386 by hand is 235 entries of name, typing, evolution target and level -- so
this derives them instead.

**--check is the point of this script.** It regenerates 1..151 and diffs against
what was hand-written. If the derivation reproduces the entries a human made,
the same derivation can be trusted for 152..386; if it does not, the differences
have to be understood before anything is emitted. Run it first, always.

Responses are cached under tools/pokeapi_cache/ alongside fetch_pokeapi.py's,
so a refetch is free.
"""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, 'pokeapi_cache')
sys.path.insert(0, HERE)

# PokeAPI type name -> the accent key used in dex_data.TYPE_ACCENTS.
# The first three are Spanish because the original file is; the last four are
# types that no Gen 1 species has as its PRIMARY, so they had no accent yet.
ACCENT = {
    'normal': 'normal', 'fire': 'fuego', 'water': 'agua', 'grass': 'planta',
    'electric': 'electrico', 'ice': 'hielo', 'fighting': 'lucha',
    'poison': 'veneno', 'ground': 'tierra', 'psychic': 'psiquico',
    'bug': 'bicho', 'rock': 'roca', 'ghost': 'fantasma', 'dragon': 'dragon',
    'dark': 'siniestro', 'steel': 'acero', 'fairy': 'hada', 'flying': 'volador',
}

# Display names the hand data spells its own way. NIDORAN H/M is Spanish
# (hembra/macho); keeping it avoids a gratuitous rename of two species people
# have already registered in their Pokedex.
NAME_FIX = {
    29: 'NIDORAN H', 32: 'NIDORAN M', 122: 'MR. MIME',
}

# Evolutions with no level: the hand data converted stones to ~30 and trades to
# ~40, so that a stone evolution lands late in a 3-day life rather than never.
# The same convention is applied to the new generations.
# Flat 30 for a stone. The hand-written 151 is NOT consistent here -- Gloom,
# Poliwhirl and Weepinbell evolve by stone at 36 while Nidorina, Nidorino,
# Clefairy and Jigglypuff do it at 30, and all of them are middle forms. It was
# authored case by case, so no rule reproduces it. That is a large part of why
# the existing 151 are left exactly as they are and only 152+ is generated.
STONE_LEVEL = 30
TRADE_LEVEL = 40
HAPPY_LEVEL = 25          # friendship: reachable, since bond rises with care

# Differences between this derivation and the hand-written 151 that are
# understood and accepted. --check reports anything NOT in here, which is what
# makes it a real check rather than a formality.
KNOWN = {
    (35, 'accent'): "Clefairy is Fairy since Gen 6 and dex_types.py already "
                    "says so; only the accent colour was left at Normal",
    (36, 'accent'): "as Clefairy",
    (44, 'level'): "hand data evolves this by stone at 36; see STONE_LEVEL",
    (61, 'level'): "as Gloom",
    (70, 'level'): "as Gloom",
}


def get(url):
    """Cached GET. curl, not urllib: the python.org macOS build ships without
    a usable CA bundle (see gen_avatars.py)."""
    if not os.path.isdir(CACHE):
        os.makedirs(CACHE)
    key = re.sub(r'[^a-z0-9]+', '_', url.split('/api/v2/')[1].strip('/'))
    path = os.path.join(CACHE, key + '.json')
    if os.path.exists(path):
        return json.load(open(path))
    r = subprocess.run(['curl', '-fsSL', url], capture_output=True)
    if r.returncode != 0:
        raise SystemExit('fetch failed: %s' % url)
    d = json.loads(r.stdout)
    json.dump(d, open(path, 'w'))
    return d


def display_name(num, slug):
    if num in NAME_FIX:
        return NAME_FIX[num]
    s = slug.upper()
    s = s.replace('-F', ' H').replace('-M', ' M') if slug.startswith('nidoran') else s
    s = s.replace("'", '')          # FARFETCHD; the period in MR. MIME stays
    s = s.replace('-', ' ') if slug not in ('ho-oh',) else s.replace('-', '-')
    return s


def walk_chain(node, out, nonbase):
    """Flatten an evolution chain into {from_num: (to_num, level)}."""
    src = int(node['species']['url'].rstrip('/').split('/')[-1])
    for nxt in node['evolves_to']:
        dst = int(nxt['species']['url'].rstrip('/').split('/')[-1])
        nonbase.add(dst)
        lvl = None
        for det in nxt['evolution_details']:
            trig = (det.get('trigger') or {}).get('name')
            if det.get('min_level'):
                lvl = det['min_level']
            elif trig == 'trade':
                lvl = TRADE_LEVEL
            elif trig == 'use-item':
                lvl = -1                # resolved once the chain is known
            elif det.get('min_happiness'):
                lvl = HAPPY_LEVEL
            if lvl:
                break
        if lvl is None:
            lvl = -1                   # anything exotic: treat it as a stone
        # Only the FIRST branch is recorded: the firmware's evolvesTo is a
        # single number. Eevee is special-cased in the game code already.
        if src not in out:
            out[src] = (dst, lvl)
        walk_chain(nxt, out, nonbase)


def build(limit):
    evo, nonbase = {}, set()
    seen_chains = set()
    rows, types, legend, capture = [], {}, set(), {}
    for n in range(1, limit + 1):
        sp = get('https://pokeapi.co/api/v2/pokemon-species/%d' % n)
        pk = get('https://pokeapi.co/api/v2/pokemon/%d' % n)
        cu = sp['evolution_chain']['url']
        if cu not in seen_chains:
            seen_chains.add(cu)
            walk_chain(get(cu)['chain'], evo, nonbase)
        t = [x['type']['name'] for x in sorted(pk['types'], key=lambda x: x['slot'])]
        types[n] = (t[0], t[1] if len(t) > 1 else None)
        if sp.get('is_legendary') or sp.get('is_mythical'):
            legend.add(n)
        capture[n] = sp.get('capture_rate', 255)
        rows.append((n, sp['name'], t[0]))
        sys.stderr.write('\r%d/%d' % (n, limit)); sys.stderr.flush()
    sys.stderr.write('\n')

    # A pre-evolution outside the range does not make its target a middle form:
    # within the 151, Pikachu is a base form because Pichu does not exist yet.
    dex = []
    for n, slug, t0 in rows:
        to, lvl = evo.get(n, (0, 0))
        if lvl == -1:
            lvl = STONE_LEVEL
        if to > limit:
            to, lvl = 0, 0             # evolves into a generation we do not have
        key = slug.replace('-', '')
        dex.append((n, key, display_name(n, slug), ACCENT[t0], to, lvl))
    return dex, types, legend, capture


def check(limit=None):
    """Regenerate and diff against what is committed.

    Called with no limit it covers the WHOLE committed table, which is the
    guarantee that matters when a generation is added: every entry below the
    old DEX_COUNT must come back byte-identical, because people have Pokedex
    bits and banked creatures riding on those numbers. Gen 1 is hand-written
    and its accepted differences live in KNOWN; everything above 151 was
    generated and must match exactly.
    """
    from dex_data import DEX, TYPE_ACCENTS, LEGENDARY
    if limit is None:
        limit = max(d[0] for d in DEX)
    from dex_types import TYPES
    dex, types, legend, _ = build(limit)
    hand = {d[0]: d for d in DEX}
    bad = 0
    for e in dex:
        n = e[0]
        h = hand[n]
        for i, what in ((1, 'slug'), (2, 'name'), (3, 'accent'),
                        (4, 'evolvesTo'), (5, 'level')):
            if e[i] == h[i]:
                continue
            note = KNOWN.get((n, what))
            if note:
                print('  known: dex %-3d %-9s %r vs %r -- %s'
                      % (n, what, e[i], h[i], note))
                continue
            print('  dex %-3d %-9s generated %-12r hand %r' % (n, what, e[i], h[i]))
            bad += 1
        if types[n] != TYPES[n]:
            print('  dex %-3d typing   generated %-22r hand %r' % (n, types[n], TYPES[n]))
            bad += 1
    miss = legend ^ set(LEGENDARY)
    if miss:
        print('  legendary set differs: %s' % sorted(miss))
        bad += 1
    for a in set(ACCENT.values()):
        if a not in TYPE_ACCENTS:
            print('  accent %r has no colour in TYPE_ACCENTS' % a)
    print('%d UNEXPECTED differences over dex 1..%d (%d entries compared)'
          % (bad, limit, len(dex)))
    return bad


# Colours for the four types no Gen 1 species has as its PRIMARY, in the same
# mid-dark register as the existing accents so they read on the cream card.
NEW_ACCENTS = [
    ("    'siniestro': '#5a4a3d',", 'siniestro'),
    ("    'acero':     '#6e7e8c',", 'acero'),
    ("    'hada':      '#c4649a',", 'hada'),
    ("    'volador':   '#7f8fd0',", 'volador'),
]


def emit(limit):
    """Rewrite dex_data.py and dex_types.py, keeping 1..151 byte for byte.

    The existing entries are NOT regenerated. They were authored by hand, they
    are not internally consistent (see STONE_LEVEL), and every one of them is
    already live in somebody's Pokedex -- so they are copied through untouched
    and only the new range is inserted before the closing bracket."""
    dex, types, legend, capture = build(limit)
    evolves_to = {e[4] for e in dex if e[4]}
    base = {e[0] for e in dex if e[0] not in evolves_to}

    dd = os.path.join(HERE, 'dex_data.py')
    src = open(dd, encoding='utf-8').read()

    # --- DEX entries
    rows = []
    for e in dex:
        if e[0] <= 151:
            continue
        rows.append("    (%d, %r, %r, %r, %d, %d)," % e)
    marker = "    (151, 'mew', 'MEW', 'psiquico', 0, 0),\n"
    if marker not in src:
        raise SystemExit('dex_data.py: cannot find the end of DEX')
    src = src.replace(marker, marker + "\n    # --- generated by gen_dex_data.py; 1-151 above are hand-written\n"
                      + "\n".join(rows) + "\n", 1)

    # --- accents for the types Gen 1 never had as a primary
    for line, key in NEW_ACCENTS:
        if ("'%s'" % key) in src.split('DEX = ')[0]:
            continue
        src = src.replace("    'dragon':    '#5a52c4',",
                          "    'dragon':    '#5a52c4',\n" + line, 1)

    # --- rarity. Legendary comes straight from PokeAPI (is_legendary or
    # is_mythical). Rare is capture_rate <= 45 among BASE forms, which
    # reproduces 23 of the 27 the Gen 1 set picks by hand; the four it misses
    # (Growlithe, Ponyta, Grimer, Rhyhorn) were chosen for being uncommon in
    # game rather than statistically rare, which no data can tell you.
    newR = sorted(n for n in base if n > 151 and n not in legend
                  and capture.get(n, 255) <= 45)
    newL = sorted(n for n in legend if n > 151)
    src = re.sub(r"(RARE = \{[^}]*)\}",
                 lambda m: m.group(1).rstrip().rstrip(',') + ",\n        # generated:\n        "
                 + ", ".join(str(n) for n in newR) + "}", src, count=1)
    src = re.sub(r"(LEGENDARY = \{[^}]*)\}",
                 lambda m: m.group(1).rstrip().rstrip(',') + ", "
                 + ", ".join(str(n) for n in newL) + "}", src, count=1)
    open(dd, 'w', encoding='utf-8').write(src)

    # --- typings
    dt = os.path.join(HERE, 'dex_types.py')
    tsrc = open(dt, encoding='utf-8').read()
    lines = []
    for n in range(152, limit + 1):
        a, b = types[n]
        lines.append("    %d: (%r, %r)," % (n, a, b))
    end = tsrc.rindex('}')
    tsrc = tsrc[:end] + "    # --- generated by gen_dex_data.py\n" + "\n".join(lines) + "\n" + tsrc[end:]
    open(dt, 'w', encoding='utf-8').write(tsrc)

    print('emitted %d new species, %d new rare, %d new legendary'
          % (limit - 151, len(newR), len(newL)))


if __name__ == '__main__':
    if '--check' in sys.argv:
        # optional explicit limit: --check 151 to look at Gen 1 alone
        at = sys.argv.index('--check')
        lim = int(sys.argv[at + 1]) if len(sys.argv) > at + 1 and sys.argv[at + 1].isdigit() else None
        sys.exit(1 if check(lim) else 0)
    if '--emit' in sys.argv:
        emit(int(sys.argv[sys.argv.index('--emit') + 1]))
        sys.exit(0)
    print(__doc__)
