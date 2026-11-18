// Jellyfin / Emby integration test: the app's MediaBrowserService + HTTP client (GET with headers, POST, a redirect
// to the server's new address) against the media server API of scripts/download-test-server.py: a wrong password,
// "not a media server", sign-in, a 1203-movie library in pages of 500, series, details with the server's resume
// positions, the saved catalogs, the stream address, progress reports, an edited source, and a log without the
// token or the password. Run by scripts/test-download-integration.sh.

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "check.h"
#include "../../src/app/mediabrowser_service.h"
#include "../../src/network/http.h"
#include "../../src/platform/clock.h"
#include "../../src/platform/fs.h"
#include "../../src/platform/log.h"
#include "../../src/storage/catalog_cache.h"

namespace {

    std::string server() {
        const char *s = std::getenv("PS4IPTV_TEST_SERVER");
        return s ? s : "";
    }

    std::string dataDir() {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string d = fs::join(base ? base : ".", "mediaserver-it/");
        fs::ensureDir(d);
        return d;
    }

    double now() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    std::string serverLog() {
        http::Request r;
        r.url = server() + "/_log";
        return http::get(r).body;
    }

    void resetLog() {
        http::Request r;
        r.url = server() + "/_reset";
        http::get(r);
    }

    int count(const std::string &text, const std::string &part) {
        int n = 0;
        for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) {
            n++;
        }
        return n;
    }

    template<typename Outcome, typename Start>
    std::shared_ptr<Outcome> wait(JobSystem &jobs, Start start) {
        auto out = std::make_shared<Outcome>();
        auto done = std::make_shared<bool>(false);
        start([out, done](Outcome &o) {
            *out = o;
            *done = true;
        });
        double t0 = now();
        while (!*done && now() - t0 < 60) {
            jobs.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return *done ? out : nullptr;
    }

    std::shared_ptr<sources::AuthOutcome> signIn(JobSystem &jobs, MediaBrowserService &svc, const iptv::Profile &p) {
        return wait<sources::AuthOutcome>(jobs, [&](std::function<void(sources::AuthOutcome &)> cb) {
            svc.authenticate(p, [cb](const sources::AuthOutcome &o) {
                sources::AuthOutcome copy = o;
                cb(copy);
            });
        });
    }
}

TEST(mediaserver_it_end_to_end) {
    if (server().empty()) {
        std::printf("  (skipped: PS4IPTV_TEST_SERVER not set)\n");
        return;
    }
    if (const char *logs = std::getenv("PS4IPTV_TEST_LOG")) {
        diag::init(logs);
    }
    http::globalInit("", "PS4IPTV-test");
    JobSystem jobs;
    jobs.setWorkerExitHook(http::releaseThreadResources);
    jobs.start(4);
    MediaBrowserService svc(jobs, "0.4.1");
    std::string dir = dataDir();
    resetLog();

    iptv::Profile p;
    p.id = "jf-it-" + std::to_string(clockx::unixNow());
    p.type = iptv::SourceType::Jellyfin;
    p.name = "Test server";
    p.username = "exampleuser";

    // not a media server; a wrong password
    p.server = server() + "/m3u/html";
    auto notServer = signIn(jobs, svc, p);
    CHECK(notServer && notServer->result.status == iptv::AuthStatus::Malformed && !svc.signedIn(p));
    p.server = server() + "/jfmoved";   // the server moved: the address after the redirect is used from then on
    p.password = "wrong";
    auto wrong = signIn(jobs, svc, p);
    CHECK(wrong && wrong->result.status == iptv::AuthStatus::InvalidCredentials && !svc.signedIn(p));
    p.password = "examplepass";
    auto ok = signIn(jobs, svc, p);
    CHECK(ok && ok->result.status == iptv::AuthStatus::Ok && svc.signedIn(p));
    CHECK(ok && ok->result.account.message == "Jellyfin Server 10.10.3");
    CHECK_EQ(svc.serverName(p), std::string("Jellyfin Server 10.10.3"));

    // Movies: one library, three pages; then the saved copy
    auto movies = wait<sources::MoviesOutcome>(jobs, [&](std::function<void(sources::MoviesOutcome &)> cb) {
        svc.loadMovies(p, dir, sources::Source::Network, cb);
    });
    CHECK(movies && movies->ok && movies->catalog);
    if (movies && movies->catalog) {
        CHECK_EQ(movies->catalog->size(), (size_t) 1203);
        CHECK(movies->catalog->categories().size() == 1 && movies->catalog->categories()[0].name == "Movies");
        const iptv::Movie *m = movies->catalog->find("m7");
        CHECK(m != nullptr && m->title == "Movie 0007" && m->extension == "mkv" && m->year == 1997
              && m->icon.find("/jf/Items/m7/Images/Primary?tag=t7") != std::string::npos);
        const iptv::Movie *m8 = movies->catalog->find("m8");
        CHECK(m8 != nullptr && m8->extension == "mp4");
    }
    auto saved = wait<sources::MoviesOutcome>(jobs, [&](std::function<void(sources::MoviesOutcome &)> cb) {
        svc.loadMovies(p, dir, sources::Source::Cache, cb);
    });
    CHECK(saved && saved->ok && saved->catalog && saved->catalog->size() == 1203 && saved->catalog->fromCache);
    auto series = wait<sources::SeriesListOutcome>(jobs, [&](std::function<void(sources::SeriesListOutcome &)> cb) {
        svc.loadSeriesList(p, dir, sources::Source::Network, cb);
    });
    CHECK(series && series->ok && series->catalog && series->catalog->size() == 3);

    // details: the server's resume positions
    auto info = wait<sources::InfoOutcome<iptv::MovieInfo>>(jobs,
            [&](std::function<void(sources::InfoOutcome<iptv::MovieInfo> &)> cb) { svc.loadMovieInfo(p, "m5", cb); });
    CHECK(info && info->ok && info->info.plot == "About m5." && info->info.durationSeconds == 7200
          && info->info.resumeSeconds > 1799 && info->info.resumeSeconds < 1801);
    auto show = wait<sources::InfoOutcome<iptv::SeriesInfo>>(jobs,
            [&](std::function<void(sources::InfoOutcome<iptv::SeriesInfo> &)> cb) { svc.loadSeriesInfo(p, "s1", cb); });
    CHECK(show && show->ok && show->info.series.plot == "A show." && show->info.seasons.size() == 2);
    if (show && show->info.seasons.size() == 2) {
        const iptv::Season &s1 = show->info.seasons[0];
        CHECK(s1.number == 1 && s1.episodes.size() == 3 && s1.episodes[0].number == 1 && s1.episodes[2].number == 3);
        CHECK(show->info.seasons[1].episodes.size() == 3 && show->info.seasons[1].episodes[2].resumeSeconds > 599);
    }

    // playback: the file as it is, with the token; progress reports reach the server
    std::string url = svc.streamUrl(p, "m5");
    CHECK_EQ(url, server() + "/jf/Videos/m5/stream?static=true&api_key=exampletoken-jf&ApiKey=exampletoken-jf");
    svc.report(p, mediabrowser::Report::Start, "m5", "ps1", 0, false);
    svc.report(p, mediabrowser::Report::Progress, "m5", "ps1", 60.5, true);
    svc.report(p, mediabrowser::Report::Stop, "m5", "ps1", 61, false);
    jobs.waitIdle();
    jobs.pump();
    std::string log = serverLog();
    CHECK_EQ(count(log, "\"/jf/Sessions/Playing\""), 1);
    CHECK_EQ(count(log, "\"/jf/Sessions/Playing/Progress\""), 1);
    CHECK_EQ(count(log, "\"/jf/Sessions/Playing/Stopped\""), 1);
    CHECK(log.find("\"PositionTicks\": 605000000") != std::string::npos);
    CHECK(log.find("\"PlaySessionId\": \"ps1\"") != std::string::npos);
    CHECK_EQ(count(log, "\"path\": \"/jf/Users/u1/Items\""), 4);   // 3 movie pages + 1 series page
    // without the token: per sign-in the redirect, the public information and the sign-in itself (two sign-ins)
    CHECK_EQ(count(log, "\"authorized\": false"), 6);
    CHECK_EQ(count(log, "\"client\": false"), 0);   // every request said which client it is
    CHECK_EQ(count(log, "\"device\": false"), 0);

    // an edited source (another user): its old sign-in does not count; no request is made without one
    iptv::Profile edited = p;
    edited.username = "someoneelse";
    CHECK(!svc.signedIn(edited) && svc.streamUrl(edited, "m5").empty());
    auto refused = wait<sources::MoviesOutcome>(jobs, [&](std::function<void(sources::MoviesOutcome &)> cb) {
        svc.loadMovies(edited, dir, sources::Source::Network, cb);
    });
    CHECK(refused && !refused->ok);

    // the log never shows the token or the password
    if (const char *logs = std::getenv("PS4IPTV_TEST_LOG")) {
        diag::shutdown();
        for (const auto &f: fs::listDir(logs)) {
            std::string text;
            fs::readFile(fs::join(logs, f.name), text, 8 << 20);
            CHECK(text.find("exampletoken-jf") == std::string::npos);
            CHECK(text.find("examplepass") == std::string::npos);
        }
    }
    jobs.stop();
}
