// XMLTV guide integration test: the app's XmltvService + HTTP client (streaming sink, speed cap) + gzip inflater +
// saved guide, against scripts/download-test-server.py on 127.0.0.1 - a .xml.gz file, plain XML, Content-Encoding,
// HTTP 404, an HTML answer, cancellation; the guide downloaded once and then read from the saved guide (also by a new
// service, as after a restart); a failed download not tried again; changed guide addresses. Plus the inflater on its
// own (pieces, concatenated members, padding, damaged data, limits). Run by scripts/test-download-integration.sh.

#include <zlib.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "check.h"
#include "../../src/app/xmltv_service.h"
#include "../../src/iptv/m3u.h"
#include "../../src/network/gunzip.h"
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
        std::string d = fs::join(base ? base : ".", "xmltv-it/");
        fs::ensureDir(d);
        return d;
    }

    // a profile id of its own per run: saved guides / failure notes of an earlier run never count
    std::string profileId(const std::string &name) {
        static std::string run = std::to_string(clockx::unixNow());
        return "xmltv-it-" + name + "-" + run;
    }

    double now() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    std::string gzipOf(const std::string &in) {
        z_stream z{};
        deflateInit2(&z, 6, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
        std::string out(deflateBound(&z, (uLong) in.size()) + 32, '\0');
        z.next_in = (Bytef *) in.data();
        z.avail_in = (uInt) in.size();
        z.next_out = (Bytef *) &out[0];
        z.avail_out = (uInt) out.size();
        deflate(&z, Z_FINISH);
        out.resize(z.total_out);
        deflateEnd(&z);
        return out;
    }

    int guideRequests() {
        http::Request r;
        r.url = server() + "/_log";
        http::Response resp = http::get(r);
        int n = 0;
        for (size_t at = resp.body.find("/xmltv/"); at != std::string::npos; at = resp.body.find("/xmltv/", at + 1)) {
            n++;
        }
        return n;
    }

    struct Playlist {
        iptv::Profile profile;
        iptv::LiveCatalog live;
    };

    Playlist playlist(const std::string &name, const std::string &guidePath, int channels) {
        Playlist p;
        p.profile.id = profileId(name);
        p.profile.type = iptv::SourceType::M3u;
        p.profile.name = name;
        std::string m = "#EXTM3U url-tvg=\"" + server() + guidePath + "\"\n";
        for (int i = 0; i < channels; i++) {
            m += "#EXTINF:-1 tvg-id=\"ch" + std::to_string(i) + ".example\",Channel " + std::to_string(i) + "\n"
                 "http://stream.example.com/" + std::to_string(i) + ".ts\n";
        }
        m += "#EXTINF:-1 tvg-id=\"nope.example\",Nope\nhttp://stream.example.com/nope.ts\n";
        m3u::Info info;
        m3u::build(p.profile.id, m, p.live, info);
        return p;
    }

    const iptv::LiveChannel &channel(const Playlist &p, const std::string &name) {
        for (const auto &c: p.live.channels()) {
            if (c.name == name) {
                return c;
            }
        }
        return p.live.channels().front();
    }

    struct Answer {
        bool done = false;
        sources::GuideOutcome outcome;
    };

    std::shared_ptr<Answer> ask(JobSystem &jobs, XmltvService &svc, const Playlist &p, const std::string &name,
                                bool offline = false, double limit = 90) {
        auto a = std::make_shared<Answer>();
        svc.load(p.profile, p.live, channel(p, name), offline, [a](sources::GuideOutcome &o) {
            a->outcome = o;
            a->done = true;
        });
        double t0 = now();
        while (!a->done && now() - t0 < limit) {
            jobs.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return a;
    }

    bool onAir(const sources::GuideOutcome &o, const std::string &title) {
        int64_t t = clockx::unixNow();
        epg::NowNext nn = epg::nowNext(o.programmes, t);
        return nn.now >= 0 && o.programmes[(size_t) nn.now].title == title && nn.next >= 0;
    }
}

TEST(xmltv_it_inflater) {
    std::string xml = "<tv>";
    for (int i = 0; i < 2000; i++) {
        xml += "<programme start=\"20251117200000\" channel=\"c\"><title>T" + std::to_string(i) + "</title></programme>";
    }
    xml += "</tv>";
    std::string gz = gzipOf(xml);
    CHECK(gz::isGzip(gz.data(), gz.size()) && !gz::isGzip(xml.data(), xml.size()));
    for (size_t step: {(size_t) 1, (size_t) 7, (size_t) 4096, gz.size()}) {
        gz::Inflater inf(1 << 20);
        std::string out;
        bool ok = true;
        for (size_t i = 0; i < gz.size() && ok; i += step) {
            ok = inf.feed(gz.data() + i, std::min(step, gz.size() - i), [&out](const char *d, size_t n) {
                out.append(d, n);
                return true;
            });
        }
        CHECK(ok && inf.complete() && out == xml);
    }
    // two members one after the other, then padding
    std::string two = gzipOf("first|") + gzipOf("second") + std::string(16, '\0');
    gz::Inflater inf2(1 << 20);
    std::string out2;
    CHECK(inf2.feed(two.data(), two.size(), [&out2](const char *d, size_t n) {
        out2.append(d, n);
        return true;
    }));
    CHECK(inf2.complete() && out2 == "first|second");
    // damaged data, a cut file, too much output, a stop from the reader
    std::string bad = gz;
    for (size_t i = 20; i < 60 && i < bad.size(); i++) {
        bad[i] = (char) (bad[i] ^ 0xA5);
    }
    gz::Inflater inf3(1 << 20);
    bool fed = inf3.feed(bad.data(), bad.size(), [](const char *, size_t) { return true; });
    CHECK(!fed || !inf3.complete());
    gz::Inflater inf4(1 << 20);
    CHECK(inf4.feed(gz.data(), gz.size() / 2, [](const char *, size_t) { return true; }) && !inf4.complete());
    gz::Inflater inf5(1000);
    CHECK(!inf5.feed(gz.data(), gz.size(), [](const char *, size_t) { return true; }));
    CHECK(inf5.error() == "too large once decompressed");
    gz::Inflater inf6(1 << 20);
    CHECK(!inf6.feed(gz.data(), gz.size(), [](const char *, size_t) { return false; }));
}

TEST(xmltv_it_service) {
    if (server().empty()) {
        std::printf("  (skipped: PS4IPTV_TEST_SERVER not set)\n");
        return;
    }
    if (const char *logs = std::getenv("PS4IPTV_TEST_LOG")) {
        diag::init(logs);   // the log lines the service writes (checked for addresses by the caller)
    }
    http::globalInit("", "PS4IPTV-test");
    JobSystem jobs;
    jobs.setWorkerExitHook(http::releaseThreadResources);
    jobs.start(4);
    std::string dir = dataDir();

    // a .xml.gz guide: downloaded once, then every channel is read from the saved guide
    // (credential-like query: only "host/…/30.xml.gz" may reach the log)
    Playlist gzp = playlist("gz", "/xmltv/gzipfile/30.xml.gz?username=exampleuser&password=examplepass", 30);
    CHECK_EQ(gzp.live.guideUrls.size(), (size_t) 1);
    int before = guideRequests();
    {
        XmltvService svc(jobs, dir);
        auto a = ask(jobs, svc, gzp, "Channel 3");
        CHECK(a->done && a->outcome.ok);
        CHECK(onAir(a->outcome, "Show 3-0 & co"));
        CHECK(!a->outcome.programmes.empty() && a->outcome.programmes[0].description == "About show 0 of channel 3.");
        CHECK(a->outcome.programmes.size() <= epg::MAX_PROGRAMMES);
        auto b = ask(jobs, svc, gzp, "Channel 29");
        CHECK(b->done && b->outcome.ok && onAir(b->outcome, "Show 29-0 & co"));
        auto none = ask(jobs, svc, gzp, "Nope");   // not in the guide: no programme information, no request
        CHECK(none->done && none->outcome.ok && none->outcome.programmes.empty());
        CHECK_EQ(guideRequests() - before, 1);
    }
    CHECK(fs::exists(XmltvService(jobs, dir).guidePath(gzp.profile.id)));
    {
        XmltvService restarted(jobs, dir);   // as after a restart: the saved guide answers
        auto c = ask(jobs, restarted, gzp, "Channel 5");
        CHECK(c->done && c->outcome.ok && onAir(c->outcome, "Show 5-0 & co"));
        auto off = ask(jobs, restarted, gzp, "Channel 6", true);
        CHECK(off->done && off->outcome.ok && onAir(off->outcome, "Show 6-0 & co"));
        CHECK_EQ(guideRequests() - before, 1);
    }

    // plain XML and Content-Encoding: gzip
    for (const char *path: {"/xmltv/plain/4.xml", "/xmltv/encoded/4.xml"}) {
        Playlist p = playlist(path[7] == 'p' ? "plain" : "encoded", path, 4);
        XmltvService svc(jobs, dir);
        auto a = ask(jobs, svc, p, "Channel 2");
        CHECK(a->done && a->outcome.ok && onAir(a->outcome, "Show 2-0 & co"));
    }

    // 404 and an HTML page: "could not be loaded", and not asked again for an hour (also after a restart)
    for (const char *path: {"/xmltv/missing/4.xml", "/xmltv/html/4.xml"}) {
        Playlist p = playlist(path[7] == 'm' ? "missing" : "html", path, 4);
        int n0 = guideRequests();
        XmltvService svc(jobs, dir);
        auto a = ask(jobs, svc, p, "Channel 1");
        CHECK(a->done && !a->outcome.ok && a->outcome.programmes.empty());
        auto again = ask(jobs, svc, p, "Channel 2");
        CHECK(again->done && !again->outcome.ok);
        XmltvService restarted(jobs, dir);
        auto later = ask(jobs, restarted, p, "Channel 1");
        CHECK(later->done && !later->outcome.ok);
        CHECK_EQ(guideRequests() - n0, 1);
        CHECK(!fs::exists(svc.guidePath(p.profile.id)));
    }

    // offline without a saved guide: nothing is fetched
    {
        Playlist p = playlist("offline", "/xmltv/plain/4.xml", 4);
        int n0 = guideRequests();
        XmltvService svc(jobs, dir);
        auto a = ask(jobs, svc, p, "Channel 1", true);
        CHECK(a->done && !a->outcome.ok);
        CHECK_EQ(guideRequests() - n0, 0);
    }

    // the playlist names other guide addresses: built again from them
    {
        Playlist first = playlist("moved", "/xmltv/plain/3.xml", 3);
        Playlist moved = playlist("moved", "/xmltv/gzipfile/3.xml.gz", 3);
        int n0 = guideRequests();
        XmltvService svc(jobs, dir);
        CHECK(ask(jobs, svc, first, "Channel 1")->outcome.ok);
        CHECK(ask(jobs, svc, moved, "Channel 1")->outcome.ok);
        CHECK_EQ(guideRequests() - n0, 2);
    }

    // a sign-in elsewhere while a slow download runs: it stops, its waiting request is never answered
    {
        Playlist p = playlist("slow", "/xmltv/slow/200.xml", 200);
        XmltvService svc(jobs, dir);
        auto a = std::make_shared<Answer>();
        svc.load(p.profile, p.live, channel(p, "Channel 1"), false, [a](sources::GuideOutcome &o) {
            a->outcome = o;
            a->done = true;
        });
        double t0 = now();
        while (now() - t0 < 0.5) {
            jobs.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        svc.reset();
        jobs.waitIdle();
        jobs.pump();
        CHECK(!a->done);
        CHECK(!fs::exists(svc.guidePath(p.profile.id)));
    }
    jobs.stop();
}
