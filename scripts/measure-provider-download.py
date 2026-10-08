"""PC control test for download speed: the same provider movie the PS4 downloads, measured on this PC.

Credentials come from config/test_streams.txt (git-ignored, the /live/<user>/<pass>/ URL). Nothing secret is
printed: no URL, no host, no user name, no password - only the stream id, the title and numbers.

Usage:
  python -I scripts/measure-provider-download.py [--id STREAM_ID] [--seconds 60] [--parallel 2] [--chunk 1048576]

Measures one connection for --seconds (first 10 s, first 30 s, overall, peak 1-second rate), the HTTP status,
Content-Length and whether Range requests are honoured. With --parallel N it then opens N connections at
different offsets of the same file at the same time: if the sum is about N x the single rate, the provider
limits each connection (per-connection throttling); if the sum stays at the single rate, the limit is per
account / per line. The data is discarded (nothing is written to the disk).
"""
import argparse
import http.client
import json
import socket
import os
import re
import sys
import threading
import time
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAMPLES = os.path.join(ROOT, 'build', 'xtream-samples', 'get_vod_streams.json')
UA = 'PS4IPTV/0.2.0'


def credentials():
    with open(os.path.join(ROOT, 'config', 'test_streams.txt'), encoding='utf-8-sig') as f:
        for line in f:
            m = re.search(r'(https?://[^/]+)/live/([^/]+)/([^/]+)/', line)
            if m:
                return m.group(1), m.group(2), m.group(3)
    raise SystemExit('no Xtream URL in config/test_streams.txt')


def pick_movie(stream_id):
    if not os.path.exists(SAMPLES):
        if stream_id:
            return stream_id, 'mkv', '(title unknown)'
        raise SystemExit('no cached movie list (run scripts/fetch-xtream-samples.py get_vod_streams) and no --id')
    with open(SAMPLES, encoding='utf-8') as f:
        movies = json.load(f)
    if stream_id:
        for m in movies:
            if str(m.get('stream_id')) == str(stream_id):
                return str(m['stream_id']), m.get('container_extension') or 'mkv', m.get('name', '')
        return stream_id, 'mkv', '(not in the cached list)'
    # newest addition: most likely to be a normal, currently served file
    movies = [m for m in movies if m.get('stream_id')]
    movies.sort(key=lambda m: int(m.get('added') or 0), reverse=True)
    m = movies[0]
    return str(m['stream_id']), m.get('container_extension') or 'mkv', m.get('name', '')


def open_stream(url, offset=0):
    headers = {'User-Agent': UA}
    if offset > 0:
        headers['Range'] = 'bytes=%d-' % offset
    req = urllib.request.Request(url, headers=headers)
    return urllib.request.urlopen(req, timeout=30)


def mb(n):
    return n / 1e6


def single(url, seconds, chunk):
    t0 = time.monotonic()
    r = open_stream(url)
    t_head = time.monotonic() - t0
    status = r.status
    length = r.headers.get('Content-Length')
    accept = r.headers.get('Accept-Ranges')
    redirected = r.geturl() != url
    total = 0
    marks = {}
    per_second = []
    second_start = time.monotonic()
    second_bytes = 0
    reads = 0
    start = time.monotonic()
    while True:
        data = r.read(chunk)
        now = time.monotonic()
        if not data:
            break
        reads += 1
        total += len(data)
        second_bytes += len(data)
        if now - second_start >= 1.0:
            per_second.append(second_bytes / (now - second_start))
            second_start = now
            second_bytes = 0
        el = now - start
        for mark in (10, 30):
            if mark not in marks and el >= mark:
                marks[mark] = total / el
        if el >= seconds:
            break
    elapsed = time.monotonic() - start
    r.close()
    print('single connection')
    print('  HTTP status            %d%s' % (status, ' (after a redirect)' if redirected else ''))
    print('  time to first header   %.2f s' % t_head)
    print('  Content-Length         %s' % ('%.1f MB' % mb(int(length)) if length else 'unknown'))
    print('  Accept-Ranges          %s' % (accept or '-'))
    print('  downloaded             %.1f MB in %.1f s' % (mb(total), elapsed))
    for mark in (10, 30):
        if mark in marks:
            print('  first %2d s average     %.2f MB/s (%.1f Mbit/s)' % (mark, mb(marks[mark]), mb(marks[mark]) * 8))
    avg = total / elapsed if elapsed > 0 else 0
    print('  overall average        %.2f MB/s (%.1f Mbit/s)' % (mb(avg), mb(avg) * 8))
    if per_second:
        print('  peak 1-second rate     %.2f MB/s' % mb(max(per_second)))
        print('  slowest 1-second rate  %.2f MB/s' % mb(min(per_second)))
    return avg, int(length) if length else 0


def range_check(url):
    try:
        r = open_stream(url, 1000000)
        status = r.status
        cr = r.headers.get('Content-Range') or '-'
        r.read(1024)
        r.close()
        cr_total = cr.split('/')[-1] if '/' in cr else '-'
        print('range request at 1 MB  HTTP %d, Content-Range total %s' % (status, cr_total))
        return status == 206
    except Exception as e:  # noqa: BLE001 - diagnostic only
        print('range request at 1 MB  failed: %s' % type(e).__name__)
        return False


def parallel(url, n, seconds, length, chunk):
    results = [0] * n
    errors = [None] * n

    def worker(i):
        try:
            offset = (length // (n + 1)) * i if length else 0
            r = open_stream(url, offset)
            start = time.monotonic()
            while time.monotonic() - start < seconds:
                data = r.read(chunk)
                if not data:
                    break
                results[i] += len(data)
            r.close()
        except Exception as e:  # noqa: BLE001 - diagnostic only
            errors[i] = type(e).__name__ + (' HTTP %d' % e.code if hasattr(e, 'code') else '')

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(n)]
    t0 = time.monotonic()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    el = time.monotonic() - t0
    print('%d parallel connections (%.0f s)' % (n, el))
    for i in range(n):
        print('  connection %d           %s' % (i + 1, errors[i] or '%.2f MB/s' % mb(results[i] / el)))
    print('  sum                    %.2f MB/s' % mb(sum(results) / el))
    return sum(results) / el


def final_location(url):
    # the provider answers /movie/... with a redirect to the media host; follow it without reading the body
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *args, **kwargs):
            return None
    opener = urllib.request.build_opener(NoRedirect)
    req = urllib.request.Request(url, headers={'User-Agent': UA})
    try:
        r = opener.open(req, timeout=30)
        r.close()
        return url
    except urllib.error.HTTPError as e:
        if e.code in (301, 302, 303, 307, 308) and e.headers.get('Location'):
            return urllib.parse.urljoin(url, e.headers['Location'])
        raise


def rtt(url, n=8):
    u = urllib.parse.urlsplit(url)
    port = u.port or (443 if u.scheme == 'https' else 80)
    times = []
    for _ in range(n):
        t0 = time.monotonic()
        s = socket.create_connection((u.hostname, port), timeout=10)
        times.append(time.monotonic() - t0)
        s.close()
        time.sleep(0.2)
    times.sort()
    print('TCP connect RTT to the media host: min %.0f ms, median %.0f ms, max %.0f ms'
          % (times[0] * 1000, times[len(times) // 2] * 1000, times[-1] * 1000))
    return times[0]


def with_rcvbuf(url, rcvbuf, seconds, chunk):
    # a fixed socket receive buffer set before connect (as a client with a fixed TCP window would have)
    u = urllib.parse.urlsplit(url)
    port = u.port or 80

    class Conn(http.client.HTTPConnection):
        def connect(self):
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            if rcvbuf:
                s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
            s.settimeout(30)
            s.connect((u.hostname, port))
            self.sock = s

    c = Conn(u.hostname, port)
    path = u.path + ('?' + u.query if u.query else '')
    c.request('GET', path, headers={'User-Agent': UA})
    r = c.getresponse()
    total = 0
    start = time.monotonic()
    while time.monotonic() - start < seconds:
        data = r.read(chunk)
        if not data:
            break
        total += len(data)
    el = time.monotonic() - start
    c.close()
    return total / el


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--id', default='')
    ap.add_argument('--seconds', type=float, default=60)
    ap.add_argument('--parallel', type=int, default=0)
    ap.add_argument('--chunk', type=int, default=1 << 20)
    ap.add_argument('--gap', type=float, default=45, help='seconds between connections (1-connection accounts)')
    ap.add_argument('--rcvbuf', default='', help='comma list of socket receive buffer sizes in KB (0 = OS default)')
    a = ap.parse_args()
    base, user, pw = credentials()
    sid, ext, title = pick_movie(a.id)
    url = '%s/movie/%s/%s/%s.%s' % (base, user, pw, sid, ext)
    print('movie stream_id %s (%s) "%s"' % (sid, ext, title))
    if a.rcvbuf:
        media = final_location(url)
        if urllib.parse.urlsplit(media).scheme != 'http':
            raise SystemExit('the media host is not plain http: --rcvbuf test not supported')
        rtt(media)
        for kb in [int(x) for x in a.rcvbuf.split(',')]:
            time.sleep(a.gap)
            rate = with_rcvbuf(media, kb * 1024, a.seconds, a.chunk)
            print('  receive buffer %-8s %.2f MB/s' % ('%d KB' % kb if kb else 'default', mb(rate)))
        return 0
    avg, length = single(url, a.seconds, a.chunk)
    range_check(url)
    if a.parallel > 1:
        time.sleep(2)
        parallel(url, a.parallel, min(a.seconds, 30), length, a.chunk)
    return 0


if __name__ == '__main__':
    sys.exit(main())
