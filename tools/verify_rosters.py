#!/usr/bin/env python3
"""Check the Johto and Hoenn ladders in trainers.h against the real game data.

    python3 tools/verify_rosters.py

The Kanto ladder was checked by hand against FireRed/LeafGreen. Johto and Hoenn
were written from recall, which is not good enough to leave unverified.

The source is the pret DISASSEMBLIES rather than a wiki: data/trainers/
parties.asm in pokecrystal and src/data/trainer_parties.h in pokeemerald are the
games' own tables, so they cannot be out of date or mistyped by an editor. It is
the same project already used for the avatars.

Only the FIRST party of a name is taken. Crystal and Emerald both carry rematch
teams for the same leaders, and the gym battle is the first one.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from dex_data import DEX

CRYSTAL = 'https://raw.githubusercontent.com/pret/pokecrystal/master/data/trainers/parties.asm'
EMERALD = 'https://raw.githubusercontent.com/pret/pokeemerald/master/src/data/trainer_parties.h'
# Platinum keeps ONE JSON PER TRAINER, and rematches are their own files -- so
# unlike Crystal and Emerald there is no "take the first party" ambiguity here.
PLATINUM = 'https://raw.githubusercontent.com/pret/pokeplatinum/main/res/trainers/data/%s.json'

# Ladder order, which is NOT the same as Diamond/Pearl: Fantina is the third gym
# in Platinum and the fifth there. The level ramp is what pins it -- 14/22/26/32
# /37/41/44/50 is monotonic only this way.
SINNOH_FILES = ['leader_roark', 'leader_gardenia', 'leader_fantina',
                'leader_maylene', 'leader_wake', 'leader_byron',
                'leader_candice', 'leader_volkner', 'elite_four_aaron',
                'elite_four_bertha', 'elite_four_flint', 'elite_four_lucian',
                'champion_cynthia']
SINNOH = ['ROARK', 'GARDENIA', 'FANTINA', 'MAYLENE', 'WAKE', 'BYRON', 'CANDICE',
          'VOLKNER', 'AARON', 'BERTHA', 'FLINT', 'LUCIAN', 'CYNTHIA']

JOHTO = ['FALKNER', 'BUGSY', 'WHITNEY', 'MORTY', 'CHUCK', 'JASMINE', 'PRYCE',
         'CLAIR', 'WILL', 'KOGA', 'BRUNO', 'KAREN', 'LANCE']
HOENN = ['Roxanne', 'Brawly', 'Wattson', 'Flannery', 'Norman', 'Winona',
         'TateAndLiza', 'Juan', 'Sidney', 'Phoebe', 'Glacia', 'Drake', 'Wallace']

SLUG = {}
for n, slug, *_ in DEX:
    SLUG[slug] = n


def dex_of(raw):
    """A disassembly species constant -> our dex number."""
    s = re.sub(r'[^a-z0-9]', '', raw.lower())
    if s in SLUG:
        return SLUG[s]
    # TATE_AND_LIZA style leftovers and the odd spelling difference
    fix = {'nidoranf': 'nidoranf', 'nidoranm': 'nidoranm', 'hooh': 'hooh',
           'mrmime': 'mrmime', 'farfetchd': 'farfetchd'}
    return SLUG.get(fix.get(s, s))


def fetch(url):
    r = subprocess.run(['curl', '-fsSL', url], capture_output=True)
    if r.returncode != 0:
        raise SystemExit('fetch failed: %s' % url)
    return r.stdout.decode('utf-8', 'replace')


def crystal_parties(text):
    """name -> [(dex, level)], first party only."""
    out = {}
    cur = None
    for line in text.splitlines():
        m = re.match(r'\s*db "([A-Z0-9_ .\']+)@", TRAINERTYPE_(\w+)', line)
        if m:
            cur = m.group(1).strip()
            if cur in out:
                cur = None            # a rematch; keep the first
            else:
                out[cur] = []
            continue
        if cur is None:
            continue
        m = re.match(r'\s*db\s+(\d+),\s*([A-Z0-9_]+)', line)
        if m:
            d = dex_of(m.group(2))
            if d:
                out[cur].append((d, int(m.group(1))))
            continue
        if re.match(r'\s*db -1', line):
            cur = None
    return out


def emerald_parties(text):
    out = {}
    cur = None
    lvl = None
    for line in text.splitlines():
        m = re.match(r'static const struct \w+ sParty_(\w+?)(\d*)\[\]', line)
        if m:
            name = m.group(1)
            cur = name if name not in out else None
            if cur:
                out[cur] = []
            continue
        if cur is None:
            continue
        m = re.search(r'\.lvl\s*=\s*(\d+)', line)
        if m:
            lvl = int(m.group(1))
            continue
        m = re.search(r'\.species\s*=\s*SPECIES_(\w+)', line)
        if m and lvl is not None:
            d = dex_of(m.group(1))
            if d:
                out[cur].append((d, lvl))
            lvl = None
    return out


def ours(region):
    """Parse our own table out of trainers.h."""
    src = open(os.path.join(HERE, '..', 'trainers.h'), encoding='utf-8').read()
    blk = src.split('TRAINERS_%s[TRAINER_COUNT] = {' % region)[1].split('\n};')[0]
    out = []
    for line in blk.splitlines():
        m = re.match(r'\s*\{\s*"([^"]+)",\s*"[^"]+",\s*\w+,\s*(\d+),\s*\{(.*)\}\s*\}', line)
        if not m:
            continue
        team = [(int(a), int(b)) for a, b in re.findall(r'\{(\d+),(\d+)\}', m.group(3))]
        out.append((m.group(1), team))
    return out


def compare(label, mine, theirs, names):
    print('=== %s' % label)
    diffs = 0
    for (myname, myteam), key in zip(mine, names):
        real = theirs.get(key)
        if real is None:
            print('  %-9s NOT FOUND in the disassembly as %r' % (myname, key))
            diffs += 1
            continue
        if myteam == real:
            print('  %-9s ok' % myname)
            continue
        diffs += 1
        print('  %-9s DIFFERS' % myname)
        print('      ours: %s' % ' '.join('%d:L%d' % t for t in myteam))
        print('      real: %s' % ' '.join('%d:L%d' % t for t in real))
    print('  %d of %d trainers differ\n' % (diffs, len(mine)))
    return diffs


def platinum_parties():
    """{OUR NAME: [(dex, level), ...]} straight out of pokeplatinum's JSON."""
    import json
    out = {}
    for fn, name in zip(SINNOH_FILES, SINNOH):
        d = json.loads(fetch(PLATINUM % fn))
        team = []
        for m in d['party']:
            team.append((dex_of(m['species'].replace('SPECIES_', '')), m['level']))
        out[name] = team
    return out


def main():
    cj = crystal_parties(fetch(CRYSTAL))
    eh = emerald_parties(fetch(EMERALD))
    n = compare('JOHTO (pokecrystal)', ours('JOHTO'), cj, JOHTO)
    n += compare('HOENN (pokeemerald)', ours('HOENN'), eh, HOENN)
    n += compare('SINNOH (pokeplatinum)', ours('SINNOH'), platinum_parties(), SINNOH)
    print('%d trainers differ in total' % n)
    return 1 if n else 0


if __name__ == '__main__':
    sys.exit(main())
