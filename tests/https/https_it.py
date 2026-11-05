#!/usr/bin/env python3
"""HTTPS playback integration test (standard library + the openssl / ffmpeg command line tools).

Runs tests/https/tls_harness (FFmpeg 5.0 + Mbed TLS 2.16.6 host build, opened the way libmpv 0.34.1 opens streams)
against local servers:

  HTTP                    plain HTTP (the behaviour that must not change)
  HTTPS valid             certificate for localhost / 127.0.0.1 from the test CA
  HTTPS wrong host        certificate from the test CA for another name (hostname mismatch)
  HTTPS untrusted         self-signed certificate (not in the CA bundle)
  HTTPS expired           certificate from the test CA, validity in the past
  HTTPS TLS 1.3 only      documents the Mbed TLS 2.16 limit (TLS 1.2 maximum)

Media (made with the system ffmpeg): H.264/AAC MPEG-TS and MP4, and an HLS VOD (2 s segments) with relative, absolute
HTTPS (tokenized) and mixed-origin segment lists. Every request is logged by the servers with its User-Agent.

Optional (--public): stable public HTTPS test streams with the app's bundled CA list (third_party/cacert).

usage: https_it.py <harness> <work dir> [--public]
"""

import http.server
import json
import os
import socketserver
import ssl
import subprocess
import sys
import threading
import urllib.parse

HARNESS = sys.argv[1]
WORK = os.path.abspath(sys.argv[2])
PUBLIC = "--public" in sys.argv
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
UA = "V7-HTTPS-Test/1.0 (PS4IPTV)"
TOKEN = "S3CRETTOKEN4711"
USER = "streamuser77"
PASSWORD = "streampass88"
os.makedirs(WORK, exist_ok=True)
REQUESTS = []
REQ_LOCK = threading.Lock()


def run(cmd, **kw):
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, **kw)


# ------------------------------------------------------------------ certificates
def make_certs():
    d = os.path.join(WORK, "certs")
    os.makedirs(d, exist_ok=True)
    p = lambda n: os.path.join(d, n)
    run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", p("ca.key"), "-out", p("ca.crt"),
         "-days", "30", "-subj", "/CN=V7 Test Root CA", "-addext", "basicConstraints=critical,CA:TRUE",
         "-addext", "keyUsage=critical,keyCertSign,cRLSign"])

    def leaf(name, san, extra=()):
        run(["openssl", "req", "-newkey", "rsa:2048", "-nodes", "-keyout", p(name + ".key"), "-out", p(name + ".csr"),
             "-subj", "/CN=" + san.split(",")[0].split(":")[1]])
        ext = p(name + ".ext")
        with open(ext, "w") as f:
            f.write("subjectAltName=%s\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\n" % san)
        run(["openssl", "x509", "-req", "-in", p(name + ".csr"), "-CA", p("ca.crt"), "-CAkey", p("ca.key"),
             "-CAcreateserial", "-out", p(name + ".crt"), "-extfile", ext, *extra])

    leaf("valid", "DNS:localhost,IP:127.0.0.1", ["-days", "30"])
    leaf("wronghost", "DNS:wrong.example", ["-days", "30"])
    leaf("expired", "DNS:localhost,IP:127.0.0.1", ["-not_before", "20200101000000Z", "-not_after", "20200201000000Z"])
    run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", p("untrusted.key"), "-out",
         p("untrusted.crt"), "-days", "30", "-subj", "/CN=localhost", "-addext",
         "subjectAltName=DNS:localhost,IP:127.0.0.1"])
    return d


# ------------------------------------------------------------------ media
def make_media():
    d = os.path.join(WORK, "media")
    if os.path.exists(os.path.join(d, "hls", "index.m3u8")):
        return d
    os.makedirs(os.path.join(d, "hls"), exist_ok=True)
    src = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
           "testsrc2=size=320x180:rate=25:duration=8", "-f", "lavfi", "-i", "sine=frequency=440:duration=8"]
    enc = ["-c:v", "libx264", "-preset", "ultrafast", "-g", "25", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "64k"]
    run(src + enc + [os.path.join(d, "media.ts")])
    run(src + enc + ["-movflags", "+faststart", os.path.join(d, "media.mp4")])
    run(src + enc + ["-f", "hls", "-hls_time", "2", "-hls_playlist_type", "vod", "-hls_segment_filename",
                     os.path.join(d, "hls", "seg%d.ts"), os.path.join(d, "hls", "index.m3u8")])
    return d


# ------------------------------------------------------------------ servers
class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    media = None
    ports = {}

    def log_message(self, *a):
        pass

    def send_bytes(self, body, ctype, status=200, extra=None):
        rng = self.headers.get("Range")
        start, end = 0, len(body) - 1
        if rng and rng.startswith("bytes="):
            a, _, b = rng[6:].partition("-")
            start = int(a) if a else 0
            end = int(b) if b else len(body) - 1
            status = 206
        chunk = body[start:end + 1]
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(chunk)))
        self.send_header("Accept-Ranges", "bytes")
        if status == 206:
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, len(body)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(chunk)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        with REQ_LOCK:
            REQUESTS.append({"server": self.server.name, "path": u.path, "query": u.query,
                             "ua": self.headers.get("User-Agent", "")})
        path = u.path
        if path == "/redirect":
            self.send_response(302)
            self.send_header("Location", q["to"][0])
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if path.startswith("/hls/") and path.endswith(".m3u8"):
            name = path[5:]
            text = open(os.path.join(self.media, "hls", "index.m3u8")).read()
            if name == "master.m3u8":
                text = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=400000,RESOLUTION=320x180\nindex.m3u8?token=%s\n" % TOKEN
            elif name.startswith("abs-"):
                # absolute segment URLs on another server, with credentials in the path / query
                target = name[4:-5]
                base = "https://localhost:%d" % self.ports[target]
                text = "".join(base + "/hls/live/%s/%s/" % (USER, PASSWORD) + l + "?token=" + TOKEN + "\n"
                               if l.startswith("seg") else l + "\n" for l in text.splitlines())
            self.send_bytes(text.encode(), "application/vnd.apple.mpegurl")
            return
        if path.startswith("/hls/"):
            seg = os.path.basename(path)
            f = os.path.join(self.media, "hls", seg)
            if os.path.exists(f):
                self.send_bytes(open(f, "rb").read(), "video/mp2t")
                return
        base = os.path.basename(path)
        if base in ("media.ts", "media.mp4"):
            self.send_bytes(open(os.path.join(self.media, base), "rb").read(),
                            "video/mp2t" if base.endswith(".ts") else "video/mp4")
            return
        self.send_response(404)
        self.send_header("Content-Length", "0")
        self.end_headers()


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def start(name, ctx=None):
    s = Server(("127.0.0.1", 0), Handler)
    s.name = name
    if ctx:
        s.socket = ctx.wrap_socket(s.socket, server_side=True)
    threading.Thread(target=s.serve_forever, daemon=True).start()
    Handler.ports[name] = s.server_address[1]
    return s.server_address[1]


def tls_ctx(certs, name, tls13_only=False):
    c = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    c.load_cert_chain(os.path.join(certs, name + ".crt"), os.path.join(certs, name + ".key"))
    c.minimum_version = ssl.TLSVersion.TLSv1_3 if tls13_only else ssl.TLSVersion.TLSv1_2
    return c


# ------------------------------------------------------------------ cases
results = []
failures = 0


def harness(mode, url, ca, verify=True, seek=False, tag=""):
    raw = os.path.join(WORK, "raw-%d.log" % len(results))
    red = os.path.join(WORK, "redacted-%d.log" % len(results))
    cmd = [HARNESS, mode, url, ca or "-", "1" if verify else "0", UA, raw, red] + (["seek"] if seek else [])
    out = subprocess.run(cmd, check=True, capture_output=True, text=True, timeout=120).stdout.strip()
    r = json.loads(out.splitlines()[-1])
    r["raw"] = open(raw, errors="replace").read()
    r["redacted"] = open(red, errors="replace").read()
    return r


def case(name, mode, url, ca, expect_ok, verify=True, seek=False, tls=None, check=None):
    global failures
    with REQ_LOCK:
        REQUESTS.clear()
    r = harness(mode, url, ca, verify, seek)
    with REQ_LOCK:
        reqs = list(REQUESTS)
    ok = (r["status"] == "ok") == expect_ok
    notes = []
    if tls and r["tls"] != tls:
        ok = False
        notes.append("tls verdict %s, expected %s" % (r["tls"], tls))
    if seek and expect_ok and not r["seek"]:
        ok = False
        notes.append("seek failed")
    bad_ua = [q for q in reqs if q["ua"] != UA]
    if bad_ua:
        ok = False
        notes.append("%d request(s) without the custom User-Agent: %s" % (len(bad_ua), bad_ua[:2]))
    # credentials / tokens never in the stored log
    for secret in (TOKEN, USER, PASSWORD):
        if secret in r["redacted"]:
            ok = False
            notes.append("%s in the stored log" % secret)
    if check:
        msg = check(r, reqs)
        if msg:
            ok = False
            notes.append(msg)
    results.append({"case": name, "ok": ok, "status": r["status"], "packets": r["packets"], "tls": r["tls"],
                    "requests": len(reqs), "servers": sorted({q["server"] for q in reqs}), "notes": notes})
    if not ok:
        failures += 1
    print("%s  %-62s status=%-28s packets=%-4d tls=%-11s requests=%d %s" % (
        "ok  " if ok else "FAIL", name, r["status"][:28], r["packets"], r["tls"], len(reqs), "; ".join(notes)))
    return r


def main():
    certs = make_certs()
    Handler.media = make_media()
    ca = os.path.join(certs, "ca.crt")
    http_port = start("http")
    valid = start("valid", tls_ctx(certs, "valid"))
    valid2 = start("valid2", tls_ctx(certs, "valid"))
    wrong = start("wronghost", tls_ctx(certs, "wronghost"))
    untrusted = start("untrusted", tls_ctx(certs, "untrusted"))
    expired = start("expired", tls_ctx(certs, "expired"))
    tls13 = start("tls13", tls_ctx(certs, "valid", tls13_only=True))
    H = "http://127.0.0.1:%d" % http_port
    S = lambda port: "https://localhost:%d" % port
    q = "?username=%s&password=%s&token=%s" % (USER, PASSWORD, TOKEN)

    def segments_over(server, minimum=4):
        def f(r, reqs):
            segs = [x for x in reqs if x["path"].endswith(".ts") and x["server"] == server]
            return None if len(segs) >= minimum else "%d segment request(s) on %s" % (len(segs), server)
        return f

    def raw_has_token(r, reqs):
        return None if TOKEN in r["raw"] or USER in r["raw"] else "the raw log never showed the URL (test not meaningful)"

    print("== protocols in the FFmpeg build: see the build log; https + tls checked by configure")
    case("HTTP direct MPEG-TS (unchanged)", "direct", H + "/media.ts", ca, True)
    case("HTTP direct MPEG-TS, no CA bundle (HTTP needs none)", "direct", H + "/media.ts", None, True)
    case("HTTP HLS (relative segments)", "hls", H + "/hls/index.m3u8", ca, True, check=segments_over("http"))
    case("HTTPS direct MPEG-TS, valid certificate", "direct", S(valid) + "/media.ts" + q, ca, True, check=raw_has_token)
    case("HTTPS direct MP4 + seek (Range over TLS)", "direct", S(valid) + "/media.mp4", ca, True, seek=True)
    case("HTTPS by IP address (SAN IP)", "direct", "https://127.0.0.1:%d/media.ts" % valid, ca, True)
    case("HTTPS HLS manifest, relative HTTPS segments", "hls", S(valid) + "/hls/index.m3u8", ca, True,
         check=segments_over("valid"))
    case("HTTPS HLS master -> variant (tokenized)", "hls", S(valid) + "/hls/master.m3u8" + q, ca, True,
         check=lambda r, reqs: segments_over("valid")(r, reqs) or raw_has_token(r, reqs))
    case("HTTPS HLS, absolute HTTPS segments on another host", "hls", S(valid) + "/hls/abs-valid2.m3u8", ca, True,
         check=segments_over("valid2"))
    case("HTTP HLS manifest with HTTPS segments (mixed)", "hls", H + "/hls/abs-valid.m3u8", ca, True,
         check=segments_over("valid"))
    case("HTTP -> HTTPS redirect (302)", "direct", H + "/redirect?to=" + urllib.parse.quote(S(valid) + "/media.ts"),
         ca, True, check=lambda r, reqs: None if {"http", "valid"} <= {x["server"] for x in reqs} else "redirect not followed")
    case("HTTPS -> HTTPS redirect (302, other server)", "direct",
         S(valid) + "/redirect?to=" + urllib.parse.quote(S(valid2) + "/media.ts" + q), ca, True,
         check=lambda r, reqs: None if "valid2" in {x["server"] for x in reqs} else "redirect not followed")
    case("HTTPS HLS via HTTP -> HTTPS redirect", "hls",
         H + "/redirect?to=" + urllib.parse.quote(S(valid) + "/hls/index.m3u8"), ca, True)
    case("untrusted (self-signed) certificate is refused", "direct", S(untrusted) + "/media.ts", ca, False,
         tls="certificate")
    case("hostname mismatch is refused", "direct", S(wrong) + "/media.ts", ca, False, tls="certificate")
    case("expired certificate is refused", "direct", S(expired) + "/media.ts", ca, False, tls="certificate")
    case("HTTPS -> HTTPS redirect to a wrong-host server is refused", "direct",
         S(valid) + "/redirect?to=" + urllib.parse.quote(S(wrong) + "/media.ts"), ca, False, tls="certificate")
    case("HTTP -> HTTPS redirect to an untrusted server is refused", "direct",
         H + "/redirect?to=" + urllib.parse.quote(S(untrusted) + "/media.ts"), ca, False, tls="certificate")
    case("HLS segments on an untrusted server are refused (nested opens verified)", "hls",
         S(valid) + "/hls/abs-untrusted.m3u8", ca, False, tls="certificate",
         check=lambda r, reqs: None if not [x for x in reqs if x["server"] == "untrusted"] else "segment bytes were served")
    case("no CA bundle: HTTPS is refused, never unverified", "direct", S(valid) + "/media.ts", None, False)
    case("CA bundle unreadable: refused", "direct", S(valid) + "/media.ts", os.path.join(WORK, "missing.pem"), False,
         tls="ca-bundle")
    case("TLS 1.3-only server (Mbed TLS 2.16: TLS 1.2 max)", "direct", S(tls13) + "/media.ts", ca, False,
         tls="handshake")
    # control: with verification off the same untrusted server is reachable, so the refusals above come from the
    # certificate check (the app never turns verification off)
    case("control: untrusted server with verification off (test only)", "direct", S(untrusted) + "/media.ts", ca,
         True, verify=False)

    if PUBLIC:
        bundle = os.path.join(REPO, "third_party", "cacert", "cacert.pem")
        for name, mode, url in [
            ("PUBLIC Mux HLS test stream (HTTPS manifest + segments)", "hls",
             "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8"),
            ("PUBLIC Apple bipbop HLS (HTTPS)", "hls",
             "https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_4x3/bipbop_4x3_variant.m3u8"),
            ("PUBLIC MP4 over HTTPS (W3C test media)", "direct", "https://media.w3.org/2010/05/sintel/trailer.mp4")]:
            try:
                with REQ_LOCK:
                    REQUESTS.clear()
                r = harness(mode, url, bundle, True)
                ok = r["status"] == "ok" or (r["packets"] > 0)
                print("%s  %-62s status=%-28s packets=%-4d tls=%s (bundled cacert.pem)" % (
                    "ok  " if ok else "NOTE", name, r["status"][:28], r["packets"], r["tls"]))
                results.append({"case": name, "ok": ok, "status": r["status"], "packets": r["packets"],
                                "tls": r["tls"], "public": True})
            except Exception as e:   # the network is optional
                print("NOTE  %-62s not reachable: %s" % (name, e))
                results.append({"case": name, "ok": None, "status": "unreachable", "public": True})

    with open(os.path.join(WORK, "results.json"), "w") as f:
        json.dump(results, f, indent=1)
    local = [r for r in results if not r.get("public")]
    print("%d local case(s), %d failure(s)" % (len(local), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
