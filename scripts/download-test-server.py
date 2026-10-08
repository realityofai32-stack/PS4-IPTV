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
Control: /_log (JSON list of requests), /_reset. Listens on 127.0.0.1, port written to the file in argv[1].
"""
import json
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LOG = []
SEEN = set()
LOCK = threading.Lock()


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
        if path == '/_reset':
            with LOCK:
                LOG.clear()
                SEEN.clear()
            self.send_response(204)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        parts = path.strip('/').split('/')
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
