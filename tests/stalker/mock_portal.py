#!/usr/bin/env python3
"""Mock Stalker / MAG middleware portals for the Stalker integration test (standard library only, synthetic data).

Several portals, one port each (written as JSON to the port file):

  ministra   API at /stalker_portal/server/load.php (Ministra 5 layout). get_all_channels, relative logos, the series
             module (seasons -> episodes with their own cmd, and a series whose seasons list episode numbers), account
             end date. Tokens expire after EXPIRE_AFTER authorized requests (token renewal).
  classic    API at /portal.php. No get_all_channels (error answer): the paged itv list; JSON numbers instead of
             strings, categories as a bare array; old-style series inside VOD (is_series + "series": [1..n]); no series
             module (HTML 404).
  login      get_profile answers status 2 (login / password required for the box)
  blocked    get_profile refuses the MAC (status 1 + block message)
  malformed  every answer is an HTML page
  brokenlists  handshake / profile fine, catalog answers are truncated JSON
  xtream     an Xtream Codes player_api.php (routing check: Xtream profiles never reach the portal code)

Every portal checks what a MAG box sends: the MAC cookie, the MAG User-Agent / X-User-Agent and, after the
handshake, "Authorization: Bearer <token>"; anything else is answered "Authorization failed." and counted as a
protocol violation. Control (any portal): /control/requests, /control/reset, /control/expire, /control/tokens.
"""

import http.server
import json
import random
import socketserver
import string
import sys
import threading
import urllib.parse

MAC = "00:1A:79:5E:6B:7C"
EXPIRE_AFTER = 10
MAG_UA_PART = "MAG200 stbapp"

LOCK = threading.Lock()


def rand(n=24):
    return "".join(random.choice(string.ascii_uppercase + string.digits) for _ in range(n))


class Portal:
    def __init__(self, name):
        self.name = name
        self.tokens = {}          # token -> authorized requests answered
        self.requests = []
        self.violations = []
        self.issued = []          # every token / play token handed out (the client must never log them)
        self.port = 0

    # ---------------------------------------------------------------- data
    def channels(self):
        names = ["Haber Türk", "Spor Şampiyon", "Belgesel Dünya", "News One", "Kids Fun", "Music Box"]
        out = []
        for i in range(1, 31):
            logo = ("/stalker_portal/misc/logos/320/%d.png" % i if i % 3 == 0 else
                    "http://img.example.com/logo/%d.png" % i if i % 3 == 1 else "")
            ch = {"id": str(100 + i), "name": "%s %d" % (names[i % len(names)], i), "number": str(i),
                  "cmd": "ffmpeg http://localhost/ch/%d_" % (100 + i), "tv_genre_id": "1" if i <= 15 else "2",
                  "logo": logo, "xmltv_id": "epg%d" % i, "tv_archive": 1 if i % 5 == 0 else 0}
            if self.name == "classic":
                ch["id"] = 100 + i              # numbers, not strings
                ch["number"] = i
                ch["tv_genre_id"] = 1 if i <= 15 else 2
            out.append(ch)
        return out

    def movies(self):
        out = []
        for i in range(1, 41):
            out.append({"id": str(5000 + i), "name": "Film %d %d" % (i, 2000 + i % 20), "o_name": "",
                        "description": "Plot of film %d." % i, "director": "Director %d" % i, "actors": "A, B",
                        "year": "%d-01-01" % (2000 + i % 20), "rating_imdb": "%.1f" % (5 + (i % 5)),
                        "time": str(80 + i), "genres_str": "Drama", "category_id": "10" if i <= 25 else "11",
                        "screenshot_uri": "/stalker_portal/screenshots/%d.jpg" % (5000 + i),
                        "added": "2025-0%d-1%d 10:00:00" % (1 + i % 9, i % 10),
                        "cmd": "/media/file_%d.mpg" % (5000 + i), "is_series": "0"})
        if self.name == "classic":
            for j in range(1, 4):
                out.append({"id": str(7000 + j), "name": "Old Series %d" % j, "is_series": "1",
                            "series": [1, 2, 3, 4, 5], "cmd": "eyJ0eXBlIjoic2VyaWVzIiwiaWQiOjcwMDB9%d" % j,
                            "category_id": "11", "description": "Old style series %d" % j, "year": "2019"})
        return out

    def series(self):
        return [{"id": "900", "name": "Mini Series 2021", "category_id": "20", "description": "Two seasons.",
                 "year": "2021", "screenshot_uri": "http://img.example.com/s900.jpg", "rating_imdb": "8.1"},
                {"id": "901", "name": "Season List Show", "category_id": "21", "description": "Seasons list numbers.",
                 "year": "2020"}]

    def seasons(self, movie_id):
        if movie_id == "900":
            return [{"id": "900:2", "name": "Season 2", "series": []}, {"id": "900:1", "name": "Season 1", "series": []}]
        if movie_id == "901":
            return [{"id": "901:1", "name": "Season 1", "cmd": "season-cmd-901-1", "series": [1, 2]},
                    {"id": "901:2", "name": "Season 2", "cmd": "season-cmd-901-2", "series": [1, 2, 3]}]
        return []

    def episodes(self, season_id):
        counts = {"900:1": 3, "900:2": 2}
        n = counts.get(season_id, 0)
        return [{"id": "%s:e%d" % (season_id, k), "name": "Episode %d" % k, "series_number": str(k),
                 "cmd": "/media/episode_%s_%d.mpg" % (season_id.replace(":", "_"), k), "time": "42"}
                for k in range(1, n + 1)]

    # ---------------------------------------------------------------- answers
    def page(self, items, p, per=14):
        p = max(1, p)
        return {"js": {"total_items": len(items), "max_page_items": per, "cur_page": p,
                       "data": items[(p - 1) * per:p * per]}}

    def answer(self, q, headers):
        t, a = q.get("type", ""), q.get("action", "")
        if self.name == "malformed":
            return 200, "<html><body>Portal maintenance</body></html>", "text/html"
        if self.name == "xtream":
            return 404, "", "text/plain"
        ua = headers.get("User-Agent", "")
        xua = headers.get("X-User-Agent", "")
        cookie = headers.get("Cookie", "")
        if MAG_UA_PART not in ua or "MAG250" not in xua or ("mac=" + urllib.parse.quote(MAC, safe="")) not in cookie:
            self.violations.append("identity headers missing on %s/%s" % (t, a))
            return 200, "Authorization failed.", "text/plain"
        if t == "stb" and a == "handshake":
            if headers.get("Authorization"):
                self.violations.append("bearer token sent with the handshake")
            tok = rand()
            with LOCK:
                self.tokens[tok] = 0
                self.issued.append(tok)
            return 200, json.dumps({"js": {"token": tok, "random": rand(8)}, "text": "generated in: 0.01s"}), "application/json"
        auth = headers.get("Authorization", "")
        tok = auth[7:] if auth.startswith("Bearer ") else ""
        with LOCK:
            known = tok in self.tokens and self.tokens[tok] >= 0
            if known:
                self.tokens[tok] += 1
                if self.name == "ministra" and self.tokens[tok] > EXPIRE_AFTER:
                    self.tokens[tok] = -1     # expired
                    known = False
        if not known:
            return 200, "Authorization failed.", "text/plain"
        if t == "stb" and a == "get_profile":
            for k in ("sn", "device_id", "device_id2", "signature"):
                if not q.get(k):
                    self.violations.append("get_profile without " + k)
            if self.name == "login":
                return 200, json.dumps({"js": {"id": "77", "status": 2}}), "application/json"
            if self.name == "blocked":
                return 200, json.dumps({"js": {"id": None, "status": 1, "block_msg": "MAC address not activated"}}), "application/json"
            return 200, json.dumps({"js": {"id": "77", "name": "box", "status": 0, "parent_password": "0000"}}), "application/json"
        if t == "account_info" and a == "get_main_info":
            return 200, json.dumps({"js": {"mac": MAC, "phone": "", "end_date": "2027-01-31 23:59:59"}}), "application/json"
        if self.name == "brokenlists" and t in ("itv", "vod", "series"):
            return 200, '{"js":{"total_items":3,"data":[{"id":"1","name":"cut', "application/json"
        if t == "itv":
            if a == "get_genres":
                g = [{"id": "*", "title": "All"}, {"id": "1", "title": "Haberler"}, {"id": "2", "title": "Spor"}]
                if self.name == "classic":
                    g = [{"id": 1, "title": "Haberler"}, {"id": 2, "title": "Spor"}]
                return 200, json.dumps({"js": g}), "application/json"
            if a == "get_all_channels":
                if self.name == "classic":
                    return 200, json.dumps({"js": {"error": "unknown action"}}), "application/json"
                return 200, json.dumps({"js": {"total_items": 30, "data": self.channels()}}), "application/json"
            if a == "get_ordered_list":
                return 200, json.dumps(self.page(self.channels(), int(q.get("p", "1")))), "application/json"
            if a == "create_link":
                return self.link(q, "live", "ts")
        if t == "vod":
            if a == "get_categories":
                c = [{"id": "*", "title": "All"}, {"id": "10", "title": "Aksiyon"}, {"id": "11", "title": "Dram"}]
                return 200, json.dumps({"js": c}), "application/json"
            if a == "get_ordered_list":
                cat = q.get("category", "*")
                items = [m for m in self.movies() if cat == "*" or m["category_id"] == cat]
                return 200, json.dumps(self.page(items, int(q.get("p", "1")))), "application/json"
            if a == "create_link":
                return self.link(q, "movie", "mkv")
        if t == "series":
            if self.name == "classic":
                return 404, "<html>404 Not Found</html>", "text/html"
            if a == "get_categories":
                return 200, json.dumps({"js": [{"id": "20", "title": "Mini"}, {"id": "21", "title": "Long"}]}), "application/json"
            if a == "get_ordered_list":
                mid = q.get("movie_id", "")
                if not mid:
                    cat = q.get("category", "*")
                    items = [s for s in self.series() if cat == "*" or s["category_id"] == cat]
                    return 200, json.dumps(self.page(items, int(q.get("p", "1")))), "application/json"
                sid = q.get("season_id", "0")
                if sid in ("0", ""):
                    return 200, json.dumps(self.page(self.seasons(mid), int(q.get("p", "1")))), "application/json"
                return 200, json.dumps(self.page(self.episodes(sid), int(q.get("p", "1")))), "application/json"
        return 200, json.dumps({"js": False}), "application/json"

    def link(self, q, kind, ext):
        cmd = q.get("cmd", "")
        if "limit-me" in cmd:
            return 200, json.dumps({"js": {"cmd": "", "error": "limit"}}), "application/json"
        if not cmd:
            return 200, json.dumps({"js": {"cmd": "", "error": "nothing_to_play"}}), "application/json"
        play = rand(16)
        with LOCK:
            self.issued.append(play)
        stream = urllib.parse.quote(cmd, safe="")
        url = "http://127.0.0.1:%d/play/%s.php?mac=%s&stream=%s&extension=%s&play_token=%s" % (
            self.port, kind, MAC, stream, ext, play)
        if q.get("series"):
            url += "&series=" + q["series"]
        return 200, json.dumps({"js": {"id": "1", "cmd": "ffmpeg " + url, "error": ""}}), "application/json"


PORTALS = {}


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def reply(self, status, body, ctype):
        data = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        portal = PORTALS[self.server.portal]
        u = urllib.parse.urlparse(self.path)
        q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query, keep_blank_values=True).items()}
        if u.path.startswith("/control/"):
            what = u.path[9:]
            with LOCK:
                if what == "requests":
                    return self.reply(200, json.dumps({"requests": portal.requests, "violations": portal.violations}),
                                      "application/json")
                if what == "reset":
                    portal.requests.clear()
                    portal.violations.clear()
                    return self.reply(200, "{}", "application/json")
                if what == "expire":
                    for k in portal.tokens:
                        portal.tokens[k] = -1
                    return self.reply(200, "{}", "application/json")
                if what == "tokens":
                    return self.reply(200, json.dumps(portal.issued), "application/json")
        with LOCK:
            portal.requests.append({"path": u.path, "type": q.get("type", ""), "action": q.get("action", ""),
                                    "params": {k: v for k, v in q.items() if k not in ("type", "action")},
                                    "auth": bool(self.headers.get("Authorization"))})
        if portal.name == "xtream":
            if u.path == "/player_api.php":
                act = q.get("action", "")
                if not act:
                    return self.reply(200, json.dumps({"user_info": {"auth": 1, "status": "Active", "exp_date": "1893456000",
                                                                     "max_connections": "1", "active_cons": "0",
                                                                     "allowed_output_formats": ["m3u8", "ts"]},
                                                       "server_info": {"url": "127.0.0.1", "port": str(self.server.server_address[1]),
                                                                       "server_protocol": "http"}}), "application/json")
                if act == "get_live_categories":
                    return self.reply(200, json.dumps([{"category_id": "1", "category_name": "News"}]), "application/json")
                if act == "get_live_streams":
                    return self.reply(200, json.dumps([{"stream_id": 1, "name": "X News", "category_id": "1"}]),
                                      "application/json")
                if act == "get_vod_categories":
                    return self.reply(200, json.dumps([{"category_id": "5", "category_name": "Filmler"}]), "application/json")
                if act == "get_vod_streams":
                    return self.reply(200, json.dumps([{"stream_id": 300 + i, "name": "Xtream Film %d 2020" % i,
                                                        "category_id": "5", "container_extension": "mkv",
                                                        "stream_icon": "http://img.example.com/x%d.jpg" % i,
                                                        "rating": "7", "added": "1700000000"} for i in range(1, 6)]),
                                      "application/json")
                if act == "get_series_categories":
                    return self.reply(200, json.dumps([{"category_id": "8", "category_name": "Diziler"}]), "application/json")
                if act == "get_series":
                    return self.reply(200, json.dumps([{"series_id": 70, "name": "Xtream Dizi", "category_id": "8",
                                                        "cover": "http://img.example.com/s70.jpg"}]), "application/json")
                if act == "get_series_info":
                    eps = {"1": [{"id": "701", "episode_num": 1, "season": 1, "container_extension": "mkv"},
                                 {"id": "702", "episode_num": 2, "season": 1, "container_extension": "mkv"}],
                           "2": [{"id": "711", "episode_num": 1, "season": 2, "container_extension": "mp4"}]}
                    return self.reply(200, json.dumps({"info": {"name": "Xtream Dizi"}, "seasons": [],
                                                       "episodes": eps}), "application/json")
            return self.reply(404, "", "text/plain")
        expected = "/portal.php" if portal.name in ("classic", "login", "blocked", "brokenlists", "malformed") \
            else "/stalker_portal/server/load.php"
        if u.path != expected:
            return self.reply(404, "<html>404</html>", "text/html")
        status, body, ctype = portal.answer(q, self.headers)
        self.reply(status, body, ctype)


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    ports = {}
    for name in ("ministra", "classic", "login", "blocked", "malformed", "brokenlists", "xtream"):
        PORTALS[name] = Portal(name)
        s = Server(("127.0.0.1", 0), Handler)
        s.portal = name
        PORTALS[name].port = s.server_address[1]
        ports[name] = s.server_address[1]
        threading.Thread(target=s.serve_forever, daemon=True).start()
    ports["mac"] = MAC
    ports["expire_after"] = EXPIRE_AFTER
    with open(sys.argv[1] + ".tmp", "w") as f:
        json.dump(ports, f)
    import os
    os.replace(sys.argv[1] + ".tmp", sys.argv[1])
    threading.Event().wait()


if __name__ == "__main__":
    main()
