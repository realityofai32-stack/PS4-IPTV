// Stremio add-on integration test: the app's StremioService + HTTP client against the add-on of
// scripts/download-test-server.py: an add-on that needs configuration, a manifest with a search-only catalog, a movie
// catalog in pages ("skip"), a series catalog that ignores "skip" (no endless paging), details, episodes, the saved
// catalogs and the stream chosen when a title plays (the direct address, not the torrent before it, with the
// add-on's User-Agent). Run by scripts/test-download-integration.sh (in the mediaserver_it program).

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "check.h"
#include "../../src/app/stremio_service.h"
#include "../../src/network/http.h"
#include "../../src/platform/clock.h"
#include "../../src/platform/fs.h"

namespace {

    std::string server() {
        const char *s = std::getenv("PS4IPTV_TEST_SERVER");
        return s ? s : "";
    }

    double seconds() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    template<typename Outcome, typename Start>
    std::shared_ptr<Outcome> wait(JobSystem &jobs, Start start) {
        auto out = std::make_shared<Outcome>();
        auto done = std::make_shared<bool>(false);
        start([out, done](const Outcome &o) {
            *out = o;
            *done = true;
        });
        double t0 = seconds();
        while (!*done && seconds() - t0 < 60) {
            jobs.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return *done ? out : nullptr;
    }

    int requests(const std::string &part) {
        http::Request r;
        r.url = server() + "/_log";
        std::string log = http::get(r).body;
        int n = 0;
        for (size_t at = log.find(part); at != std::string::npos; at = log.find(part, at + 1)) {
            n++;
        }
        return n;
    }
}

TEST(stremio_it_end_to_end) {
    if (server().empty()) {
        std::printf("  (skipped: PS4IPTV_TEST_SERVER not set)\n");
        return;
    }
    http::globalInit("", "PS4IPTV-test");
    JobSystem jobs;
    jobs.setWorkerExitHook(http::releaseThreadResources);
    jobs.start(4);
    StremioService svc(jobs);
    const char *base = std::getenv("PS4IPTV_TEST_TMP");
    std::string dir = fs::join(base ? base : ".", "stremio-it/");
    fs::ensureDir(dir);

    iptv::Profile p;
    p.id = "addon-it-" + std::to_string(clockx::unixNow());
    p.type = iptv::SourceType::Stremio;
    p.playlistUrl = server() + "/addoncfg";   // ".../manifest.json" added
    auto cfg = wait<sources::AuthOutcome>(jobs, [&](std::function<void(const sources::AuthOutcome &)> cb) {
        svc.authenticate(p, cb);
    });
    CHECK(cfg && cfg->result.status == iptv::AuthStatus::Refused && cfg->message.find("configured") != std::string::npos);

    p.playlistUrl = server() + "/addon/manifest.json";
    auto ok = wait<sources::AuthOutcome>(jobs, [&](std::function<void(const sources::AuthOutcome &)> cb) {
        svc.authenticate(p, cb);
    });
    CHECK(ok && ok->result.status == iptv::AuthStatus::Ok && ok->result.account.message.find("Test add-on 1.0.0") == 0);

    int before = requests("/addon/catalog/series");
    auto movies = wait<sources::MoviesOutcome>(jobs, [&](std::function<void(const sources::MoviesOutcome &)> cb) {
        svc.loadMovies(p, dir, sources::Source::Network, cb);
    });
    CHECK(movies && movies->ok && movies->catalog && movies->catalog->size() == 237);
    if (movies && movies->catalog) {
        CHECK(movies->catalog->categories().size() == 1 && movies->catalog->categories()[0].name == "Top");
        const iptv::Movie *m = movies->catalog->find("tt1000150");
        CHECK(m != nullptr && m->title == "Film 150" && m->year == 2001);
    }
    auto series = wait<sources::SeriesListOutcome>(jobs, [&](std::function<void(const sources::SeriesListOutcome &)> cb) {
        svc.loadSeriesList(p, dir, sources::Source::Network, cb);
    });
    CHECK(series && series->ok && series->catalog && series->catalog->size() == 2);
    CHECK_EQ(requests("/addon/catalog/series") - before, 2);   // the repeated page ended the paging
    CHECK_EQ(requests("/addon/catalog/movie/find"), 0);         // a search-only catalog is never listed
    auto saved = wait<sources::MoviesOutcome>(jobs, [&](std::function<void(const sources::MoviesOutcome &)> cb) {
        svc.loadMovies(p, dir, sources::Source::Cache, cb);
    });
    CHECK(saved && saved->ok && saved->catalog && saved->catalog->size() == 237 && saved->catalog->fromCache);

    auto info = wait<sources::InfoOutcome<iptv::MovieInfo>>(jobs,
            [&](std::function<void(const sources::InfoOutcome<iptv::MovieInfo> &)> cb) {
                svc.loadMovieInfo(p, "tt1000005", cb);
            });
    CHECK(info && info->ok && info->info.plot == "About." && info->info.durationSeconds == 5400);
    auto show = wait<sources::InfoOutcome<iptv::SeriesInfo>>(jobs,
            [&](std::function<void(const sources::InfoOutcome<iptv::SeriesInfo> &)> cb) {
                svc.loadSeriesInfo(p, "tt2000000", cb);
            });
    CHECK(show && show->ok && show->info.seasons.size() == 1 && show->info.seasons[0].episodes.size() == 2
          && show->info.seasons[0].episodes[0].id == "tt2000000:1:1");

    auto link = wait<sources::LinkOutcome>(jobs, [&](std::function<void(const sources::LinkOutcome &)> cb) {
        svc.resolve(p, true, "tt2000000:1:1", cb);
    });
    CHECK(link && link->ok && link->userAgent == "ExampleAddonAgent/1.0");
    CHECK(link && link->url.find("/plain/movie/") != std::string::npos
          && link->url.find("for=tt2000000%3A1%3A1") != std::string::npos);   // the id reached the add-on decoded

    // another add-on address on the same source: the old manifest does not count
    iptv::Profile moved = p;
    moved.playlistUrl = server() + "/elsewhere/manifest.json";
    auto none = wait<sources::LinkOutcome>(jobs, [&](std::function<void(const sources::LinkOutcome &)> cb) {
        svc.resolve(moved, false, "tt1000005", cb);
    });
    CHECK(none && !none->ok);
    jobs.stop();
}
