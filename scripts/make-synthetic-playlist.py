#!/usr/bin/env python3
"""Writes a large sanitized M3U playlist for navigation / performance tests on the console.

Usage: python -I scripts/make-synthetic-playlist.py <entries> <output.m3u>

Copy the file to /data/PS4IPTV/playlists/ on the PS4 (FTP) and add a source with the playlist address
/data/PS4IPTV/playlists/<file name>. Every stream and logo URL points to example.com / example.net: the
channels can be browsed, searched and favorited, but not played. No real provider data.
"""
import sys

PREFIXES = ['TR: ', 'UK: ', 'DE: ', '|FR| ', 'US: ', 'RU: ', '']
WORDS = ['News', 'Sport', 'Haber', 'Spor', 'Kids', 'Çocuk', 'Film', 'Música', 'Документальный', 'Discovery',
         'Cinema', 'Belgesel', 'Müzik', 'Dizi', 'NHK 総合', 'KBS 한국', 'الجزيرة']


def main():
    n = int(sys.argv[1])
    lines = ['#EXTM3U']
    for i in range(n):
        prefix = PREFIXES[i % len(PREFIXES)]
        name = '%s%s %d%s' % (prefix, WORDS[(i * 5) % len(WORDS)], i % 1000, ' HD' if i % 3 == 0 else '')
        group = '%s %s' % (prefix[:2].strip('|') or 'XX', WORDS[(i // 7) % len(WORDS)])
        logo = '' if i % 10 == 0 else ' tvg-logo="http://img.example.net/logos/%d.png"' % i
        lines.append('#EXTINF:-1 tvg-id="ch%d.example"%s group-title="%s",%s' % (i, logo, group, name))
        lines.append('http://stream.example.com/synthetic/%d.ts' % (100000 + i))
    with open(sys.argv[2], 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print('%d entries written to %s' % (n, sys.argv[2]))


if __name__ == '__main__':
    main()
