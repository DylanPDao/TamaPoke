#!/usr/bin/env python3
"""Which species actually have sprite art, per region.

    python3 tools/check_sprites.py              # every region
    python3 tools/check_sprites.py --gen 4      # one generation
    python3 tools/check_sprites.py --local      # what is packed here, not upstream

Run this BEFORE adding a generation. SpriteCollab is community art and its
coverage is not uniform: Sinnoh and Kalos are complete, Paldea is 85%. A gap is
not a failure -- the firmware falls back to a numbered placeholder -- but it has
to be a known number rather than something a player discovers, and a species
with no art anywhere should be kept out of the egg pool.

Upstream is read from the GitHub API rather than by probing each dex number:
one request per hundred species instead of one per species.
"""
import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
API = ('https://api.github.com/repos/PMDCollab/SpriteCollab/contents/sprite'
       '?per_page=100&page=%d')

# (name, first dex, last dex) -- the boundaries are the games', not ours
GENS = [
    ('1 Kanto', 1, 151),    ('2 Johto', 152, 251),  ('3 Hoenn', 252, 386),
    ('4 Sinnoh', 387, 493), ('5 Unova', 494, 649),  ('6 Kalos', 650, 721),
    ('7 Alola', 722, 809),  ('8 Galar', 810, 905),  ('9 Paldea', 906, 1025),
]


def upstream():
    """Every dex number SpriteCollab has a directory for."""
    have = set()
    for page in range(1, 20):
        r = subprocess.run(['curl', '-sfL', API % page], capture_output=True)
        try:
            rows = json.loads(r.stdout)
        except Exception:
            break
        if not isinstance(rows, list) or not rows:
            break
        for e in rows:
            n = e.get('name', '')
            if n.isdigit():
                have.add(int(n))
    return have


def local():
    """Every dex number packed into tools/sdcard/mons here."""
    have = set()
    d = os.path.join(ROOT, 'tools', 'sdcard', 'mons')
    if not os.path.isdir(d):
        return have
    for f in os.listdir(d):
        if f.startswith('p') and f[1:4].isdigit() and f.endswith('.bin'):
            have.add(int(f[1:4]))
        elif f.startswith('ps') and f[2:5].isdigit():
            have.add(int(f[2:5]))
    return have


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gen', type=int, help='only this generation (1-9)')
    ap.add_argument('--local', action='store_true',
                    help='what is packed here rather than what exists upstream')
    args = ap.parse_args()

    have = local() if args.local else upstream()
    if not have:
        print('nothing found -- no network, or no sprites packed yet')
        return 1

    gens = [g for g in GENS if args.gen is None or g[0].startswith(str(args.gen))]
    print('%-12s %5s %5s  %8s  %s' % ('region', 'have', 'of', 'coverage', 'missing'))
    holes = []
    for name, lo, hi in gens:
        miss = [d for d in range(lo, hi + 1) if d not in have]
        n, tot = (hi - lo + 1) - len(miss), hi - lo + 1
        shown = ' '.join(str(d) for d in miss[:8]) + ('...' if len(miss) > 8 else '')
        print('%-12s %5d %5d  %7.1f%%  %s' % (name, n, tot, 100.0 * n / tot, shown))
        holes += miss
    if holes:
        print('\n%d species have no art. They must stay in the dex at their own '
              'numbers -- dropping one renumbers everything after it -- but '
              'should be kept out of the egg pool.' % len(holes))
    return 0


if __name__ == '__main__':
    sys.exit(main())
