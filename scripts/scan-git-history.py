#!/usr/bin/env python3
"""Scans the complete Git history for private IPTV credentials - without ever printing them.

Secrets are read from the git-ignored local files:
  config/test_streams.txt      KEY=<authenticated URL> lines: the URL, its host, username and password
  config/forbidden_strings.txt one literal per line (any other private value: profile names, hosts, ...)
Every object reachable from any ref or the reflog (plus dangling objects) is searched: file contents in every
commit, commit messages, tag messages. Additionally, authenticated-looking Xtream URLs
(/live|movie|series/<user>/<pass>/<id>), M3U stream lines from Xtream panels (host/<user>/<pass>/<id>) and URLs
with credential / token query parameters are reported unless they use the documented placeholder or example
hosts and values, and every committed .m3u / .m3u8 file outside tests/fixtures/m3u/ is reported (private
playlists must never be committed; the fixtures use example hosts only). Output names the object, its path and the commits that contain it; the secret itself is never shown.

Exit code: 0 clean, 1 findings, 2 no secret source available (nothing to compare against).
"""
import os
import re
import subprocess
import sys

PLACEHOLDER_HOSTS = (b'example.com', b'example.net', b'example.org', b'host:', b'127.0.0.1', b'localhost')
PLACEHOLDER_VALUES = {b'username', b'password', b'user', b'pass', b'u', b'p', b'user%20name', b'p%40ss%2fword',
                      b'myuser42', b's3cretpass', b'alice', b'pw', b'ab', b'cd', b'zzunknownuser', b'zzunknownpw',
                      b'exampleuser', b'examplepass', b'exampletoken', b'm3uuser42', b'm3upass42', b'm3uuser77',
                      b'm3upass77', b'secret'}
FIXTURE_DIR = 'tests/fixtures/m3u/'



def git(*args, data=None):
    return subprocess.run(['git', *args], input=data, capture_output=True, check=False).stdout


def load_secrets(root):
    secrets = {}
    path = os.path.join(root, 'config', 'test_streams.txt')
    if os.path.exists(path):
        for line in open(path, encoding='utf-8', errors='replace'):
            line = line.strip()
            if not line or line.startswith('#') or '=' not in line:
                continue
            key, value = line.split('=', 1)
            value = value.strip()
            if '<' in value or len(value) < 4:
                continue
            secrets['%s url' % key] = value
            m = re.search(r'https?://([^/:]+)(?::\d+)?/(?:live|movie|series|timeshift)/([^/]+)/([^/]+)/', value)
            if m:
                secrets['%s host' % key] = m.group(1)
                secrets['%s username' % key] = m.group(2)
                secrets['%s password' % key] = m.group(3)
    path = os.path.join(root, 'config', 'forbidden_strings.txt')
    if os.path.exists(path):
        for i, line in enumerate(open(path, encoding='utf-8', errors='replace')):
            line = line.strip()
            if line and not line.startswith('#') and len(line) >= 4:
                secrets['forbidden string #%d' % (i + 1)] = line
    return {k: v.encode() for k, v in secrets.items() if len(v) >= 4}


def main():
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else '.')
    os.chdir(root)
    secrets = load_secrets(root)
    paths = {}
    for line in git('rev-list', '--all', '--reflog', '--objects').decode('utf-8', 'replace').splitlines():
        sha, _, path = line.partition(' ')
        paths[sha] = path or '<commit/tag>'
    for line in git('fsck', '--unreachable', '--no-reflogs').decode('utf-8', 'replace').splitlines():
        parts = line.split()
        if len(parts) >= 3:
            paths.setdefault(parts[2], '<unreachable>')
    data = git('cat-file', '--batch', data=('\n'.join(paths) + '\n').encode())
    url_re = re.compile(rb'(https?://[^\s/"\']+)?/(live|movie|series)/([A-Za-z0-9_.%@-]+)/([A-Za-z0-9_.%@-]+)/\d+')
    # M3U lines of Xtream panels: http://host:port/<user>/<pass>/<id>[.ext] (no /live/ marker)
    m3u_re = re.compile(rb'(https?://[^\s/"\'<>]+)/([A-Za-z0-9_.%@-]+)/([A-Za-z0-9_.%@-]+)/\d+(?:\.[a-z0-9]{1,5})?(?=[\s"\'<>]|$)')
    # credentials / tokens in query strings
    query_re = re.compile(rb'(https?://[^\s/"\'?<>]+)[^\s"\'<>]*[?&](username|password|token|auth|signature|key)=([^&\s"\'<>]{3,})')
    findings = []
    pos = 0
    objects = 0
    while pos < len(data):
        nl = data.index(b'\n', pos)
        head = data[pos:nl].split()
        if len(head) < 3:
            pos = nl + 1
            continue
        sha, kind, size = head[0].decode(), head[1].decode(), int(head[2])
        body = data[nl + 1:nl + 1 + size]
        pos = nl + 2 + size
        objects += 1
        if kind == 'tree':
            continue
        for name, value in secrets.items():
            if value in body:
                findings.append((sha, kind, paths.get(sha, '?'), name))
        for m in url_re.finditer(body):
            host, user, password = m.group(1) or b'', m.group(3).lower(), m.group(4).lower()
            if any(h in host for h in PLACEHOLDER_HOSTS) or (user in PLACEHOLDER_VALUES and password in PLACEHOLDER_VALUES):
                continue
            if not host and (user in PLACEHOLDER_VALUES or password in PLACEHOLDER_VALUES):
                continue
            findings.append((sha, kind, paths.get(sha, '?'), 'authenticated-looking %s URL' % m.group(2).decode()))
        for m in m3u_re.finditer(body):
            host, user, password = m.group(1), m.group(2).lower(), m.group(3).lower()
            if any(h in host for h in PLACEHOLDER_HOSTS) or user in (b'live', b'movie', b'series'):
                continue
            if user in PLACEHOLDER_VALUES and password in PLACEHOLDER_VALUES:
                continue
            findings.append((sha, kind, paths.get(sha, '?'), 'authenticated-looking M3U stream URL'))
        for m in query_re.finditer(body):
            host, value = m.group(1), m.group(3).lower()
            if any(h in host for h in PLACEHOLDER_HOSTS) or value in PLACEHOLDER_VALUES or value.startswith(b'<'):
                continue
            findings.append((sha, kind, paths.get(sha, '?'), 'URL with a %s parameter' % m.group(2).decode()))
        path = paths.get(sha, '')
        if kind == 'blob' and re.search(r'\.m3u8?$', path, re.I) and not path.startswith(FIXTURE_DIR):
            findings.append((sha, kind, path, 'playlist file outside ' + FIXTURE_DIR))
    print('objects scanned: %d, secret kinds: %d' % (objects, len(secrets)))
    if not secrets:
        print('WARNING: no config/test_streams.txt or config/forbidden_strings.txt: only the URL pattern was checked')
    for sha, kind, path, name in sorted(set(findings)):
        commits = git('log', '--all', '--format=%h', '--find-object=' + sha).decode().split() if kind == 'blob' else [sha[:7]]
        print('FINDING  %-30s %s %s %s  in commits: %s' % (name, kind, sha[:12], path, ' '.join(commits[:10]) or '?'))
    print('RESULT: %s' % ('CLEAN' if not findings else '%d finding(s) - rewrite the history before publishing' % len(findings)))
    return 1 if findings else (2 if not secrets else 0)


if __name__ == '__main__':
    sys.exit(main())
