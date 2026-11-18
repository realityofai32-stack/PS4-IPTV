#!/usr/bin/env python3
"""HTTP test server for the download integration test (scripts/run-download-integration-test.ps1).

Serves generated files (no fixtures on disk; the same byte pattern as tests/host/download_it.cpp) at Xtream-style
paths:  /<mode>/movie/<user>/<pass>/<id>.<ext>   /<mode>/series/<user>/<pass>/<id>.<ext>
The file size is the id in bytes ("3000000.mkv" = 3,000,000 bytes); mode "big" serves 5 GiB + id bytes.

Modes:
  plain     Content-Length, Range (206 + Content-Range), If-Range, ETag, Last-Modified
  redirect  302 to the same file under /plain
  norange   ignores Range: always 200 with the whole file
  flaky     the first request of each path sends half of the body, then drops the connection
  forbid1   the first request of each path answers 403
  nolength  no Content-Length (body ends when the connection closes), no Range
  slow      like plain, sent in 32 KB pieces with a pause between them
  big       5 GiB + id bytes (only the requested range is generated)
  missing   404
Playlists (M3U fetch test): /m3u/<mode>/<entries>.m3u with mode
  plain     generated Extended M3U (Content-Length)
  gzip      the same, Content-Encoding: gzip when the client accepts it
  redirect  302 to /m3u/plain/...
  slow      the same in 8 KB pieces with a pause (cancellation)
  ua        403 unless the User-Agent is "ExamplePlaylistAgent/1.0"
  html      200 with an HTML page (not a playlist)
  missing   404
XMLTV guides (programme guide test): /xmltv/<mode>/<channels>.xml with channels ch0.example ... (half-hour
programmes from two hours ago to two days ahead, generated from the current time) and mode
  plain     application/xml
  gzipfile  the same as a .gz file (application/gzip, no Content-Encoding)
  encoded   Content-Encoding: gzip when the client accepts it
  slow      plain in 8 KB pieces with a pause (cancellation)
  html      200 with an HTML page (not a guide)
  missing   404
A Jellyfin / Emby media server (media server test): /jf/... with user "exampleuser" / "examplepass" (token
"exampletoken-jf"): System/Info/Public, POST Users/AuthenticateByName, Users/u1/Views (a movie library of 1203
movies, a series library of 3 series, a music library), Users/u1/Items (paged), Users/u1/Items/<id>,
Shows/<id>/Episodes and POST Sessions/Playing[/Progress|/Stopped] (bodies logged). Every request but the public
information and the sign-in needs the token. /jfmoved/System/Info/Public redirects to /jf/... (the server moved).
A Stremio add-on (add-on test): /addon/manifest.json with a movie catalog of 237 titles in pages of 100 ("skip"), a
series catalog that ignores "skip" (the same page again), metas, and streams (a torrent, then a direct address on
this server); /addoncfg/manifest.json needs configuration first.
Failures (network failure test, tests/host/failure_it.cpp): /fail/<mode>/<anything> (GET or POST) with mode
  reset      headers + part of the body, then the connection is reset (RST)
  truncated  Content-Length 5000, 100 bytes, then the connection closes
  stall      headers + 10 bytes, then silence for 15 s
  slow       a valid JSON body in small pieces over ~6 s (cancellation)
  loop       a redirect to itself
  garbage    200 with binary bytes; html: 200 with an HTML page; empty: 200 with no body
  big        200 announcing and streaming 80 MiB
  statusNNN  that HTTP status
XMLTV and playlists also have mode "chunked" (Transfer-Encoding: chunked); XMLTV mode "expired" ends every
programme an hour ago. An Xtream API (/xt/player_api.php, exampleuser / examplepass): account, live categories
and streams. Media server variants: /jfempty (no libraries), /jfodd (malformed items, no artwork); /_jfexpire
(the token stops working), /_jfdown (every /jf* request is reset). Add-on variants: /addonbad and /addonnotjson
(invalid manifests), /addonempty (empty catalogs), /addonfail (metas and streams answer 500), /addontorrent and
/addonyt (only torrent / YouTube streams), /addonmulti (several streams: the first direct one is used);
/_addondown (every add-on request answers 503).
Control: /_log (JSON list of requests), /_reset (also ends the states above). Listens on 127.0.0.1, port written
to the file in argv[1].
"""
import gzip
import json
import socket
import struct
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LOG = []
SEEN = set()
LOCK = threading.Lock()
STATE = {'jf_expired': False, 'jf_down': False, 'addon_down': False}
JF_VARIANTS = ('jf', 'jfmoved', 'jfempty', 'jfodd')
ADDON_VARIANTS = ('addon', 'addoncfg', 'addonbad', 'addonnotjson', 'addonempty', 'addonfail', 'addontorrent',
                  'addonyt', 'addonmulti')


def playlist(n):
    lines = ['#EXTM3U']
    for i in range(n):
        group = 'Group %d' % (i % 40)
        lines.append('#EXTINF:-1 tvg-id="ch%d.example" tvg-logo="http://img.example.com/%d.png" group-title="%s",'
                     'Channel %d' % (i, i, group, i))
        lines.append('http://stream.example.com/exampleuser/examplepass/%d.ts' % (100000 + i))
    return ('\n'.join(lines) + '\n').encode('utf-8')


def xmltv(n, shift=0):
    now = int(time.time()) // 1800 * 1800 + shift
    fmt = lambda t: time.strftime('%Y%m%d%H%M%S +0000', time.gmtime(t))
    out = ['<?xml version="1.0" encoding="UTF-8"?>', '<!DOCTYPE tv SYSTEM "xmltv.dtd">', '<tv generator-info-name="t">']
    for i in range(n):
        out.append('<channel id="ch%d.example"><display-name>Channel %d</display-name></channel>' % (i, i))
    for i in range(n):
        for k in range(-4, 96):
            start = now + k * 1800
            out.append('<programme start="%s" stop="%s" channel="ch%d.example"><title lang="en">Show %d-%d &amp; co'
                       '</title><desc lang="en">About show %d of channel %d.</desc></programme>'
                       % (fmt(start), fmt(start + 1800), i, i, k, k, i))
    out.append('</tv>')
    return ('\n'.join(out) + '\n').encode('utf-8')


JF_TOKEN = 'exampletoken-jf'
JF_MOVIES = 1203


def jf_movie(i):
    return {'Name': 'Movie %04d' % i, 'Id': 'm%d' % i, 'Type': 'Movie', 'ProductionYear': 1990 + i % 30,
            'CommunityRating': 5 + (i % 50) / 10.0, 'DateCreated': '2024-03-01T12:00:00.0000000Z',
            'Container': 'mkv' if i % 2 else 'mov,mp4,m4a,3gp,3g2,mj2', 'ImageTags': {'Primary': 't%d' % i}}


def byte_at(i):
    return (i * 131 + (i >> 13) + 7) & 0xFF


def generate(start, n):
    return bytes(byte_at(i) for i in range(start, start + n))


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, fmt, *args):
        pass

    def send_body(self, start, end, chunk=256 * 1024, delay=0.0, limit=None):
        pos = start
        sent = 0
        while pos <= end:
            n = min(chunk, end - pos + 1)
            if limit is not None and sent + n > limit:
                n = limit - sent
                if n > 0:
                    self.wfile.write(generate(pos, n))
                    self.wfile.flush()
                return False
            try:
                self.wfile.write(generate(pos, n))
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                return False
            pos += n
            sent += n
            if delay:
                time.sleep(delay)
        return True

    def do_GET(self):
        path = self.path.split('?')[0]
        if path == '/_log':
            with LOCK:
                body = json.dumps(LOG).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path in ('/_reset', '/_jfexpire', '/_jfdown', '/_addondown'):
            with LOCK:
                if path == '/_reset':
                    LOG.clear()
                    SEEN.clear()
                    for k in STATE:
                        STATE[k] = False
                else:
                    STATE[{'/_jfexpire': 'jf_expired', '/_jfdown': 'jf_down', '/_addondown': 'addon_down'}[path]] = True
            self.send_response(204)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        parts = path.strip('/').split('/')
        if parts and parts[0] == 'fail':
            self.serve_failure(parts[1] if len(parts) > 1 else '')
            return
        if parts and parts[0] == 'xt':
            self.serve_xtream(urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query))
            return
        if parts and parts[0] in JF_VARIANTS:
            self.serve_jellyfin('GET', parts, b'')
            return
        if parts and parts[0] in ADDON_VARIANTS:
            self.serve_addon([urllib.parse.unquote(x) for x in parts])
            return
        if len(parts) == 3 and parts[0] == 'xmltv':
            with LOCK:
                LOG.append({'path': path, 'agent': self.headers.get('User-Agent', ''),
                            'encoding': self.headers.get('Accept-Encoding', '')})
            self.serve_xmltv(parts[1], parts[2])
            return
        if len(parts) == 3 and parts[0] == 'm3u':
            with LOCK:
                LOG.append({'path': path, 'agent': self.headers.get('User-Agent', ''),
                            'encoding': self.headers.get('Accept-Encoding', '')})
            self.serve_playlist(parts[1], parts[2])
            return
        rng = self.headers.get('Range', '')
        if_range = self.headers.get('If-Range', '')
        with LOCK:
            LOG.append({'path': path, 'range': rng, 'ifRange': if_range})
            first = path not in SEEN
            SEEN.add(path)
        if len(parts) != 5 or parts[1] not in ('movie', 'series'):
            self.reply_status(404)
            return
        mode = parts[0]
        file_id = parts[4].split('.')[0]
        if mode == 'missing' or not file_id.isdigit():
            self.reply_status(404)
            return
        if mode == 'redirect':
            self.send_response(302)
            self.send_header('Location', '/plain/' + '/'.join(parts[1:]))
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        if mode == 'forbid1' and first:
            self.reply_status(403)
            return
        size = int(file_id) + ((5 << 30) if mode == 'big' else 0)
        etag = '"%s-%d"' % (file_id, size)
        start = 0
        ranged = False
        if rng.startswith('bytes=') and rng.endswith('-') and mode not in ('norange', 'nolength'):
            offset = int(rng[6:-1])
            if not if_range or if_range == etag:
                if offset >= size:
                    self.send_response(416)
                    self.send_header('Content-Range', 'bytes */%d' % size)
                    self.send_header('Content-Length', '0')
                    self.end_headers()
                    return
                start = offset
                ranged = True
        if mode == 'nolength':
            self.send_response(200)
            self.send_header('Content-Type', 'video/x-matroska')
            self.send_header('Connection', 'close')
            self.end_headers()
            self.send_body(0, size - 1)
            self.close_connection = True
            return
        self.send_response(206 if ranged else 200)
        self.send_header('Content-Type', 'video/x-matroska')
        self.send_header('Accept-Ranges', 'none' if mode == 'norange' else 'bytes')
        self.send_header('ETag', etag)
        self.send_header('Last-Modified', 'Wed, 01 Oct 2025 10:00:00 GMT')
        if ranged:
            self.send_header('Content-Range', 'bytes %d-%d/%d' % (start, size - 1, size))
        self.send_header('Content-Length', str(size - start))
        self.end_headers()
        if mode == 'flaky' and first:
            self.send_body(start, size - 1, limit=(size - start) // 2)
            self.close_connection = True
            return
        if mode == 'slow':
            self.send_body(start, size - 1, chunk=32 * 1024, delay=0.02)
        else:
            self.send_body(start, size - 1)

    def serve_playlist(self, mode, name):
        if mode == 'missing':
            self.reply_status(404)
            return
        if mode == 'redirect':
            self.send_response(302)
            self.send_header('Location', '/m3u/plain/' + name)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        if mode == 'ua' and self.headers.get('User-Agent', '') != 'ExamplePlaylistAgent/1.0':
            self.reply_status(403)
            return
        if mode == 'html':
            body = b'<!DOCTYPE html><html><body>Not a playlist</body></html>'
        else:
            n = int(name.split('.')[0]) if name.split('.')[0].isdigit() else 0
            body = playlist(n)
        if mode == 'chunked':
            self.send_response(200)
            self.send_header('Content-Type', 'audio/x-mpegurl')
            self.send_chunked(body)
            return
        encoded = mode == 'gzip' and 'gzip' in self.headers.get('Accept-Encoding', '')
        if encoded:
            body = gzip.compress(body)
        self.send_response(200)
        self.send_header('Content-Type', 'audio/x-mpegurl')
        if encoded:
            self.send_header('Content-Encoding', 'gzip')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        try:
            if mode == 'slow':
                for i in range(0, len(body), 8192):
                    self.wfile.write(body[i:i + 8192])
                    self.wfile.flush()
                    time.sleep(0.05)
            else:
                self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def serve_xmltv(self, mode, name):
        if mode == 'missing':
            self.reply_status(404)
            return
        if mode == 'html':
            body = b'<!DOCTYPE html><html><body>Not a guide</body></html>'
            ctype = 'text/html'
        else:
            n = int(name.split('.')[0]) if name.split('.')[0].isdigit() else 0
            body = xmltv(n, -(2 * 86400) if mode == 'expired' else 0)
            ctype = 'application/xml'
        if mode == 'chunked':
            self.send_response(200)
            self.send_header('Content-Type', ctype)
            self.send_chunked(body)
            return
        encoded = mode == 'encoded' and 'gzip' in self.headers.get('Accept-Encoding', '')
        if mode == 'gzipfile' or encoded:
            body = gzip.compress(body)
        if mode == 'gzipfile':
            ctype = 'application/gzip'
        self.send_response(200)
        self.send_header('Content-Type', ctype)
        if encoded:
            self.send_header('Content-Encoding', 'gzip')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        try:
            if mode == 'slow':
                for i in range(0, len(body), 8192):
                    self.wfile.write(body[i:i + 8192])
                    self.wfile.flush()
                    time.sleep(0.05)
            else:
                self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def do_POST(self):
        length = int(self.headers.get('Content-Length', '0') or 0)
        body = self.rfile.read(length) if length > 0 else b''
        parts = self.path.split('?')[0].strip('/').split('/')
        if parts and parts[0] == 'fail':
            self.serve_failure(parts[1] if len(parts) > 1 else '')
            return
        if parts and parts[0] in JF_VARIANTS:
            self.serve_jellyfin('POST', parts, body)
            return
        self.reply_status(404)

    def reply_json(self, status, value):
        body = json.dumps(value).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def serve_jellyfin(self, method, parts, body):
        auth = self.headers.get('Authorization', '') + ' ' + self.headers.get('X-Emby-Authorization', '')
        token = self.headers.get('X-Emby-Token', '')
        authorized = (token == JF_TOKEN or ('Token="%s"' % JF_TOKEN) in auth) and not STATE['jf_expired']
        query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
        entry = {'path': '/' + '/'.join(parts), 'method': method, 'client': 'MediaBrowser Client="V7 Player"' in auth,
                 'device': 'DeviceId="v7-' in auth, 'authorized': authorized}
        rest = parts[1:]
        if method == 'POST' and rest[:1] == ['Sessions']:
            entry['body'] = json.loads(body.decode('utf-8') or '{}')
        with LOCK:
            LOG.append(entry)
        if STATE['jf_down']:
            self.reset_connection()
            return
        variant = parts[0]
        if parts[0] == 'jfmoved':
            self.send_response(302)
            self.send_header('Location', '/jf/' + '/'.join(rest))
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        if rest == ['System', 'Info', 'Public']:
            self.reply_json(200, {'ServerName': 'test', 'Version': '10.10.3', 'ProductName': 'Jellyfin Server',
                                  'Id': 'srv1'})
            return
        if method == 'POST' and rest == ['Users', 'AuthenticateByName']:
            try:
                login = json.loads(body.decode('utf-8'))
            except ValueError:
                login = {}
            if login.get('Username') == 'exampleuser' and login.get('Pw') == 'examplepass' and entry['client']:
                self.reply_json(200, {'User': {'Name': 'exampleuser', 'Id': 'u1'}, 'AccessToken': JF_TOKEN,
                                      'ServerId': 'srv1'})
            else:
                self.reply_status(401)
            return
        if not authorized:
            self.reply_status(401)
            return
        if method == 'POST' and rest[:1] == ['Sessions']:
            self.send_response(204)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        if variant == 'jfempty' and rest[:3] == ['Users', 'u1', 'Views']:
            self.reply_json(200, {'Items': [], 'TotalRecordCount': 0})
            return
        if variant == 'jfodd' and rest == ['Users', 'u1', 'Views']:
            self.reply_json(200, {'Items': [{'Id': 'l1', 'Name': 'Movies', 'CollectionType': 'movies'},
                                            {'Name': 'No id', 'CollectionType': 'movies'}, 'not an object'],
                                  'TotalRecordCount': 'three'})
            return
        if variant == 'jfodd' and rest == ['Users', 'u1', 'Items']:
            start = int(query.get('StartIndex', ['0'])[0])
            items = [{'Name': 12345, 'Id': 'odd1', 'Type': 'Movie', 'RunTimeTicks': 'abc', 'ProductionYear': 'x'},
                     {'Name': 'No id', 'Type': 'Movie'}, {'Id': 'odd3', 'Type': 'Movie'}, None, [1, 2],
                     {'Name': 'Fine', 'Id': 'odd4', 'Type': 'Movie', 'Container': 'mkv', 'ImageTags': None},
                     {'Name': 'Fine too', 'Id': 'odd5', 'Type': 'Movie', 'ImageTags': {'Primary': 7}}]
            # a server that announces more than it has: paging must still end
            self.reply_json(200, {'Items': items if start == 0 else [], 'TotalRecordCount': 5000, 'StartIndex': start})
            return
        if variant == 'jfodd' and len(rest) == 4 and rest[:3] == ['Users', 'u1', 'Items']:
            self.reply_json(200, {'Name': 'Fine', 'Id': rest[3], 'Overview': None, 'Genres': 'not a list',
                                  'RunTimeTicks': -5, 'UserData': 'none'})
            return
        if rest == ['Users', 'u1', 'Views']:
            self.reply_json(200, {'Items': [{'Id': 'l1', 'Name': 'Movies', 'CollectionType': 'movies'},
                                            {'Id': 'l2', 'Name': 'Shows', 'CollectionType': 'tvshows'},
                                            {'Id': 'l3', 'Name': 'Music', 'CollectionType': 'music'}],
                                  'TotalRecordCount': 3})
            return
        if rest == ['Users', 'u1', 'Items']:
            parent = query.get('ParentId', [''])[0]
            kind = query.get('IncludeItemTypes', [''])[0]
            start = int(query.get('StartIndex', ['0'])[0])
            limit = int(query.get('Limit', ['100'])[0])
            if parent == 'l1' and kind == 'Movie':
                items = [jf_movie(i) for i in range(start, min(JF_MOVIES, start + limit))]
                total = JF_MOVIES
            elif parent == 'l2' and kind == 'Series':
                items = [{'Name': 'Show %d' % i, 'Id': 's%d' % i, 'Type': 'Series', 'ProductionYear': 2019,
                          'ImageTags': {'Primary': 'st%d' % i}} for i in range(3)][start:start + limit]
                total = 3
            else:
                items, total = [], 0
            self.reply_json(200, {'Items': items, 'TotalRecordCount': total, 'StartIndex': start})
            return
        if len(rest) == 4 and rest[:3] == ['Users', 'u1', 'Items']:
            item = rest[3]
            if item.startswith('m'):
                detail = jf_movie(int(item[1:]))
                detail.update({'Overview': 'About %s.' % item, 'RunTimeTicks': 72000000000, 'Genres': ['Drama'],
                               'UserData': {'PlaybackPositionTicks': 18000000000}})
                self.reply_json(200, detail)
            elif item.startswith('s'):
                self.reply_json(200, {'Name': 'Show', 'Id': item, 'Overview': 'A show.', 'Genres': ['Drama']})
            else:
                self.reply_status(404)
            return
        if len(rest) == 3 and rest[0] == 'Shows' and rest[2] == 'Episodes' and query.get('UserId') == ['u1']:
            eps = [{'Id': 'e%d%d' % (s, e), 'Name': 'Episode %d' % e, 'IndexNumber': e, 'ParentIndexNumber': s,
                    'SeasonName': 'Season %d' % s, 'Container': 'mkv', 'RunTimeTicks': 26000000000}
                   for s in (2, 1) for e in (3, 1, 2)]
            eps[0]['UserData'] = {'PlaybackPositionTicks': 6000000000}
            self.reply_json(200, {'Items': eps, 'TotalRecordCount': len(eps)})
            return
        self.reply_status(404)

    def serve_addon(self, parts):
        with LOCK:
            LOG.append({'path': '/' + '/'.join(parts), 'addon': True})
        if STATE['addon_down']:
            self.reply_status(503)
            return
        variant = parts[0]
        rest = parts[1:]
        if rest == ['manifest.json'] and variant == 'addonbad':
            self.reply_json(200, {'name': 'Broken', 'catalogs': 'none'})
            return
        if rest == ['manifest.json'] and variant == 'addonnotjson':
            body = b'<!DOCTYPE html><html><body>Not an add-on</body></html>'
            self.send_response(200)
            self.send_header('Content-Type', 'text/html')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if len(rest) >= 3 and rest[0] == 'catalog' and variant == 'addonempty':
            self.reply_json(200, {'metas': []})
            return
        if len(rest) == 3 and rest[0] in ('meta', 'stream') and variant == 'addonfail':
            self.reply_status(500)
            return
        if len(rest) == 3 and rest[0] == 'stream' and variant in ('addontorrent', 'addonyt', 'addonmulti'):
            origin = 'http://%s:%d' % self.server.server_address
            torrent = {'infoHash': '0123456789abcdef0123456789abcdef01234567', 'name': 'Torrent'}
            youtube = {'ytId': 'dQw4w9WgXcQ', 'name': 'Trailer'}
            streams = {'addontorrent': [torrent, dict(torrent, fileIdx=1)],
                       'addonyt': [youtube, {'ytId': 'aaaaaaaaaaa'}],
                       'addonmulti': [torrent, youtube, {'externalUrl': 'https://watch.example.com/x'},
                                      {'url': origin + '/plain/movie/exampleuser/examplepass/3000.mkv?pick=first',
                                       'name': 'First'},
                                      {'url': 'https://cdn.example.com/second.mp4', 'name': 'Second'}]}[variant]
            self.reply_json(200, {'streams': streams})
            return
        if rest == ['manifest.json']:
            manifest = {'id': 'org.example.test', 'version': '1.0.0', 'name': 'Test add-on',
                        'resources': ['catalog', 'meta', {'name': 'stream', 'types': ['movie', 'series']}],
                        'types': ['movie', 'series'],
                        'catalogs': [{'type': 'movie', 'id': 'top', 'name': 'Top', 'extra': [{'name': 'skip'}]},
                                     {'type': 'series', 'id': 'shows', 'name': 'Shows', 'extra': [{'name': 'skip'}]},
                                     {'type': 'movie', 'id': 'find', 'name': 'Find',
                                      'extra': [{'name': 'search', 'isRequired': True}]}]}
            if variant == 'addoncfg':
                manifest['behaviorHints'] = {'configurable': True, 'configurationRequired': True}
            self.reply_json(200, manifest)
            return
        if len(rest) >= 3 and rest[0] == 'catalog':
            skip = 0
            last = rest[-1][:-5] if rest[-1].endswith('.json') else rest[-1]
            if last.startswith('skip='):
                skip = int(last[5:])
            if rest[1] == 'movie' and rest[2].startswith('top'):
                metas = [{'id': 'tt%d' % (1000000 + i), 'type': 'movie', 'name': 'Film %d' % i,
                          'poster': 'https://img.example.com/%d.jpg' % i, 'releaseInfo': '2001'}
                         for i in range(skip, min(237, skip + 100))]
            elif rest[1] == 'series' and rest[2].startswith('shows'):
                metas = [{'id': 'tt%d' % (2000000 + i), 'type': 'series', 'name': 'Show %d' % i} for i in range(2)]
            else:
                metas = []
            self.reply_json(200, {'metas': metas})
            return
        if len(rest) == 3 and rest[0] == 'meta':
            item = rest[2][:-5]
            if rest[1] == 'movie':
                self.reply_json(200, {'meta': {'id': item, 'type': 'movie', 'name': 'Film', 'description': 'About.',
                                               'runtime': '90 min'}})
            else:
                videos = [{'id': '%s:1:%d' % (item, e), 'title': 'Episode %d' % e, 'season': 1, 'episode': e}
                          for e in (2, 1)]
                self.reply_json(200, {'meta': {'id': item, 'type': 'series', 'name': 'Show', 'videos': videos}})
            return
        if len(rest) == 3 and rest[0] == 'stream':
            item = rest[2][:-5]
            origin = 'http://%s:%d' % self.server.server_address
            self.reply_json(200, {'streams': [
                {'infoHash': '0123456789abcdef0123456789abcdef01234567', 'name': 'Torrent'},
                {'url': origin + '/plain/movie/exampleuser/examplepass/3000.mkv?for=' + urllib.parse.quote(item),
                 'name': 'Direct', 'behaviorHints': {'proxyHeaders': {'request': {'User-Agent': 'ExampleAddonAgent/1.0'}}}}]})
            return
        self.reply_status(404)

    def send_chunked(self, body, size=4096):
        self.send_header('Transfer-Encoding', 'chunked')
        self.end_headers()
        try:
            for i in range(0, len(body), size):
                piece = body[i:i + size]
                self.wfile.write(b'%x\r\n' % len(piece) + piece + b'\r\n')
            self.wfile.write(b'0\r\n\r\n')
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def reset_connection(self):
        # SO_LINGER 0: close() sends a reset instead of an orderly end
        try:
            self.wfile.flush()
            self.connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
            self.connection.close()
        except OSError:
            pass
        self.close_connection = True

    def serve_failure(self, mode):
        with LOCK:
            LOG.append({'path': self.path.split('?')[0], 'fail': mode})
        try:
            if mode in ('reset', 'truncated', 'stall'):
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', '5000')
                self.end_headers()
                self.wfile.write(b'{"items": [' + b'1, ' * 30 if mode != 'stall' else b'{"items": ')
                self.wfile.flush()
                if mode == 'reset':
                    self.reset_connection()
                elif mode == 'stall':
                    time.sleep(15)
                self.close_connection = True
            elif mode == 'slow':
                body = json.dumps({'items': list(range(1500))}).encode()
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                for i in range(0, len(body), 128):
                    self.wfile.write(body[i:i + 128])
                    self.wfile.flush()
                    time.sleep(0.1)
            elif mode == 'loop':
                self.send_response(302)
                self.send_header('Location', self.path)
                self.send_header('Content-Length', '0')
                self.end_headers()
            elif mode in ('garbage', 'html', 'empty'):
                body = {'garbage': bytes(range(256)) * 8,
                        'html': b'<!DOCTYPE html><html><head><title>Login</title></head><body>Please sign in</body></html>',
                        'empty': b''}[mode]
                self.send_response(200)
                self.send_header('Content-Type', 'text/html' if mode == 'html' else 'application/json')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif mode == 'big':
                size = 80 << 20
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(size))
                self.end_headers()
                piece = b'[' + b' ' * ((1 << 20) - 1)
                for _ in range(80):
                    self.wfile.write(piece)
                    piece = b' ' * (1 << 20)
            elif mode.startswith('status') and mode[6:].isdigit():
                self.reply_status(int(mode[6:]))
            else:
                self.reply_status(404)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
            self.close_connection = True

    def serve_xtream(self, query):
        with LOCK:
            LOG.append({'path': '/xt/player_api.php', 'action': query.get('action', [''])[0]})
        if query.get('username') != ['exampleuser'] or query.get('password') != ['examplepass']:
            self.reply_json(200, {'user_info': {'auth': 0}})
            return
        action = query.get('action', [''])[0]
        if action == '':
            self.reply_json(200, {'user_info': {'auth': 1, 'status': 'Active', 'exp_date': '1999999999',
                                                'max_connections': '1', 'active_cons': '0', 'username': 'exampleuser'},
                                  'server_info': {'url': '127.0.0.1', 'port': str(self.server.server_address[1]),
                                                  'server_protocol': 'http', 'timezone': 'UTC'}})
        elif action == 'get_live_categories':
            self.reply_json(200, [{'category_id': '1', 'category_name': 'News', 'parent_id': 0},
                                  {'category_id': '2', 'category_name': 'Sports', 'parent_id': 0}])
        elif action == 'get_live_streams':
            self.reply_json(200, [{'num': i + 1, 'name': 'Channel %d' % i, 'stream_type': 'live', 'stream_id': 100 + i,
                                   'stream_icon': '', 'epg_channel_id': 'ch%d.example' % i,
                                   'category_id': str(1 + i % 2), 'added': '1600000000'} for i in range(12)])
        else:
            self.reply_json(200, [])

    def reply_status(self, status):
        body = b'error'
        self.send_response(status)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.daemon_threads = True
    with open(sys.argv[1], 'w') as f:
        f.write(str(server.server_address[1]))
    server.serve_forever()


if __name__ == '__main__':
    main()
