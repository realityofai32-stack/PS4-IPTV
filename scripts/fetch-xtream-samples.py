"""Fetches real Xtream API responses for host-side parser validation (never committed).

Credentials are taken from config/test_streams.txt (the /live/<user>/<pass>/ URL). Responses are saved under
build/xtream-samples/ (git-ignored) and only shapes/counts are printed - no credentials, no URLs.
Usage: python -I scripts/fetch-xtream-samples.py [actions...]
"""
import gzip
import json
import os
import re
import sys
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'build', 'xtream-samples')


def credentials():
    with open(os.path.join(ROOT, 'config', 'test_streams.txt'), encoding='utf-8-sig') as f:
        for line in f:
            m = re.search(r'(https?://[^/]+)/live/([^/]+)/([^/]+)/', line)
            if m:
                return m.group(1), urllib.parse.unquote(m.group(2)), urllib.parse.unquote(m.group(3))
    raise SystemExit('no Xtream URL in config/test_streams.txt')


def fetch(base, user, pw, action):
    q = {'username': user, 'password': pw}
    if action:
        q['action'] = action
    url = base + '/player_api.php?' + urllib.parse.urlencode(q)
    req = urllib.request.Request(url, headers={'User-Agent': 'PS4IPTV/0.1.0', 'Accept-Encoding': 'gzip'})
    with urllib.request.urlopen(req, timeout=60) as r:
        data = r.read()
        if r.headers.get('Content-Encoding') == 'gzip':
            data = gzip.decompress(data)
        return r.status, r.headers.get('Content-Type'), data


def main():
    actions = sys.argv[1:] or ['', 'get_live_categories', 'get_live_streams']
    base, user, pw = credentials()
    os.makedirs(OUT, exist_ok=True)
    for action in actions:
        status, ctype, data = fetch(base, user, pw, action)
        name = (action or 'auth') + '.json'
        with open(os.path.join(OUT, name), 'wb') as f:
            f.write(data)
        doc = json.loads(data)
        shape = type(doc).__name__
        if isinstance(doc, list):
            keys = sorted(doc[0].keys()) if doc and isinstance(doc[0], dict) else []
            types = {k: type(doc[0][k]).__name__ for k in keys} if keys else {}
            print(f'{name}: HTTP {status} {ctype} {len(data)} bytes, list of {len(doc)}; first item fields: {types}')
        else:
            print(f'{name}: HTTP {status} {ctype} {len(data)} bytes, {shape} keys {sorted(doc.keys())}')


if __name__ == '__main__':
    main()
