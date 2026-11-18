// Network failure integration test (V7 Player): the app's HTTP client and every source type's service - Xtream, M3U,
// XMLTV, Stalker / MAG, Jellyfin / Emby, Stremio - against failures served by scripts/download-test-server.py and
// addresses that cannot work (a name that never resolves, a closed port): DNS failure, connection refused, reset,
// a stalled answer, cancellation, redirect loops, malformed / truncated / empty / HTML / oversized answers and HTTP
// errors. Every call must end within a bounded time with a user-facing message that carries no address or
// credential. Then media server sessions (expired token, an empty server, malformed items, the server gone while
// playback is prepared), add-on answers (invalid manifests, empty catalogs, failing metas / streams, torrent- or
// YouTube-only streams, several streams, the add-on gone mid-session), guides and playlists sent chunked, an expired
// guide, a large guide with bounded memory, and an offline start with saved lists. Finally the log: no username,
// password, token, MAC address or credential-bearing address. Run by scripts/test-download-integration.sh.

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "check.h"
#include "../../src/app/m3u_service.h"
#include "../../src/app/mediabrowser_service.h"
#include "../../src/app/stalker_service.h"
#include "../../src/app/stremio_service.h"
#include "../../src/app/xmltv_service.h"
#include "../../src/app/xtream_service.h"
#include "../../src/iptv/epg.h"
#include "../../src/iptv/m3u.h"
#include "../../src/network/http.h"
#include "../../src/platform/clock.h"
#include "../../src/platform/fs.h"
#include "../../src/platform/log.h"

namespace {

    std::string server() {
        const char *s = std::getenv("PS4IPTV_TEST_SERVER");
        return s ? s : "";
    }

    std::string dataDir() {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string d = fs::join(base ? base : ".", "failure-it/");
        fs::ensureDir(d);
        return d;
    }

    std::string logDir() {
        return dataDir() + "logs";
    }

    std::string runId(const std::string &name) {
        static std::string run = std::to_string(clockx::unixNow());
        return "fail-" + name + "-" + run;
    }

    double now() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    void control(const char *path) {
        http::Request r;
        r.url = server() + path;
        r.retries = 0;
        http::get(r);
    }

    int requests(const std::string &part) {
        http::Request r;
        r.url = server() + "/_log";
        std::string body = http::get(r).body;
        int n = 0;
        for (size_t at = body.find(part); at != std::string::npos; at = body.find(part, at + 1)) {
            n++;
        }
        return n;
    }

    // a port nothing listens on: bound once by the system, then closed
    int closedPort() {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        socklen_t len = sizeof(a);
        bind(fd, (sockaddr *) &a, sizeof(a));
        getsockname(fd, (sockaddr *) &a, &len);
        int port = ntohs(a.sin_port);
        close(fd);
        return port;
    }

    // the base address of a failure: an unresolvable name, a closed port, or a failure the server plays
    std::string base(const std::string &mode) {
        if (mode == "dns") {
            return "http://v7-player-test.invalid";
        }
        if (mode == "refused") {
            static int port = closedPort();
            return "http://127.0.0.1:" + std::to_string(port);
        }
        return server() + "/fail/" + mode;
    }

    // a user-facing message: present, and without an address or a credential
    bool clean(const std::string &message) {
        for (const char *bad: {"examplepass", "exampleuser", "exampletoken", "00:1A:79", "127.0.0.1", "http://",
                               "https://", ".invalid", "/fail/"}) {
            if (message.find(bad) != std::string::npos) {
                std::printf("     message carries \"%s\": %s\n", bad, message.c_str());
                return false;
            }
        }
        return !message.empty();
    }

    // runs one service call to its callback, pumping the job system like the app's frame loop
    template<typename Outcome>
    std::shared_ptr<Outcome> wait(JobSystem &jobs, const std::function<void(std::function<void(Outcome &)>)> &start,
                                  double limit = 60) {
        auto out = std::make_shared<Outcome>();
        auto done = std::make_shared<bool>(false);
        start([out, done](Outcome &o) {
            *out = o;
            *done = true;
        });
        double t0 = now();
        while (!*done && now() - t0 < limit) {
            jobs.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        return *done ? out : nullptr;
    }

    iptv::Profile xtream(const std::string &server) {
        iptv::Profile p;
        p.id = runId("xt");
        p.type = iptv::SourceType::Xtream;
        p.name = "Xtream";
        p.server = server;
        p.username = "exampleuser";
        p.password = "examplepass";
        return p;
    }

    struct Guide {
        iptv::Profile profile;
        iptv::LiveCatalog live;
    };

    Guide guide(const std::string &name, const std::string &url, int channels) {
        Guide g;
        g.profile.id = runId("guide-" + name);
        g.profile.type = iptv::SourceType::M3u;
        std::string m = "#EXTM3U url-tvg=\"" + url + "\"\n";
        for (int i = 0; i < channels; i++) {
            m += "#EXTINF:-1 tvg-id=\"ch" + std::to_string(i) + ".example\",Channel " + std::to_string(i) + "\n"
                 "http://stream.example.com/" + std::to_string(i) + ".ts\n";
        }
        m3u::Info info;
        m3u::build(g.profile.id, m, g.live, info);
        return g;
    }

    long residentKiB() {
        FILE *f = std::fopen("/proc/self/statm", "r");
        long pages = 0, resident = 0;
        if (f) {
            if (std::fscanf(f, "%ld %ld", &pages, &resident) != 2) {
                resident = 0;
            }
            std::fclose(f);
        }
        return resident * (sysconf(_SC_PAGESIZE) / 1024);
    }
}

TEST(failure_it_http_layer) {
    if (server().empty()) {
        std::printf("  (skipped: PS4IPTV_TEST_SERVER not set)\n");
        return;
    }
    http::globalInit("", "PS4IPTV-test");
    const std::string query = "?username=exampleuser&password=examplepass";
    struct Case {
        const char *mode;
        long stallS;
        size_t maxBytes;
        int retries;
        http::Error expect;
    };
    const Case cases[] = {
            {"dns", 15, 0, 0, http::Error::Dns},
            {"refused", 15, 0, 0, http::Error::Connect},
            {"reset", 15, 0, 0, http::Error::Interrupted},
            {"truncated", 15, 0, 0, http::Error::Interrupted},
            {"stall", 2, 0, 0, http::Error::Timeout},   // libcurl averages the speed over seconds: total limit below
            {"big", 15, 1u << 20, 0, http::Error::TooLarge},
    };
    for (const Case &c: cases) {
        http::Request r;
        r.url = base(c.mode) + "/x.json" + query;
        r.stallTimeoutS = c.stallS;
        r.connectTimeoutMs = 5000;
        r.totalTimeoutMs = 8000;   // the stalled answer: whichever limit comes first, never longer
        r.retries = c.retries;
        if (c.maxBytes) {
            r.maxBytes = c.maxBytes;
        }
        double t0 = now();
        http::Response resp = http::get(r);
        double took = now() - t0;
        if (resp.error != c.expect) {
            std::printf("     %s: error %d, status %ld, %s\n", c.mode, (int) resp.error, resp.status, resp.detail.c_str());
        }
        CHECK(resp.error == c.expect);
        CHECK(!resp.ok() && took < 12);
        CHECK(clean(http::describe(resp)));
    }
    // a redirect loop ends, and is an error
    {
        http::Request r;
        r.url = base("loop") + "/x.json" + query;
        r.retries = 0;
        http::Response resp = http::get(r);
        CHECK(!resp.ok() && resp.error != http::Error::None);
        CHECK(clean(http::describe(resp)));
    }
    // canceled while a slow answer arrives: stops at once
    {
        http::Request r;
        r.url = base("slow") + "/x.json";
        r.cancel = std::make_shared<std::atomic<bool>>(false);
        auto flag = r.cancel;
        std::thread stopper([flag] {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            flag->store(true);
        });
        double t0 = now();
        http::Response resp = http::get(r);
        stopper.join();
        CHECK(resp.error == http::Error::Canceled);
        CHECK(now() - t0 < 3);
    }
    // temporary server errors are tried again (twice), a missing page is not
    {
        http::Request r;
        r.url = base("status503") + "/x.json";
        http::Response resp = http::get(r);
        CHECK(resp.status == 503 && resp.attempts == 3 && !resp.ok());
        r.url = base("status404") + "/x.json";
        resp = http::get(r);
        CHECK(resp.status == 404 && resp.attempts == 1);
        CHECK(clean(http::describe(resp)));
    }
    // malformed bodies are delivered as they are (the caller's parser rejects them), never more than allowed
    {
        http::Request r;
        r.url = base("garbage") + "/x.json";
        http::Response resp = http::get(r);
        CHECK(resp.ok() && resp.body.size() == 2048);
        r.url = base("empty") + "/x.json";
        resp = http::get(r);
        CHECK(resp.ok() && resp.body.empty());
    }
}

TEST(failure_it_every_source_type) {
    if (server().empty()) {
        std::printf("  (skipped: PS4IPTV_TEST_SERVER not set)\n");
        return;
    }
    diag::init(logDir());   // the log of everything below is checked at the end (failure_it_log_privacy)
    http::globalInit("", "PS4IPTV-test");
    JobSystem jobs;
    jobs.setWorkerExitHook(http::releaseThreadResources);
    jobs.start(4);
    const std::string dir = dataDir();
    XtreamService xt(jobs);
    M3uService m3u(jobs);
    StalkerService stalker(jobs, dir);
    MediaBrowserService media(jobs, "0.4.1");
    StremioService addons(jobs);

    const char *modes[] = {"dns", "refused", "reset", "truncated", "garbage", "html", "empty", "big", "loop",
                           "status500", "status401", "status403"};
    int calls = 0;
    double slowest = 0;
    std::string mode;
    auto timed = [&](double t0, const char *what) {
        double took = now() - t0;
        if (took > 20) {
            std::printf("     slow: %s under %s took %.1f s\n", what, mode.c_str(), took);
        }
        slowest = std::max(slowest, took);
        calls++;
    };
    for (const char *m: modes) {
        mode = m;
        const std::string b = base(mode);
        double t0 = now();
        // Xtream: sign-in and the live list
        iptv::Profile p = xtream(b);
        auto auth = wait<sources::AuthOutcome>(jobs, [&](std::function<void(sources::AuthOutcome &)> cb) {
            xt.authenticate(p, [cb](const sources::AuthOutcome &o) {
                sources::AuthOutcome c = o;
                cb(c);
            });
        });
        CHECK(auth && auth->result.status != iptv::AuthStatus::Ok && clean(auth->message));
        timed(t0, "xtream sign-in");
        t0 = now();
        auto live = wait<sources::LiveOutcome>(jobs, [&](std::function<void(sources::LiveOutcome &)> cb) {
            xt.loadLive(p, dir, [cb](sources::LiveOutcome &o) { cb(o); });
        });
        CHECK(live && !live->ok && clean(live->message));
        timed(t0, "xtream live");

        // M3U playlist
        t0 = now();
        iptv::Profile pl;
        pl.id = runId(std::string("m3u-") + mode);
        pl.type = iptv::SourceType::M3u;
        pl.playlistUrl = b + "/get.php?username=exampleuser&password=examplepass&type=m3u_plus";
        auto list = wait<M3uService::Outcome>(jobs, [&](std::function<void(M3uService::Outcome &)> cb) {
            m3u.load(pl, dir, M3uService::Source::Network, true, cb);
        });
        CHECK(list && !list->ok && clean(list->message));
        if (list && (std::string(mode) == "dns" || std::string(mode) == "refused")) {
            CHECK(list->networkFailure);
        }
        timed(t0, "m3u");

        // XMLTV guide named by a playlist
        t0 = now();
        {
            XmltvService guides(jobs, dir);
            Guide g = guide(mode, b + "/guide.xml?username=exampleuser&password=examplepass", 2);
            auto a = wait<sources::GuideOutcome>(jobs, [&](std::function<void(sources::GuideOutcome &)> cb) {
                guides.load(g.profile, g.live, g.live.channels().front(), false, cb);
            });
            if (!a || a->ok || !a->programmes.empty()) {
                std::printf("     xmltv under %s: %s\n", mode.c_str(), !a ? "no answer" : a->ok ? "ok" : "programmes");
            }
            CHECK(a && !a->ok && a->programmes.empty());
            CHECK(!fs::exists(guides.guidePath(g.profile.id)));
            guides.reset();      // nothing of this guide may answer a service that is gone
            jobs.waitIdle();
            jobs.pump();
        }
        timed(t0, "xmltv");

        // Stalker / MAG portal
        t0 = now();
        iptv::Profile portal;
        portal.id = runId(std::string("stalker-") + mode);
        portal.type = iptv::SourceType::Stalker;
        portal.server = b + "/stalker_portal/c/";
        portal.mac = "00:1A:79:00:00:09";
        auto sp = wait<sources::AuthOutcome>(jobs, [&](std::function<void(sources::AuthOutcome &)> cb) {
            stalker.authenticate(portal, [cb](const sources::AuthOutcome &o) {
                sources::AuthOutcome c = o;
                cb(c);
            });
        });
        CHECK(sp && sp->result.status != iptv::AuthStatus::Ok && clean(sp->message));
        timed(t0, "stalker");

        // Jellyfin / Emby
        t0 = now();
        iptv::Profile jf;
        jf.id = runId(std::string("jf-") + mode);
        jf.type = iptv::SourceType::Jellyfin;
        jf.server = b;
        jf.username = "exampleuser";
        jf.password = "examplepass";
        auto ja = wait<sources::AuthOutcome>(jobs, [&](std::function<void(sources::AuthOutcome &)> cb) {
            media.authenticate(jf, [cb](const sources::AuthOutcome &o) {
                sources::AuthOutcome c = o;
                cb(c);
            });
        });
        CHECK(ja && ja->result.status != iptv::AuthStatus::Ok && clean(ja->message) && !media.signedIn(jf));
        timed(t0, "jellyfin");

        // Stremio add-on
        t0 = now();
        iptv::Profile ad;
        ad.id = runId(std::string("addon-") + mode);
        ad.type = iptv::SourceType::Stremio;
        ad.playlistUrl = b + "/manifest.json";
        auto aa = wait<sources::AuthOutcome>(jobs, [&](std::function<void(sources::AuthOutcome &)> cb) {
            addons.authenticate(ad, [cb](const sources::AuthOutcome &o) {
                sources::AuthOutcome c = o;
                cb(c);
            });
        });
        CHECK(aa && aa->result.status != iptv::AuthStatus::Ok && clean(aa->message));
        timed(t0, "stremio");
    }
    std::printf("     %d failing calls, slowest %.1f s\n", calls, slowest);
    CHECK(slowest < 45);

    // canceled while a source answers slowly: no callback, the job ends
    {
        iptv::Profile pl;
        pl.id = runId("m3u-cancel");
        pl.type = iptv::SourceType::M3u;
        pl.playlistUrl = base("slow") + "/list.m3u";
        bool called = false;
        CancelToken t = m3u.load(pl, dir, M3uService::Source::Network, false, [&called](M3uService::Outcome &) {
            called = true;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        t.cancel();
        double t0 = now();
        jobs.waitIdle();
        jobs.pump();
        std::printf("     canceled playlist: callback %s, job ended after %.2f s\n", called ? "called" : "not called",
                    now() - t0);
        CHECK(!called && now() - t0 < 5);
    }
    jobs.stop();
}

TEST(failure_it_media_server_sessions) {
    if (server().empty()) {
        return;
    }
    http::globalInit("", "PS4IPTV-test");
    JobSystem jobs;
    jobs.setWorkerExitHook(http::releaseThreadResources);
    jobs.start(4);
    MediaBrowserService media(jobs, "0.4.1");
    const std::string dir = dataDir();
    control("/_reset");
    auto signIn = [&](iptv::Profile &p) {
        return wait<sources::AuthOutcome>(jobs, [&](std::function<void(sources::AuthOutcome &)> cb) {
            media.authenticate(p, [cb](const sources::AuthOutcome &o) {
                sources::AuthOutcome c = o;
                cb(c);
            });
        });
    };
    auto movies = [&](const iptv::Profile &p, sources::Source source) {
        return wait<sources::MoviesOutcome>(jobs, [&](std::function<void(sources::MoviesOutcome &)> cb) {
            media.loadMovies(p, dir, source, cb);
        });
    };
    iptv::Profile p;
    p.id = runId("jf-session");
    p.type = iptv::SourceType::Jellyfin;
    p.server = server() + "/jf";
    p.username = "exampleuser";
    p.password = "examplepass";
    auto ok = signIn(p);
    CHECK(ok && ok->result.status == iptv::AuthStatus::Ok);

    // the token expires on the server (signed out elsewhere, password changed): a clear message, then a new sign-in
    control("/_jfexpire");
    auto expired = movies(p, sources::Source::Network);
    CHECK(expired && !expired->ok && clean(expired->message));
    auto details = wait<sources::InfoOutcome<iptv::MovieInfo>>(jobs,
            [&](std::function<void(sources::InfoOutcome<iptv::MovieInfo> &)> cb) { media.loadMovieInfo(p, "m5", cb); });
    CHECK(details && !details->ok && clean(details->message));
    control("/_reset");
    auto again = signIn(p);
    CHECK(again && again->result.status == iptv::AuthStatus::Ok);
    auto fine = movies(p, sources::Source::Network);
    CHECK(fine && fine->ok && fine->catalog && fine->catalog->size() == 1203);

    // the server goes away while playback is prepared: details fail cleanly, progress reports never block or crash
    control("/_jfdown");
    auto gone = wait<sources::InfoOutcome<iptv::MovieInfo>>(jobs,
            [&](std::function<void(sources::InfoOutcome<iptv::MovieInfo> &)> cb) { media.loadMovieInfo(p, "m5", cb); });
    CHECK(gone && !gone->ok && clean(gone->message));
    CHECK(!media.streamUrl(p, "m5").empty());   // the address is built locally: mpv reports what it finds
    double t0 = now();
    media.report(p, mediabrowser::Report::Start, "m5", "ps9", 0, false);
    media.report(p, mediabrowser::Report::Stop, "m5", "ps9", 3, false);
    CHECK(now() - t0 < 0.5);   // never on the caller's thread
    jobs.waitIdle();
    jobs.pump();
    auto saved = movies(p, sources::Source::Cache);   // offline: the saved list still opens
    CHECK(saved && saved->ok && saved->catalog && saved->catalog->size() == 1203);
    control("/_reset");

    // a server without libraries: an empty list, not an error that never ends
    iptv::Profile empty = p;
    empty.id = runId("jf-empty");
    empty.server = server() + "/jfempty";
    CHECK(signIn(empty) && media.signedIn(empty));
    auto none = movies(empty, sources::Source::Network);
    CHECK(none && (none->ok ? (none->catalog && none->catalog->size() == 0) : clean(none->message)));

    // malformed items and missing artwork: the usable ones are listed, the rest skipped, paging ends
    iptv::Profile odd = p;
    odd.id = runId("jf-odd");
    odd.server = server() + "/jfodd";
    CHECK(signIn(odd) && media.signedIn(odd));
    int before = requests("/jfodd/Users/u1/Items");
    auto list = movies(odd, sources::Source::Network);
    CHECK(list && list->ok && list->catalog);
    if (list && list->catalog) {
        const iptv::Movie *m = list->catalog->find("odd4");
        CHECK(m != nullptr && m->title == "Fine" && m->icon.empty());
        CHECK(list->catalog->find("odd5") != nullptr);
        CHECK(list->catalog->size() <= 4);
    }
    CHECK(requests("/jfodd/Users/u1/Items") - before <= 3);
    auto info = wait<sources::InfoOutcome<iptv::MovieInfo>>(jobs,
            [&](std::function<void(sources::InfoOutcome<iptv::MovieInfo> &)> cb) { media.loadMovieInfo(odd, "odd4", cb); });
    CHECK(info && (info->ok ? info->info.durationSeconds >= 0 : clean(info->message)));
    jobs.stop();
}

TEST(failure_it_addon_answers) {
    if (server().empty()) {
        return;
    }
    http::globalInit("", "PS4IPTV-test");
    JobSystem jobs;
    jobs.setWorkerExitHook(http::releaseThreadResources);
    jobs.start(4);
    StremioService addons(jobs);
    const std::string dir = dataDir();
    control("/_reset");
    auto profile = [&](const std::string &variant) {
        iptv::Profile p;
        p.id = runId("addon-" + variant);
        p.type = iptv::SourceType::Stremio;
        p.playlistUrl = server() + "/" + variant + "/manifest.json";
        return p;
    };
    auto signIn = [&](const iptv::Profile &p) {
        return wait<sources::AuthOutcome>(jobs, [&](std::function<void(sources::AuthOutcome &)> cb) {
            addons.authenticate(p, [cb](const sources::AuthOutcome &o) {
                sources::AuthOutcome c = o;
                cb(c);
            });
        });
    };
    auto resolve = [&](const iptv::Profile &p, const std::string &id) {
        return wait<sources::LinkOutcome>(jobs, [&](std::function<void(sources::LinkOutcome &)> cb) {
            addons.resolve(p, false, id, [cb](const sources::LinkOutcome &o) {
                sources::LinkOutcome c = o;
                cb(c);
            });
        });
    };
    // invalid manifests: refused with a message
    for (const char *v: {"addonbad", "addonnotjson"}) {
        auto a = signIn(profile(v));
        CHECK(a && a->result.status != iptv::AuthStatus::Ok && clean(a->message));
    }
    // empty catalogs: an empty list
    iptv::Profile empty = profile("addonempty");
    CHECK(signIn(empty) && signIn(empty)->result.status == iptv::AuthStatus::Ok);
    auto none = wait<sources::MoviesOutcome>(jobs, [&](std::function<void(sources::MoviesOutcome &)> cb) {
        addons.loadMovies(empty, dir, sources::Source::Network, cb);
    });
    CHECK(none && (none->ok ? (none->catalog && none->catalog->size() == 0) : clean(none->message)));
    // metas and streams failing
    iptv::Profile failing = profile("addonfail");
    CHECK(signIn(failing) && signIn(failing)->result.status == iptv::AuthStatus::Ok);
    auto meta = wait<sources::InfoOutcome<iptv::MovieInfo>>(jobs,
            [&](std::function<void(sources::InfoOutcome<iptv::MovieInfo> &)> cb) {
                addons.loadMovieInfo(failing, "tt1000005", cb);
            });
    CHECK(meta && !meta->ok && clean(meta->message));
    auto broken = resolve(failing, "tt1000005");
    CHECK(broken && !broken->ok && clean(broken->message));
    // only torrents / only YouTube: nothing V7 can play, said clearly (no torrent client, no YouTube scraping)
    for (const char *v: {"addontorrent", "addonyt"}) {
        iptv::Profile p = profile(v);
        CHECK(signIn(p) && signIn(p)->result.status == iptv::AuthStatus::Ok);
        auto l = resolve(p, "tt1000005");
        CHECK(l && !l->ok && clean(l->message) && l->url.empty());
    }
    // several streams: the first direct address
    iptv::Profile multi = profile("addonmulti");
    CHECK(signIn(multi) && signIn(multi)->result.status == iptv::AuthStatus::Ok);
    auto picked = resolve(multi, "tt1000005");
    CHECK(picked && picked->ok && picked->url.find("pick=first") != std::string::npos);
    // the add-on goes away mid-session: a message; the saved list still opens
    iptv::Profile p = profile("addon");
    CHECK(signIn(p) && signIn(p)->result.status == iptv::AuthStatus::Ok);
    auto first = wait<sources::MoviesOutcome>(jobs, [&](std::function<void(sources::MoviesOutcome &)> cb) {
        addons.loadMovies(p, dir, sources::Source::Network, cb);
    });
    CHECK(first && first->ok);
    control("/_addondown");
    auto down = wait<sources::MoviesOutcome>(jobs, [&](std::function<void(sources::MoviesOutcome &)> cb) {
        addons.loadMovies(p, dir, sources::Source::Network, cb);
    });
    CHECK(down && !down->ok && clean(down->message));
    auto link = resolve(p, "tt1000005");
    CHECK(link && !link->ok && clean(link->message));
    auto saved = wait<sources::MoviesOutcome>(jobs, [&](std::function<void(sources::MoviesOutcome &)> cb) {
        addons.loadMovies(p, dir, sources::Source::Cache, cb);
    });
    CHECK(saved && saved->ok && saved->catalog && saved->catalog->size() == 237);
    control("/_reset");
    jobs.stop();
}

TEST(failure_it_guides_and_playlists) {
    if (server().empty()) {
        return;
    }
    http::globalInit("", "PS4IPTV-test");
    JobSystem jobs;
    jobs.setWorkerExitHook(http::releaseThreadResources);
    jobs.start(4);
    const std::string dir = dataDir();
    // a guide and a playlist sent chunked (no Content-Length)
    {
        XmltvService guides(jobs, dir);
        Guide g = guide("chunked", server() + "/xmltv/chunked/4.xml", 4);
        auto a = wait<sources::GuideOutcome>(jobs, [&](std::function<void(sources::GuideOutcome &)> cb) {
            guides.load(g.profile, g.live, g.live.channels()[1], false, cb);
        });
        CHECK(a && a->ok && !a->programmes.empty());
        if (a && a->ok) {
            epg::NowNext nn = epg::nowNext(a->programmes, clockx::unixNow());
            CHECK(nn.now >= 0);
        }
    }
    {
        M3uService m3u(jobs);
        iptv::Profile pl;
        pl.id = runId("m3u-chunked");
        pl.type = iptv::SourceType::M3u;
        pl.playlistUrl = server() + "/m3u/chunked/50.m3u";
        auto list = wait<M3uService::Outcome>(jobs, [&](std::function<void(M3uService::Outcome &)> cb) {
            m3u.load(pl, dir, M3uService::Source::Network, false, cb);
        });
        CHECK(list && list->ok && list->catalog && list->catalog->channels().size() == 50);
    }
    // a guide whose programmes all ended: nothing is on air, nothing is made up
    {
        XmltvService guides(jobs, dir);
        Guide g = guide("expired", server() + "/xmltv/expired/4.xml", 4);
        auto a = wait<sources::GuideOutcome>(jobs, [&](std::function<void(sources::GuideOutcome &)> cb) {
            guides.load(g.profile, g.live, g.live.channels()[1], false, cb);
        });
        CHECK(a != nullptr);
        if (a) {
            epg::NowNext nn = epg::nowNext(a->programmes, clockx::unixNow());
            CHECK(nn.now < 0 && nn.next < 0);
            for (const auto &prog: a->programmes) {
                CHECK(prog.stop > clockx::unixNow());   // only programmes that have not ended are kept
            }
        }
    }
    // a large guide (1500 channels, 150 000 programmes, ~35 MB) for a playlist with 3 channels: streamed, the saved
    // guide holds those 3 channels only, memory stays bounded
    {
        long before = residentKiB();
        XmltvService guides(jobs, dir);
        Guide g = guide("large", server() + "/xmltv/plain/1500.xml", 3);
        auto a = wait<sources::GuideOutcome>(jobs, [&](std::function<void(sources::GuideOutcome &)> cb) {
            guides.load(g.profile, g.live, g.live.channels()[2], false, cb);
        }, 120);
        CHECK(a && a->ok && !a->programmes.empty());
        long grown = residentKiB() - before;
        int64_t saved = fs::fileSize(guides.guidePath(g.profile.id));
        std::printf("     large guide: resident memory +%ld KiB, saved guide %lld bytes\n", grown, (long long) saved);
        CHECK(saved > 0 && saved < 256 * 1024);
        CHECK(grown < 64 * 1024);
    }
    jobs.stop();
}

TEST(failure_it_offline_start_with_saved_lists) {
    if (server().empty()) {
        return;
    }
    http::globalInit("", "PS4IPTV-test");
    JobSystem jobs;
    jobs.setWorkerExitHook(http::releaseThreadResources);
    jobs.start(4);
    const std::string dir = dataDir();
    // Xtream: signed in and loaded once; then the provider cannot be reached: the saved live list opens
    XtreamService xt(jobs);
    iptv::Profile p = xtream(server() + "/xt");
    auto auth = wait<sources::AuthOutcome>(jobs, [&](std::function<void(sources::AuthOutcome &)> cb) {
        xt.authenticate(p, [cb](const sources::AuthOutcome &o) {
            sources::AuthOutcome c = o;
            cb(c);
        });
    });
    CHECK(auth && auth->result.status == iptv::AuthStatus::Ok);
    auto live = wait<sources::LiveOutcome>(jobs, [&](std::function<void(sources::LiveOutcome &)> cb) {
        xt.loadLive(p, dir, [cb](sources::LiveOutcome &o) { cb(o); });
    });
    CHECK(live && live->ok && !live->fromCache && live->channels.size() == 12);
    iptv::Profile offline = p;
    offline.server = base("refused");
    auto again = wait<sources::LiveOutcome>(jobs, [&](std::function<void(sources::LiveOutcome &)> cb) {
        xt.loadLive(offline, dir, [cb](sources::LiveOutcome &o) { cb(o); });
    });
    CHECK(again && again->ok && again->fromCache && again->channels.size() == 12);
    auto cacheOnly = wait<sources::LiveOutcome>(jobs, [&](std::function<void(sources::LiveOutcome &)> cb) {
        xt.loadLive(offline, dir, [cb](sources::LiveOutcome &o) { cb(o); }, true);
    });
    CHECK(cacheOnly && cacheOnly->ok && cacheOnly->channels.size() == 12);
    // a wrong password is not a network failure
    iptv::Profile wrong = p;
    wrong.password = "wrong";
    auto refused = wait<sources::AuthOutcome>(jobs, [&](std::function<void(sources::AuthOutcome &)> cb) {
        xt.authenticate(wrong, [cb](const sources::AuthOutcome &o) {
            sources::AuthOutcome c = o;
            cb(c);
        });
    });
    CHECK(refused && refused->result.status == iptv::AuthStatus::InvalidCredentials && clean(refused->message));

    // M3U: the saved playlist opens without the network
    M3uService m3u(jobs);
    iptv::Profile pl;
    pl.id = runId("m3u-offline");
    pl.type = iptv::SourceType::M3u;
    pl.playlistUrl = server() + "/m3u/plain/20.m3u";
    auto first = wait<M3uService::Outcome>(jobs, [&](std::function<void(M3uService::Outcome &)> cb) {
        m3u.load(pl, dir, M3uService::Source::Network, true, cb);
    });
    CHECK(first && first->ok);
    pl.playlistUrl = base("dns") + "/20.m3u";
    auto net = wait<M3uService::Outcome>(jobs, [&](std::function<void(M3uService::Outcome &)> cb) {
        m3u.load(pl, dir, M3uService::Source::Network, true, cb);
    });
    CHECK(net && !net->ok && net->networkFailure);
    auto saved = wait<M3uService::Outcome>(jobs, [&](std::function<void(M3uService::Outcome &)> cb) {
        m3u.load(pl, dir, M3uService::Source::Cache, false, cb);
    });
    CHECK(saved && saved->ok && saved->catalog && saved->catalog->channels().size() == 20);
    jobs.stop();
}

// last (tests run in file order): everything above wrote to this log
TEST(failure_it_log_privacy) {
    if (server().empty()) {
        return;
    }
    diag::shutdown();
    int files = 0;
    for (const auto &f: fs::listDir(logDir())) {
        std::string text;
        fs::readFile(fs::join(logDir(), f.name), text, 64u << 20);
        files++;
        for (const char *secret: {"examplepass", "exampleuser", "exampletoken", "00:1A:79:00:00:09", "00-1A-79"}) {
            size_t at = text.find(secret);
            if (at != std::string::npos) {
                size_t line = text.rfind('\n', at);
                std::printf("     %s in the log: %s\n", secret,
                            text.substr(line == std::string::npos ? 0 : line + 1, 200).c_str());
            }
            CHECK(at == std::string::npos);
        }
    }
    CHECK(files > 0);
}
