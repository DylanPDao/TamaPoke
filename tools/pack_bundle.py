#!/usr/bin/env python3
"""Empaqueta los sprites de la SD (tools/sdcard/mons/*.bin) en un .pak POR
REGION para que el instalador web los suba de un clic.

ONE FILE PER REGION, not one big one, and that is forced rather than chosen:
Kanto alone is already ~40 MB, so all three would be ~100 MB -- exactly GitHub's
hard per-file limit, which would make the bundle uncommittable. Splitting also
means a player can install Kanto and stop, which is the whole 40 MB most people
want, and add a region later without re-sending what they already have.

Formato TPAK (little-endian):
  char[4]  "TPAK"
  uint16   count
  count x { uint8 nameLen; char name[nameLen]; uint32 size }   (indice)
  ...datos de cada fichero, en el mismo orden...

El instalador (web/index.html) lo descarga, lo parte por el indice y manda cada
fichero a la placa con el protocolo PUT (igual que tools/send_sd.py).
"""
import glob
import os
import struct

HERE = os.path.dirname(__file__)
MONS = os.path.join(HERE, 'sdcard', 'mons')
WEB = os.path.join(HERE, '..', 'web')

# Must match REGIONS in dex_data.py. A sprite file is pNNN.bin or psNNN.bin, so
# the dex number is the trailing digits.
REGIONS = [('kanto', 1, 151), ('johto', 152, 251), ('hoenn', 252, 386)]

GITHUB_LIMIT = 100 * 1024 * 1024


def dex_of(path):
    base = os.path.basename(path)
    digits = ''.join(c for c in base if c.isdigit())
    return int(digits) if digits else 0


def write_pak(out, files):
    names = ['mons/' + os.path.basename(f) for f in files]
    blobs = [open(f, 'rb').read() for f in files]
    with open(out, 'wb') as o:
        o.write(b'TPAK')
        o.write(struct.pack('<H', len(files)))
        for name, blob in zip(names, blobs):
            nb = name.encode()
            o.write(struct.pack('<B', len(nb)))
            o.write(nb)
            o.write(struct.pack('<I', len(blob)))
        for blob in blobs:
            o.write(blob)
    return sum(len(b) for b in blobs)


def main():
    files = sorted(glob.glob(os.path.join(MONS, '*.bin')))
    if not files:
        raise SystemExit('no hay sprites en ' + MONS)
    made = 0
    for name, lo, hi in REGIONS:
        mine = [f for f in files if lo <= dex_of(f) <= hi]
        if not mine:
            print(f'{name}: no sprites packed yet, skipped')
            continue
        out = os.path.join(WEB, f'sprites-{name}.pak')
        total = write_pak(out, mine)
        size = os.path.getsize(out)
        flag = '  !! OVER GITHUB LIMIT' if size > GITHUB_LIMIT else ''
        print(f'{os.path.normpath(out)}: {len(mine)} sprites, '
              f'{total / 1048576:.1f} MB datos ({size / 1048576:.1f} MB total){flag}')
        made += 1
    if not made:
        raise SystemExit('nothing packed')


if __name__ == '__main__':
    main()
