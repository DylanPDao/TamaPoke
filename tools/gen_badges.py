#!/usr/bin/env python3
"""Gym badges: SteGriff/pokemon-badges regional SVGs -> badges.h

    brew install librsvg
    python3 tools/gen_badges.py            # fetches all three regions
    python3 tools/gen_badges.py Kanto.svg  # or renders one local file

Renders the sheet with rsvg-convert, isolates the eight badges from its lower
(larger) row, downscales each to 32x32 with alpha and packs them 8bpp with a
per-badge palette; index 0xFF is transparent.

Badge order matches the gym order in trainers.h: Boulder, Cascade, Thunder,
Rainbow, Soul, Marsh, Volcano, Earth -- Brock through Giovanni.

Source art: Stephen Griffiths 2011, CC BY 3.0, traced from Bulbapedia.
The attribution in CREDITS.md must ship with anything built from this.
"""
import os
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'badges.h')
SIZE = 32          # emitted size
RENDER_W = 2400    # render wide so the downscale has detail to average

# One row per gym region, in the same order as TRAINER_SETS in trainers.h.
# Upstream also ships Sinnoh and Unova, so a fourth region is one line here.
SVG_BASE = 'https://raw.githubusercontent.com/SteGriff/pokemon-badges/master/svg/%s.svg'
REGIONS = ['Kanto', 'Johto', 'Hoenn']

NAMES = ['BOULDER', 'CASCADE', 'THUNDER', 'RAINBOW',
         'SOUL', 'MARSH', 'VOLCANO', 'EARTH']


def decode_png(path):
    d = open(path, 'rb').read()
    pos, idat, w, h, bd, ct = 8, b'', 0, 0, 0, 0
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        data = d[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, bd, ct = struct.unpack('>IIBB', data[:10])
        elif typ == b'IDAT':
            idat += data
        pos += 12 + ln
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    raw = zlib.decompress(idat)
    rows, prev, i = [], bytearray(w * ch), 0
    for _ in range(h):
        f = raw[i]; i += 1
        line = bytearray(raw[i:i + w * ch]); i += w * ch
        for x in range(len(line)):
            a = line[x - ch] if x >= ch else 0
            b = prev[x]
            c = prev[x - ch] if x >= ch else 0
            if f == 1:   line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        rows.append(bytes(line)); prev = line
    return w, h, ch, rows


def alpha_at(rows, ch, x, y):
    return rows[y][x * ch + 3] if ch == 4 else 255


def find_badges(w, h, ch, rows):
    """Each of the eight columns holds the same badge twice, small above large,
    and the two touch vertically -- so they are found per column and the taller
    copy is kept, rather than by splitting the sheet into rows."""
    cols = [any(alpha_at(rows, ch, x, y) > 24 for y in range(h)) for x in range(w)]
    spans, x = [], 0
    while x < w:
        if cols[x]:
            x0 = x
            while x < w and cols[x]:
                x += 1
            if x - x0 > 8:
                spans.append((x0, x))
        else:
            x += 1
    # Kanto's eight columns are cleanly separated; Johto's and Hoenn's are not,
    # and two neighbours can touch into one wide span. Split anything much wider
    # than the typical column rather than demanding a layout only Kanto has.
    if len(spans) > 8:
        spans.sort(key=lambda sp: sp[1] - sp[0], reverse=True)
        spans = sorted(spans[:8])
    guard = 0
    while len(spans) < 8 and guard < 16:
        guard += 1
        widths = sorted(b - a for a, b in spans)
        med = widths[len(widths) // 2]
        wi = max(range(len(spans)), key=lambda i: spans[i][1] - spans[i][0])
        a, b = spans[wi]
        parts = max(2, int(round((b - a) / float(med))))
        parts = min(parts, 8 - len(spans) + 1)
        step = (b - a) // parts
        spans[wi:wi + 1] = [(a + k * step, a + (k + 1) * step if k < parts - 1 else b)
                            for k in range(parts)]
    if len(spans) != 8:
        raise SystemExit('expected 8 badge columns, found %d' % len(spans))

    boxes = []
    for (x0, x1) in spans:
        occ = [any(alpha_at(rows, ch, x, y) > 24 for x in range(x0, x1))
               for y in range(h)]
        bands, y = [], 0
        while y < h:
            if occ[y]:
                yy = y
                while y < h and occ[y]:
                    y += 1
                bands.append((yy, y))
            else:
                y += 1
        by0, by1 = max(bands, key=lambda b: b[1] - b[0])
        # the two copies touch, so the merged band is split and the lower
        # (larger) half taken when no gap separated them
        if (by1 - by0) > (x1 - x0) * 1.4:
            by0 = by0 + (by1 - by0) // 3
        boxes.append((x0, x1, by0, by1))
    return boxes


def downscale(rows, ch, x0, x1, y0, y1, size):
    """Box filter to size x size, keeping the badge square and centred so the
    proportions survive -- these are not all the same shape."""
    bw, bh = x1 - x0, y1 - y0
    side = max(bw, bh)
    ox = x0 - (side - bw) // 2
    oy = y0 - (side - bh) // 2
    out = []
    for j in range(size):
        row = []
        for i in range(size):
            sx0 = ox + side * i // size
            sx1 = max(sx0 + 1, ox + side * (i + 1) // size)
            sy0 = oy + side * j // size
            sy1 = max(sy0 + 1, oy + side * (j + 1) // size)
            r = g = b = a = n = 0
            for sy in range(sy0, sy1):
                if sy < 0 or sy >= len(rows):
                    continue
                line = rows[sy]
                for sx in range(sx0, sx1):
                    if sx < 0 or sx * ch + ch > len(line):
                        continue
                    al = line[sx * ch + 3] if ch == 4 else 255
                    # premultiply, or the transparent margins wash the edges pale
                    r += line[sx * ch] * al // 255
                    g += line[sx * ch + 1] * al // 255
                    b += line[sx * ch + 2] * al // 255
                    a += al
                    n += 1
            if not n:
                row.append(None); continue
            a //= n
            if a < 110:
                row.append(None)          # transparent
            else:
                row.append((r * 255 // (n * a) if a else 0,
                            g * 255 // (n * a) if a else 0,
                            b * 255 // (n * a) if a else 0))
        out.append(row)
    return out


def quantise(px, maxc=15):
    """Down to maxc colours in RGB565, most frequent kept, the rest snapped to
    the nearest survivor. Badge art is flat enough that this is invisible."""
    freq = {}
    for row in px:
        for p in row:
            if p is None:
                continue
            c = ((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3)
            freq[c] = freq.get(c, 0) + 1
    keep = [c for c, _ in sorted(freq.items(), key=lambda kv: -kv[1])[:maxc]]

    def near(c):
        r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
        best, bd = keep[0], 1 << 30
        for k in keep:
            kr, kg, kb = (k >> 11) & 31, (k >> 5) & 63, k & 31
            d = (r - kr) ** 2 + ((g - kg) // 2) ** 2 + (b - kb) ** 2
            if d < bd:
                bd, best = d, k
        return best

    lut = {c: keep.index(near(c)) for c in freq}
    idx = []
    for row in px:
        for p in row:
            if p is None:
                idx.append(0xFF)
            else:
                c = ((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3)
                idx.append(lut[c])
    return keep, idx


def render(svg_path, png):
    subprocess.run(['rsvg-convert', '-w', str(RENDER_W), svg_path, '-o', png],
                   check=True)
    w, h, ch, rows = decode_png(png)
    boxes = find_badges(w, h, ch, rows)
    return w, h, ch, rows, boxes


def source_for(region):
    """A local SVG if one was given, else the upstream one."""
    local = os.path.join(HERE, '%s.svg' % region)
    if os.path.exists(local):
        return local
    dst = os.path.join(HERE, '_%s.svg' % region.lower())
    r = subprocess.run(['curl', '-fsSL', SVG_BASE % region], capture_output=True)
    if r.returncode != 0:
        raise SystemExit('cannot fetch %s' % (SVG_BASE % region))
    open(dst, 'wb').write(r.stdout)
    return dst


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else None
    regions = REGIONS if not only else [os.path.basename(only).split('.')[0]]

    out = ['// GENERADO por tools/gen_badges.py - no editar',
           '//',
           '// Gym badges for every region. Source art: SteGriff/pokemon-badges,',
           '// Stephen Griffiths 2011, CC BY 3.0, traced from Bulbapedia. See',
           '// CREDITS.md -- the attribution has to ship with anything built',
           '// from this.',
           '#pragma once', '#include <stdint.h>', '',
           '#define BADGE_PX %d' % SIZE,
           '#define BADGE_REGIONS %d' % len(regions), '']
    tmp = []
    for ri, region in enumerate(regions):
        svg = only if only else source_for(region)
        png = os.path.join(HERE, '_badge_render.png')
        w, h, ch, rows, boxes = render(svg, png)
        print('%s: sheet %dx%d, %d badges found' % (region, w, h, len(boxes)))
        if len(boxes) < 8:
            raise SystemExit('%s: only %d badges isolated, expected 8' % (region, len(boxes)))
        for i, (x0, x1, y0, y1) in enumerate(boxes[:8]):
            px = downscale(rows, ch, x0, x1, y0, y1, SIZE)
            pal, idx = quantise(px)
            sym = '%s_%d' % (region.upper(), i)
            out.append('// %s %s' % (region, NAMES[i] if ri == 0 else i + 1))
            out.append('static const uint16_t BADGE_%s_PAL[%d] = { %s };'
                       % (sym, len(pal), ', '.join('0x%04X' % c for c in pal)))
            out.append('static const uint8_t BADGE_%s_IDX[%d] = {' % (sym, len(idx)))
            for r in range(0, len(idx), 32):
                out.append('  ' + ','.join(str(v) for v in idx[r:r + 32]) + ',')
            out.append('};')
        tmp.append(region)
        os.remove(png)
    out.append('')
    out.append('struct BadgeArt { const uint16_t *pal; const uint8_t *idx; };')
    out.append('static const BadgeArt BADGES_ART[BADGE_REGIONS][8] = {')
    for region in tmp:
        out.append('  { ' + ' '.join('{ BADGE_%s_%d_PAL, BADGE_%s_%d_IDX },'
                                     % (region.upper(), i, region.upper(), i)
                                     for i in range(8)) + ' },')
    out.append('};')
    open(OUT, 'w').write('\n'.join(out) + '\n')
    print('wrote %s (%d regions x 8 at %dx%d)'
          % (os.path.normpath(OUT), len(tmp), SIZE, SIZE))


if __name__ == '__main__':
    main()
