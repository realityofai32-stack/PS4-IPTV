"""Generates icon0.png (512x512 RGB) for the PS4 IPTV playback test - standard library only.

Dark background, orange "play" triangle, and a striped bar marking it as a test build.
Usage: python -I make_icon.py <output.png>
"""
import struct
import sys
import zlib

W = H = 512
BG = (22, 28, 40)
ORANGE = (240, 140, 30)
STRIPE_A = (240, 200, 40)
STRIPE_B = (30, 30, 30)


def pixel(x, y):
    # test-build stripe along the bottom
    if y >= 420:
        return STRIPE_A if ((x + y) // 32) % 2 == 0 else STRIPE_B
    # play triangle: left edge x=170, apex at (370, 210), vertical span 90..330
    if 170 <= x <= 370:
        half = (370 - x) * 120 / 200
        if 210 - half <= y <= 210 + half:
            return ORANGE
    return BG


def png(path):
    raw = bytearray()
    for y in range(H):
        raw.append(0)  # filter: none
        for x in range(W):
            raw.extend(pixel(x, y))

    def chunk(kind, data):
        c = struct.pack('>I', len(data)) + kind + data
        return c + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)

    out = b'\x89PNG\r\n\x1a\n'
    out += chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
    out += chunk(b'IDAT', zlib.compress(bytes(raw), 9))
    out += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(out)


if __name__ == '__main__':
    png(sys.argv[1])
