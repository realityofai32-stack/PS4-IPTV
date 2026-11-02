"""Generates the V7 IPTV icon0.png (512x512 RGB) - standard library only, 4x supersampled.

The V7 symbol (the same three polygons as ui::BrandMark, src/ui/components.cpp, on its 100 x 80 grid) centred on a
deep-navy gradient: the V's left arm in light blue, its right arm - the stem of the 7 - and the 7's top bar in
V7 Blue, both with a soft top-to-bottom shading. No text, no third-party artwork.
Usage: python -I assets/make_icon.py assets/icon0.png
"""
import struct
import sys
import zlib

W = H = 512
SS = 4  # supersampling factor

BG_TOP, BG_BOTTOM = (9, 14, 26), (16, 34, 70)
ARM_TOP, ARM_BOTTOM = (150, 198, 255), (104, 166, 250)      # left arm (the accent's light tone)
STEM_TOP, STEM_BOTTOM = (62, 152, 255), (28, 108, 236)      # right arm + bar (V7 Blue)

# the symbol on its 100 x 80 grid
LEFT = [(0, 0), (22, 0), (58, 80), (36, 80)]
RIGHT = [(36, 80), (58, 80), (100, 0), (78, 0)]
BAR = [(46, 0), (100, 0), (91.5, 16), (46, 16)]

MARK_W = 300                     # drawn width in icon pixels (height = MARK_W * 0.8)
SCALE = MARK_W / 100.0
OX = (W - MARK_W) / 2
OY = (H - MARK_W * 0.8) / 2 + 6  # optically centred (the bar makes the top look heavier)


def lerp(a, b, t):
    t = min(max(t, 0.0), 1.0)
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def inside(poly, x, y):
    # convex polygon, either winding
    sign = 0
    n = len(poly)
    for i in range(n):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % n]
        cross = (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0)
        if cross != 0:
            s = 1 if cross > 0 else -1
            if sign == 0:
                sign = s
            elif s != sign:
                return False
    return True


def sample(x, y):
    # background: vertical gradient with a slight lift towards the centre
    dx, dy = (x - W / 2) / W, (y - H / 2) / H
    glow = max(0.0, 1.0 - (dx * dx + dy * dy) * 3.2) * 0.35
    bg = lerp(BG_TOP, BG_BOTTOM, y / H)
    bg = tuple(c + (40 - c) * glow * 0.4 for c in bg)
    gx, gy = (x - OX) / SCALE, (y - OY) / SCALE
    t = gy / 80.0
    if inside(RIGHT, gx, gy) or inside(BAR, gx, gy):
        return lerp(STEM_TOP, STEM_BOTTOM, t)
    if inside(LEFT, gx, gy):
        return lerp(ARM_TOP, ARM_BOTTOM, t)
    return bg


def main(path):
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        for x in range(W):
            acc = [0.0, 0.0, 0.0]
            for sy in range(SS):
                for sx in range(SS):
                    c = sample(x + (sx + 0.5) / SS, y + (sy + 0.5) / SS)
                    acc[0] += c[0]
                    acc[1] += c[1]
                    acc[2] += c[2]
            raw.extend(bytes(int(round(v / (SS * SS))) for v in acc))

    def chunk(kind, data):
        c = struct.pack('>I', len(data)) + kind + data
        return c + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


if __name__ == '__main__':
    main(sys.argv[1])
