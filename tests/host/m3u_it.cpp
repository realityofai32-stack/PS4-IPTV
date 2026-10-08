// M3U playlist integration test: the app's M3uService + libcurl HTTP client (libcurl 7.80.0 as on the PS4)
// against scripts/download-test-server.py on 127.0.0.1 - a 25,000 entry download timed separately from parsing,
// redirects, Content-Encoding (when the client's libcurl accepts it), a custom User-Agent, HTTP 404, an HTML
// answer, cancellation, the saved copy (written only for a valid playlist, never replaced by a failure) and
// loading it back. Run by scripts/run-download-integration-test.ps1 (PS4IPTV_TEST_SERVER).

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

#include "check.h"
#include "../../src/app/m3u_service.h"
#include "../../src/network/http.h"
#include "../../src/platform/fs.h"
#include "../../src/storage/catalog_cache.h"

namespace {

    std::string server() {
        const char *s = std::getenv("PS4IPTV_TEST_SERVER");
        return s ? s : "";
    }

    std::string dataDir() {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string d = fs::join(base ? base : ".", "m3u-it");
        fs::ensureDir(d);
        return d;
    }

    double now() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    struct Run {
        bool done = false;
        M3uService::Outcome outcome;
        double seconds = 0;
    };

    // runs one load to completion, pumping the job system like the app's frame loop
    Run load(JobSystem &jobs, const iptv::Profile &p, M3uService::Source source, bool save,
             double cancelAfter = -1) {
        M3uService service(jobs);
        Run run;
        double t0 = now();
        CancelToken token = service.load(p, dataDir(), source, save, [&run](M3uService::Outcome &o) {
            run.outcome = o;
            run.done = true;
        });
        bool canceled = false;
        while (!run.done && now() - t0 < 60) {
            jobs.pump();
            if (cancelAfter >= 0 && !canceled && now() - t0 > cancelAfter) {
                token.cancel();
                canceled = true;
                jobs.waitIdle();
                jobs.pump();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        run.seconds = now() - t0;
        return run;
    }

    iptv::Profile playlist(const std::string &id, const std::string &path) {
        iptv::Profile p;
        p.id = id;
        p.type = iptv::SourceType::M3u;
        p.playlistUrl = server() + path;
        return p;
    }
}

TEST(m3u_it_download_parse_and_save) {
    if (server().empty()) {
        std::printf("  (skipped: PS4IPTV_TEST_SERVER not set)\n");
        return;
    }
    http::globalInit("", "PS4IPTV-test/1.0");
    JobSystem jobs;
    jobs.start(2);
    CatalogCache cache(dataDir());
    cache.remove("it1", M3uService::CACHE_NAME);

    // 25,000 entries: download (fetch) timed separately from parse + catalog + search index
    http::Request req;
    req.url = server() + "/m3u/plain/25000.m3u";
    double f0 = now();
    http::Response raw = http::get(req);
    double fetchMs = (now() - f0) * 1000;
    CHECK(raw.ok() && raw.body.size() > 2000000);

    Run big = load(jobs, playlist("it1", "/m3u/plain/25000.m3u"), M3uService::Source::Network, true);
    CHECK(big.done && big.outcome.ok);
    CHECK(big.outcome.catalog && big.outcome.catalog->channels().size() == 25000);
    CHECK_EQ(big.outcome.info.stats.groups, 40);
    std::printf("     M3U IT 25000 entries (%.1f MB): fetch alone %.0f ms | fetch + parse + index + save %.0f ms "
                "(parse %.0f ms, index %.0f ms)\n", raw.body.size() / 1048576.0, fetchMs, big.seconds * 1000,
                big.outcome.info.parseMs, big.outcome.info.indexMs);

    // the saved copy loads without the network and gives the same channels
    Run cached = load(jobs, playlist("it1", "/m3u/missing/x.m3u"), M3uService::Source::Cache, false);
    CHECK(cached.done && cached.outcome.ok && cached.outcome.info.fromCache);
    CHECK(cached.outcome.catalog && cached.outcome.catalog->channels().size() == 25000);
    CHECK(cached.outcome.catalog->channels()[123].id == big.outcome.catalog->channels()[123].id);
    std::printf("     M3U IT saved copy: load + parse + index %.0f ms\n", cached.seconds * 1000);

    // failures never replace the saved copy
    for (const char *path: {"/m3u/missing/1.m3u", "/m3u/html/1.m3u"}) {
        Run bad = load(jobs, playlist("it1", path), M3uService::Source::Network, true);
        CHECK(bad.done && !bad.outcome.ok && !bad.outcome.message.empty());
    }
    Run bad404 = load(jobs, playlist("it1", "/m3u/missing/1.m3u"), M3uService::Source::Network, true);
    CHECK(bad404.outcome.networkFailure);   // HTTP 404: could not download
    Run html = load(jobs, playlist("it1", "/m3u/html/1.m3u"), M3uService::Source::Network, true);
    CHECK(!html.outcome.networkFailure);    // downloaded something that is not a playlist
    Run still = load(jobs, playlist("it1", "/m3u/missing/x.m3u"), M3uService::Source::Cache, false);
    CHECK(still.outcome.ok && still.outcome.catalog->channels().size() == 25000);

    // redirect, Content-Encoding (gzip when this libcurl accepts it)
    Run redirected = load(jobs, playlist("it2", "/m3u/redirect/500.m3u"), M3uService::Source::Network, false);
    CHECK(redirected.outcome.ok && redirected.outcome.catalog->channels().size() == 500);
    Run zipped = load(jobs, playlist("it2", "/m3u/gzip/500.m3u"), M3uService::Source::Network, false);
    CHECK(zipped.outcome.ok && zipped.outcome.catalog->channels().size() == 500);

    // custom User-Agent: the server refuses any other
    iptv::Profile ua = playlist("it3", "/m3u/ua/50.m3u");
    Run refused = load(jobs, ua, M3uService::Source::Network, false);
    CHECK(!refused.outcome.ok);
    ua.userAgent = "ExamplePlaylistAgent/1.0";
    Run accepted = load(jobs, ua, M3uService::Source::Network, false);
    CHECK(accepted.outcome.ok && accepted.outcome.catalog->channels().size() == 50);

    // cancellation of a slow download: no callback, nothing saved
    cache.remove("it4", M3uService::CACHE_NAME);
    Run canceled = load(jobs, playlist("it4", "/m3u/slow/20000.m3u"), M3uService::Source::Network, true, 0.3);
    CHECK(!canceled.done);
    CHECK(canceled.seconds < 5);
    std::string body;
    CatalogCache::Meta meta;
    CHECK(!cache.load("it4", M3uService::CACHE_NAME, body, meta));

    jobs.stop();
    http::globalShutdown();
}
