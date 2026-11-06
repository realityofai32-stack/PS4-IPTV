#!/usr/bin/env python3
"""Misbehaving HTTP / HTTPS server for the network tests (standard library only). Endpoints:

  /ok                         200 "ok"
  /status/<code>              that status
  /flaky/<key>/<n>/<code>     the first <n> requests for <key> answer <code>, then 200 "ok"
  /reset/<key>/<n>            the first <n> requests for <key>: connection closed without an answer, then 200
  /partial                    Content-Length 100000, 10 bytes, connection closed
  /stall                      headers and 5 bytes, then nothing for 30 s
  /big/<bytes>                that many bytes
  /redirect?to=<url>          302 to <url> (percent-encoded)
  /loop                       302 to /loop
  /chain/<n>                  302 to /chain/<n-1> ... /chain/0 = 200
  /headers                    the request's Cookie / Authorization header values (for leak checks)
  /count/<key>                how many requests <key> has seen
Started as: failure_server.py PORTFILE CERT KEY  -> writes "<http port> <https port>" to PORTFILE.
"""
import http.server
import socket
import socketserver
import ssl
import sys
import threading
import time
import urllib.parse

COUNTS = {}
LOCK = threading.Lock()


def bump(key):
    with LOCK:
        COUNTS[key] = COUNTS.get(key, 0) + 1
        return COUNTS[key]


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def send(self, code, body=b"", extra=None):
        self.send_response(code)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Content-Type", "text/plain")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_GET(self):
        u = urllib.parse.urlsplit(self.path)
        parts = [p for p in u.path.split("/") if p]
        bump("all")
        if not parts:
            return self.send(404)
        head = parts[0]
        if head == "ok":
            return self.send(200, b"ok")
        if head == "status":
            return self.send(int(parts[1]), b"status")
        if head == "flaky":
            key, n, code = parts[1], int(parts[2]), int(parts[3])
            return self.send(code, b"busy") if bump(key) <= n else self.send(200, b"ok")
        if head == "reset":
            key, n = parts[1], int(parts[2])
            if bump(key) <= n:
                self.close_connection = True
                try:
                    self.connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, b"\x01\x00\x00\x00\x00\x00\x00\x00")
                except OSError:
                    pass
                self.connection.close()
                return
            return self.send(200, b"ok")
        if head == "partial":
            self.send_response(200)
            self.send_header("Content-Length", "100000")
            self.end_headers()
            self.wfile.write(b"0123456789")
            self.wfile.flush()
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
            return
        if head == "stall":
            self.send_response(200)
            self.send_header("Content-Length", "100")
            self.end_headers()
            self.wfile.write(b"12345")
            self.wfile.flush()
            time.sleep(30)
            return
        if head == "big":
            return self.send(200, b"x" * int(parts[1]))
        if head == "redirect":
            to = urllib.parse.parse_qs(u.query).get("to", [""])[0]
            return self.send(302, b"", {"Location": to})
        if head == "loop":
            return self.send(302, b"", {"Location": "/loop"})
        if head == "chain":
            n = int(parts[1])
            return self.send(200, b"ok") if n <= 0 else self.send(302, b"", {"Location": "/chain/%d" % (n - 1)})
        if head == "headers":
            body = ("cookie=%s\nauthorization=%s\n" % (self.headers.get("Cookie", ""),
                                                       self.headers.get("Authorization", ""))).encode()
            return self.send(200, body)
        if head == "count":
            with LOCK:
                return self.send(200, str(COUNTS.get(parts[1], 0)).encode())
        return self.send(404, b"no")


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    portfile, cert, key = sys.argv[1], sys.argv[2], sys.argv[3]
    plain = Server(("127.0.0.1", 0), Handler)
    secure = Server(("127.0.0.1", 0), Handler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(cert, key)
    secure.socket = ctx.wrap_socket(secure.socket, server_side=True)
    for s in (plain, secure):
        threading.Thread(target=s.serve_forever, daemon=True).start()
    with open(portfile + ".tmp", "w") as f:
        f.write("%d %d\n" % (plain.server_address[1], secure.server_address[1]))
    import os
    os.replace(portfile + ".tmp", portfile)
    while True:
        time.sleep(3600)


if __name__ == "__main__":
    main()
