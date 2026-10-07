"""Generates the PS4 IPTV icon0.png (512x512 RGB) - standard library only, 4x supersampled.

Deep blue gradient, white rounded TV frame with a small stand, blue play triangle inside.
Usage: python -I assets/make_icon.py assets/icon0.png
"""
import struct
import sys
import zlib

W = H = 512
SS = 4  # supersampling factor


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


TOP, BOTTOM = (16, 28, 52), (24, 70, 150)
WHITE, ACCENT = (240, 244, 250), (72, 149, 255)


def rounded_rect(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def in_triangle(x, y, a, b, c):
    def side(p, q, r):
        return (p[0] - r[0]) * (q[1] - r[1]) - (q[0] - r[0]) * (p[1] - r[1])
    d1, d2, d3 = side((x, y), a, b), side((x, y), b, c), side((x, y), c, a)
    neg = d1 < 0 or d2 < 0 or d3 < 0
    pos = d1 > 0 or d2 > 0 or d3 > 0
    return not (neg and pos)


def sample(x, y):
    bg = lerp(TOP, BOTTOM, y / H)
    outer = rounded_rect(x, y, 86, 118, 426, 352, 34)
    inner = rounded_rect(x, y, 106, 138, 406, 332, 20)
    if outer and not inner:
        return WHITE
    if inner:
        if in_triangle(x, y, (226, 178), (226, 292), (318, 235)):
            return ACCENT
        return lerp((10, 18, 34), (14, 30, 62), (y - 138) / 194)
    if rounded_rect(x, y, 236, 352, 276, 386, 0) or rounded_rect(x, y, 186, 382, 326, 398, 8):
        return WHITE
    return bg


def main(path):
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        for x in range(W):
            acc = [0, 0, 0]
            for sy in range(SS):
                for sx in range(SS):
                    c = sample(x + (sx + 0.5) / SS, y + (sy + 0.5) / SS)
                    acc[0] += c[0]
                    acc[1] += c[1]
                    acc[2] += c[2]
            raw.extend(bytes(v // (SS * SS) for v in acc))

    def chunk(kind, data):
        c = struct.pack('>I', len(data)) + kind + data
        return c + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


if __name__ == '__main__':
    main(sys.argv[1])
