// Stalker / MAG integration test: the app's StalkerService (+ SourceService, VodLibrary, the next-episode queue and
// the app's libcurl HTTP client) against the mock portals of tests/stalker/mock_portal.py. Synthetic data only.
// Run by tests/stalker/run-stalker-it.sh (PS4IPTV_STALKER_PORTS = the mock's port file, PS4IPTV_TEST_TMP).
//
// Covers: handshake / Bearer token / get_profile / account expiry, token renewal (expiry after N requests and a
// forced expiry), Live TV (get_all_channels and the paged fallback), Movies (paged, per category), Series (series
// module with seasons -> episodes, seasons listing episode numbers, old-style VOD series), create_link for channels,
// movies and episodes, portal errors (login required, blocked MAC, malformed answers, broken lists, unreachable
// portal, link limit), saved copies (reload, refresh after a day through VodLibrary), search, favorite identities,
// next episode across a season boundary, Xtream routing, and that no MAC / token / device id / play token reaches
// the log.

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>

#include "check.h"
#include "../../src/app/auto_next.h"
#include "../../src/app/source_service.h"
#include "../../src/app/vod_library.h"
#include "../../src/core/json.h"
#include "../../src/i18n/i18n.h"
#include "../../src/iptv/stalker.h"
#include "../../src/iptv/xtream.h"
#include "../../src/network/http.h"
#include "../../src/platform/clock.h"
#include "../../src/platform/fs.h"
#include "../../src/platform/log.h"
#include "../../src/platform/redact.h"
#include "../../src/storage/catalog_cache.h"

using namespace iptv;

namespace {

    json::Value &ports() {
        static json::Value v;
        static bool loaded = false;
        if (!loaded) {
            loaded = true;
            const char *f = std::getenv("PS4IPTV_STALKER_PORTS");
            std::string text;
            if (f && fs::readFile(f, text, 1 << 16)) {
                json::parse(text, v);
            }
        }
        return v;
    }

    std::string base(const char *portal) {
        return "http://127.0.0.1:" + ports()[portal].asString();
    }

    std::string dataDir() {
        const char *b = std::getenv("PS4IPTV_TEST_TMP");
        std::string d = fs::join(b ? b : ".", "stalker-it");
        fs::ensureDir(d);
        return d;
    }

    struct Env {
        JobSystem jobs;
        XtreamService xtream{jobs};
        StalkerService stalker{jobs, dataDir() + "/"};
        SourceService sources{xtream, stalker};

        Env() {
            diag::init(fs::join(dataDir(), "logs"));
            http::globalInit("", "PS4IPTV/test");
            jobs.setWorkerExitHook(http::releaseThreadResources);
            jobs.start(4);
        }
    };

    Env &env() {
        static Env e;
        return e;
    }

    // pumps the job system until `done` (callbacks run on this thread, like the app's main loop)
    bool waitFor(const std::function<bool()> &done, double seconds = 60) {
        auto start = std::chrono::steady_clock::now();
        while (!done()) {
            env().jobs.pump();
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() > seconds) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return true;
    }

    Profile portal(const char *name, const char *path, const char *id) {
        Profile p;
        p.id = id;
        p.type = SourceType::Stalker;
        p.name = name;
        p.server = base(name) + path;
        p.mac = ports()["mac"].asString();
        return p;
    }

    json::Value control(const char *portal, const char *what) {
        http::Request r;
        r.url = base(portal) + "/control/" + what;
        http::Response resp = http::get(r);
        json::Value v;
        json::parse(resp.body, v);
        return v;
    }

    int count(const json::Value &log, const char *type, const char *action) {
        int n = 0;
        for (const auto &r: log["requests"].items()) {
            n += r["type"].asString() == type && r["action"].asString() == action;
        }
        return n;
    }

    sources::AuthOutcome auth(const Profile &p) {
        sources::AuthOutcome out;
        bool done = false;
        env().sources.authenticate(p, [&](const sources::AuthOutcome &o) {
            out = o;
            done = true;
        });
        CHECK(waitFor([&] { return done; }));
        return out;
    }

    sources::LiveOutcome live(const Profile &p, bool cacheOnly = false) {
        sources::LiveOutcome out;
        bool done = false;
        env().sources.loadLive(p, dataDir() + "/", [&](sources::LiveOutcome &o) {
            out = std::move(o);
            done = true;
        }, cacheOnly);
        CHECK(waitFor([&] { return done; }));
        return out;
    }

    template<typename Outcome, typename F>
    Outcome load(F f) {
        Outcome out;
        bool done = false;
        f([&](Outcome &o) {
            out = std::move(o);
            done = true;
        });
        CHECK(waitFor([&] { return done; }, 120));
        return out;
    }

    sources::LinkOutcome link(const std::function<CancelToken(sources::LinkCallback)> &f) {
        sources::LinkOutcome out;
        bool done = false;
        f([&](const sources::LinkOutcome &o) {
            out = o;
            done = true;
        });
        CHECK(waitFor([&] { return done; }));
        return out;
    }

    bool ready() {
        if (ports()["ministra"].asString().empty()) {
            std::printf("  (skipped: PS4IPTV_STALKER_PORTS not set)\n");
            return false;
        }
        return true;
    }
}

TEST(stalker_it_sign_in_profile_and_expiry) {
    if (!ready()) {
        return;
    }
    control("ministra", "reset");
    sources::AuthOutcome o = auth(portal("ministra", "/stalker_portal/c/", "s1"));
    CHECK(o.result.status == AuthStatus::Ok);
    CHECK_EQ(o.result.account.expiresAt, stalker::parseDateTime("2027-01-31 23:59:59"));
    json::Value log = control("ministra", "requests");
    CHECK_EQ(count(log, "stb", "handshake"), 1);
    CHECK_EQ(count(log, "stb", "get_profile"), 1);
    CHECK_EQ(count(log, "account_info", "get_main_info"), 1);
    CHECK_EQ(log["violations"].size(), (size_t) 0);
    // the first candidate of a ".../stalker_portal/c/" address is the Ministra API: no detour
    CHECK(log["requests"].at(0)["path"].asString() == "/stalker_portal/server/load.php");
    // the classic layout is found through the candidate list (portal.php at the root)
    sources::AuthOutcome c = auth(portal("classic", "/c/", "s2"));
    CHECK(c.result.status == AuthStatus::Ok);
    CHECK_EQ(control("classic", "requests")["violations"].size(), (size_t) 0);
    // a portal address typed without a scheme or /c/
    Profile bare = portal("classic", "", "s2b");
    bare.server = "127.0.0.1:" + ports()["classic"].asString();
    CHECK(auth(bare).result.status == AuthStatus::Ok);
}

TEST(stalker_it_portal_errors_are_reported) {
    if (!ready()) {
        return;
    }
    sources::AuthOutcome login = auth(portal("login", "/c/", "e1"));
    CHECK(login.result.status == AuthStatus::Disabled);
    CHECK_EQ(login.message, i18n::tr("stalker.needs_login"));
    sources::AuthOutcome blocked = auth(portal("blocked", "/c/", "e2"));
    CHECK(blocked.result.status == AuthStatus::InvalidCredentials);
    CHECK_EQ(blocked.message, i18n::tr("stalker.refused"));
    CHECK_EQ(blocked.result.account.message, std::string("MAC address not activated"));
    sources::AuthOutcome malformed = auth(portal("malformed", "/c/", "e3"));
    CHECK(malformed.result.status == AuthStatus::Malformed);
    CHECK_EQ(malformed.message, i18n::tr("stalker.not_portal"));
    Profile down = portal("ministra", "/c/", "e4");
    down.server = "http://127.0.0.1:1/c/";   // nothing listens on port 1
    CHECK(auth(down).result.status == AuthStatus::Network);
    Profile bad = portal("ministra", "/c/", "e5");
    bad.server = "ftp://127.0.0.1/c/";
    CHECK(auth(bad).result.status == AuthStatus::Malformed);
    // broken catalog answers: sign-in works, lists fail cleanly (no saved copy yet: no channels, a message)
    Profile broken = portal("brokenlists", "/c/", "e6");
    CHECK(auth(broken).result.status == AuthStatus::Ok);
    sources::LiveOutcome l = live(broken);
    CHECK(!l.ok && !l.message.empty() && l.channels.empty());
    auto m = load<sources::MoviesOutcome>([&](auto cb) {
        return env().sources.loadMovies(broken, dataDir() + "/", sources::Source::Network, cb);
    });
    CHECK(!m.ok && !m.message.empty());
    auto s = load<sources::SeriesListOutcome>([&](auto cb) {
        return env().sources.loadSeriesList(broken, dataDir() + "/", sources::Source::Network, cb);
    });
    CHECK(!s.ok);
}

TEST(stalker_it_live_tv_both_styles) {
    if (!ready()) {
        return;
    }
    Profile p = portal("ministra", "/stalker_portal/c/", "s1");
    sources::LiveOutcome l = live(p);
    CHECK(l.ok && !l.fromCache);
    CHECK_EQ(l.categories.size(), (size_t) 2);
    CHECK_EQ(l.channels.size(), (size_t) 30);
    LiveCatalog cat;
    cat.assign(l.categories, l.channels);
    CHECK_EQ(cat.countInCategory("1"), 15);
    const LiveChannel *c3 = cat.find("103");
    CHECK(c3 && c3->icon == base("ministra") + "/stalker_portal/misc/logos/320/3.png" && c3->num == 3);
    CHECK(c3 && c3->cmd == "ffmpeg http://localhost/ch/103_" && !c3->isPlaylist());
    CHECK(cat.find("101") && cat.find("101")->icon == "http://img.example.com/logo/1.png");
    // search (Turkish names, folded)
    CHECK(!cat.search("haber turk").empty());
    CHECK(!cat.search("sampiyon").empty());
    // the saved copy: same identities (favorites keep working offline)
    sources::LiveOutcome saved = live(p, true);
    CHECK(saved.ok && saved.fromCache && saved.channels.size() == 30);
    bool same = true;
    for (size_t i = 0; i < saved.channels.size(); i++) {
        same &= saved.channels[i].id == l.channels[i].id && saved.channels[i].cmd == l.channels[i].cmd;
    }
    CHECK(same);
    // classic: no get_all_channels -> 3 pages of 14
    control("classic", "reset");
    sources::LiveOutcome c = live(portal("classic", "/c/", "s2"));
    CHECK(c.ok && c.channels.size() == 30 && c.categories.size() == 2);
    json::Value log = control("classic", "requests");
    CHECK_EQ(count(log, "itv", "get_all_channels"), 1);
    CHECK_EQ(count(log, "itv", "get_ordered_list"), 3);
    CHECK(c.channels[0].id == "101");   // numeric ids become strings
}

TEST(stalker_it_live_links_and_errors) {
    if (!ready()) {
        return;
    }
    Profile p = portal("ministra", "/stalker_portal/c/", "s1");
    LiveChannel ch;
    ch.id = "103";
    ch.cmd = "ffmpeg http://localhost/ch/103_";
    sources::LinkOutcome a = link([&](auto cb) { return env().stalker.resolveLive(p, ch, cb); });
    CHECK(a.ok && a.url.find("/play/live.php?") != std::string::npos && a.url.find("play_token=") != std::string::npos);
    CHECK(a.userAgent == stalker::USER_AGENT);
    sources::LinkOutcome b = link([&](auto cb) { return env().stalker.resolveLive(p, ch, cb); });
    CHECK(b.ok && b.url != a.url);   // a fresh single-use token per open
    json::Value log = control("ministra", "requests");
    bool cmdSent = false;
    for (const auto &r: log["requests"].items()) {
        cmdSent |= r["action"].asString() == "create_link" && r["params"]["cmd"].asString() == ch.cmd;
    }
    CHECK(cmdSent);
    LiveChannel limited = ch;
    limited.cmd = "ffmpeg http://localhost/ch/limit-me";
    sources::LinkOutcome l = link([&](auto cb) { return env().stalker.resolveLive(p, limited, cb); });
    CHECK(!l.ok && l.message == i18n::tr("stalker.link.limit"));
    // a command with its own complete address still works when create_link has nothing
    LiveChannel direct = ch;
    direct.cmd = "";
    sources::LinkOutcome d = link([&](auto cb) { return env().stalker.resolveLive(p, direct, cb); });
    CHECK(!d.ok && d.message == i18n::tr("stalker.link.nothing"));
}

TEST(stalker_it_token_renewal) {
    if (!ready()) {
        return;
    }
    Profile p = portal("ministra", "/stalker_portal/c/", "s1");
    control("ministra", "reset");
    // the mock expires a token after EXPIRE_AFTER requests: a full movie crawl needs several renewals
    auto m = load<sources::MoviesOutcome>([&](auto cb) {
        return env().sources.loadMovies(p, dataDir() + "/", sources::Source::Network, cb);
    });
    CHECK(m.ok && m.catalog && m.catalog->size() == 40);
    json::Value log = control("ministra", "requests");
    int pages = count(log, "vod", "get_ordered_list");
    CHECK(pages >= 3);   // 25 + 15 entries in two categories, 14 per page
    CHECK(count(log, "stb", "handshake") >= 1);
    // a forced expiry (portal restart, token revoked): the next request signs in again by itself
    control("ministra", "expire");
    control("ministra", "reset");
    LiveChannel ch;
    ch.id = "104";
    ch.cmd = "ffmpeg http://localhost/ch/104_";
    sources::LinkOutcome a = link([&](auto cb) { return env().stalker.resolveLive(p, ch, cb); });
    CHECK(a.ok);
    log = control("ministra", "requests");
    CHECK_EQ(count(log, "stb", "handshake"), 1);
    CHECK_EQ(count(log, "stb", "get_profile"), 1);
    CHECK_EQ(count(log, "itv", "create_link"), 2);   // refused once, then accepted
    CHECK_EQ(log["violations"].size(), (size_t) 0);
}

TEST(stalker_it_movies_catalog_cache_info_and_links) {
    if (!ready()) {
        return;
    }
    Profile p = portal("ministra", "/stalker_portal/c/", "s1");
    auto net = load<sources::MoviesOutcome>([&](auto cb) {
        return env().sources.loadMovies(p, dataDir() + "/", sources::Source::Network, cb);
    });
    CHECK(net.ok && net.catalog->size() == 40 && net.catalog->categories().size() == 2);
    const Movie *m7 = net.catalog->find("5007");
    CHECK(m7 && m7->title == "Film 7" && m7->year == 2007 && m7->categoryId == "10");
    CHECK(m7 && m7->icon == base("ministra") + "/stalker_portal/screenshots/5007.jpg");
    CHECK(!net.catalog->search("film 7").empty());
    auto cached = load<sources::MoviesOutcome>([&](auto cb) {
        return env().sources.loadMovies(p, dataDir() + "/", sources::Source::Cache, cb);
    });
    CHECK(cached.ok && cached.catalog->fromCache && cached.catalog->size() == 40);
    CHECK(cached.catalog->find("5007") && cached.catalog->find("5007")->title == "Film 7");
    auto info = load<sources::InfoOutcome<MovieInfo>>([&](auto cb) { return env().sources.loadMovieInfo(p, "5007", cb); });
    CHECK(info.ok && info.info.plot == "Plot of film 7." && info.info.durationSeconds == 87 * 60);
    sources::LinkOutcome l = link([&](auto cb) { return env().stalker.resolveMovie(p, "5007", cb); });
    CHECK(l.ok && l.url.find("/play/movie.php?") != std::string::npos);
    CHECK(l.url.find("stream=%2Fmedia%2Ffile_5007.mpg") != std::string::npos);   // the movie's own cmd
    sources::LinkOutcome unknown = link([&](auto cb) { return env().stalker.resolveMovie(p, "424242", cb); });
    CHECK(!unknown.ok && unknown.message == i18n::tr("stalker.link.unknown"));
}

TEST(stalker_it_series_module_seasons_episodes_and_next_episode) {
    if (!ready()) {
        return;
    }
    Profile p = portal("ministra", "/stalker_portal/c/", "s1");
    auto list = load<sources::SeriesListOutcome>([&](auto cb) {
        return env().sources.loadSeriesList(p, dataDir() + "/", sources::Source::Network, cb);
    });
    CHECK(list.ok && list.catalog->size() == 2);
    const Series *s900 = list.catalog->find("900");
    CHECK(s900 && s900->title == "Mini Series" && s900->year == 2021 && s900->plot == "Two seasons.");
    auto info = load<sources::InfoOutcome<SeriesInfo>>([&](auto cb) { return env().sources.loadSeriesInfo(p, "900", cb); });
    CHECK(info.ok && info.info.seasons.size() == 2);
    if (!info.ok || info.info.seasons.size() != 2) {
        return;
    }
    const SeriesInfo &si = info.info;
    CHECK(si.seasons[0].number == 1 && si.seasons[0].episodes.size() == 3);   // sorted by season
    CHECK(si.seasons[1].number == 2 && si.seasons[1].episodes.size() == 2);
    const Episode &e13 = si.seasons[0].episodes[2];
    CHECK(e13.number == 3 && e13.cmd == "/media/episode_900_1_3.mpg" && e13.linkSeries == 3 && e13.title == "Episode 3");
    CHECK(si.series.plot == "Two seasons.");
    // next episode across the season boundary (the player's queue, as built from Series Detail)
    screens::VodItem cur;
    cur.type = ContentType::Series;
    cur.id = e13.id;
    cur.seriesId = "900";
    cur.season = 1;
    cur.episode = 3;
    int position = -1;
    auto queue = autonext::episodeQueue(cur, &si, {}, false, position);
    CHECK_EQ(queue.size(), (size_t) 5);
    CHECK_EQ(position, 2);
    int next = autonext::nextIndex(queue, position, &si, false);
    CHECK(next == 3 && queue[3].season == 2 && queue[3].episode == 1);
    CHECK(queue[3].cmd == "/media/episode_900_2_1.mpg" && queue[3].linkSeries == 1);
    CHECK(queue[2].cmd == e13.cmd);   // the current item gets its command from the list when it had none
    // play the next episode: create_link with the episode command and its number
    control("ministra", "reset");
    sources::LinkOutcome l = link([&](auto cb) {
        return env().stalker.resolveEpisode(p, queue[3].cmd, queue[3].linkSeries, cb);
    });
    CHECK(l.ok && l.url.find("series=1") != std::string::npos);
    json::Value log = control("ministra", "requests");
    bool ok = false;
    for (const auto &r: log["requests"].items()) {
        ok |= r["action"].asString() == "create_link" && r["params"]["type"].asString().empty()
              && r["params"]["series"].asString() == "1" && r["params"]["cmd"].asString() == queue[3].cmd;
    }
    CHECK(ok);
    // seasons that list their episode numbers (one cmd per season)
    auto alt = load<sources::InfoOutcome<SeriesInfo>>([&](auto cb) { return env().sources.loadSeriesInfo(p, "901", cb); });
    CHECK(alt.ok && alt.info.seasons.size() == 2 && alt.info.seasons[1].episodes.size() == 3);
    if (alt.ok && alt.info.seasons.size() == 2) {
        const Episode &e = alt.info.seasons[1].episodes[2];
        CHECK(e.cmd == "season-cmd-901-2" && e.linkSeries == 3 && e.season == 2);
    }
}

TEST(stalker_it_old_style_vod_series) {
    if (!ready()) {
        return;
    }
    Profile p = portal("classic", "/c/", "s2");
    // Movies first: the is_series entries are not movies, they are kept for Series
    auto movies = load<sources::MoviesOutcome>([&](auto cb) {
        return env().sources.loadMovies(p, dataDir() + "/", sources::Source::Network, cb);
    });
    CHECK(movies.ok && movies.catalog->size() == 40 && movies.catalog->find("7001") == nullptr);
    control("classic", "reset");
    auto list = load<sources::SeriesListOutcome>([&](auto cb) {
        return env().sources.loadSeriesList(p, dataDir() + "/", sources::Source::Network, cb);
    });
    CHECK(list.ok && list.catalog->size() == 3 && list.catalog->find("v:7001") != nullptr);
    json::Value log = control("classic", "requests");
    CHECK_EQ(count(log, "series", "get_categories"), 1);    // tried once, 404
    CHECK_EQ(count(log, "vod", "get_ordered_list"), 0);     // the saved VOD series were used, no second crawl
    auto info = load<sources::InfoOutcome<SeriesInfo>>([&](auto cb) { return env().sources.loadSeriesInfo(p, "v:7002", cb); });
    CHECK(info.ok && info.info.seasons.size() == 1 && info.info.seasons[0].episodes.size() == 5);
    if (info.ok && !info.info.seasons.empty()) {
        const Episode &e = info.info.seasons[0].episodes[4];
        CHECK(e.linkSeries == 5 && e.cmd == "eyJ0eXBlIjoic2VyaWVzIiwiaWQiOjcwMDB9" "2");
        sources::LinkOutcome l = link([&](auto cb) { return env().stalker.resolveEpisode(p, e.cmd, e.linkSeries, cb); });
        CHECK(l.ok && l.url.find("series=5") != std::string::npos);
    }
}

TEST(stalker_it_saved_copy_refresh_through_vod_library) {
    if (!ready()) {
        return;
    }
    Profile p = portal("ministra", "/stalker_portal/c/", "s1");
    CatalogCache cache(dataDir() + "/");
    auto age = [&](int64_t seconds) {
        for (const char *n: {"stalker_vod", "stalker_vod_categories"}) {
            std::string body;
            CatalogCache::Meta meta;
            CHECK(cache.load("s1", n, body, meta));
            CHECK(cache.save("s1", n, body, clockx::unixNow() - seconds, meta.items));
        }
    };
    // a fresh saved copy: shown, no request to the portal
    age(3600);
    control("ministra", "reset");
    VodLibrary fresh(env().sources, dataDir() + "/");
    fresh.openMovies(p);
    CHECK(waitFor([&] { return fresh.movieStatus().status == CatalogStatus::Ready; }));
    CHECK(fresh.movieStatus().fromCache && fresh.movies().size() == 40);
    for (int i = 0; i < 50; i++) {
        env().jobs.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK_EQ(count(control("ministra", "requests"), "vod", "get_ordered_list"), 0);
    // older than a day: shown at once, refreshed from the portal in the background
    age(25 * 3600);
    control("ministra", "reset");
    VodLibrary stale(env().sources, dataDir() + "/");
    stale.openMovies(p);
    CHECK(waitFor([&] { return stale.movieStatus().status == CatalogStatus::Ready; }));
    CHECK(waitFor([&] { return stale.movieStatus().refreshes > 0; }, 120));
    CHECK(stale.movieStatus().lastRefreshOk && !stale.movieStatus().fromCache && stale.movies().size() == 40);
    CHECK(count(control("ministra", "requests"), "vod", "get_ordered_list") >= 3);
    // the progress counter is back to 0 when no download runs
    CHECK_EQ(env().sources.progress(p, true), 0);
}

TEST(stalker_it_xtream_profiles_never_reach_the_portal_code) {
    if (!ready()) {
        return;
    }
    Profile x;
    x.id = "x1";
    x.type = SourceType::Xtream;
    x.server = base("xtream");
    x.username = "exampleuser";
    x.password = "examplepass";
    control("xtream", "reset");
    sources::AuthOutcome o = auth(x);
    CHECK(o.result.status == AuthStatus::Ok && o.result.account.maxConnections == 1);
    sources::LiveOutcome l = live(x);
    CHECK(l.ok && l.channels.size() == 1 && l.channels[0].id == "1");
    json::Value log = control("xtream", "requests");
    for (const auto &r: log["requests"].items()) {
        CHECK(r["path"].asString() == "/player_api.php");
        CHECK(!r["auth"].asBool());   // no Bearer header: Xtream requests are unchanged
    }
}

// regression: Xtream Movies / Series / series info / next episode through the facade and VodLibrary, Xtream URLs
TEST(stalker_it_xtream_catalogs_and_next_episode_unchanged) {
    if (!ready()) {
        return;
    }
    Profile x;
    x.id = "x1";
    x.type = SourceType::Xtream;
    x.server = base("xtream");
    x.username = "exampleuser";
    x.password = "examplepass";
    VodLibrary lib(env().sources, dataDir() + "/");
    lib.openMovies(x);
    lib.openSeries(x);
    CHECK(waitFor([&] { return lib.movieStatus().status == CatalogStatus::Ready
                               && lib.seriesStatus().status == CatalogStatus::Ready; }));
    CHECK(lib.movies().size() == 5 && lib.movies().find("303") && lib.movies().find("303")->title == "Xtream Film 3");
    CHECK(lib.series().size() == 1 && lib.series().find("70"));
    lib.requestSeriesInfo(x, "70");
    CHECK(waitFor([&] { return lib.seriesInfo("70") != nullptr; }));
    auto info = lib.seriesInfo("70");
    CHECK(info && info->seasons.size() == 2);
    if (info && info->seasons.size() == 2) {
        screens::VodItem cur;
        cur.type = ContentType::Series;
        cur.id = "702";
        cur.seriesId = "70";
        int position = -1;
        auto queue = autonext::episodeQueue(cur, info.get(), {}, false, position);
        int next = autonext::nextIndex(queue, position, info.get(), false);
        CHECK(position == 1 && next == 2 && queue[2].id == "711" && queue[2].season == 2 && queue[2].cmd.empty());
        CHECK_EQ(xtream::seriesUrl(x, queue[2].id, queue[2].extension), base("xtream") + "/series/exampleuser/examplepass/711.mp4");
    }
    CHECK_EQ(xtream::movieUrl(x, "303", "mkv"), base("xtream") + "/movie/exampleuser/examplepass/303.mkv");
    // a Stalker profile with the same id elsewhere never mixes in (Xtream requests only)
    json::Value log = control("xtream", "requests");
    for (const auto &r: log["requests"].items()) {
        CHECK(r["path"].asString() == "/player_api.php");
    }
}

// last: nothing identifying reached the log file
TEST(stalker_it_log_never_contains_identity_or_tokens) {
    if (!ready()) {
        return;
    }
    std::string text;
    CHECK(fs::readFile(fs::join(fs::join(dataDir(), "logs"), "log.txt"), text, 64u << 20));
    CHECK(text.find(" stalker ") != std::string::npos);   // the client did log (the check is meaningful)
    std::string mac = ports()["mac"].asString();
    stalker::Identity id = stalker::identity(mac);
    std::string bare;
    for (char c: mac) {
        if (c != ':') {
            bare += c;
        }
    }
    std::vector<std::string> secrets = {mac, bare, "00%3A1A%3A79", id.serial, id.deviceId, id.deviceId2, id.signature};
    for (const char *portal: {"ministra", "classic", "login", "blocked", "brokenlists"}) {
        json::Value issued = control(portal, "tokens");
        for (const auto &t: issued.items()) {
            secrets.push_back(t.asString());
        }
    }
    std::printf("     %zu identity values and tokens checked against %zu bytes of log\n", secrets.size(), text.size());
    CHECK(secrets.size() > 15);
    int leaks = 0;
    for (const auto &s: secrets) {
        if (!s.empty() && text.find(s) != std::string::npos) {
            std::printf("  log contains a secret (%zu chars)\n", s.size());
            leaks++;
        }
    }
    CHECK_EQ(leaks, 0);
    std::string lower;
    for (char c: text) {
        lower += (char) std::tolower((unsigned char) c);
    }
    CHECK(lower.find("00:1a:79:5e") == std::string::npos);
    CHECK(text.find("play_token=") == std::string::npos || text.find("play_token=[REDACTED]") != std::string::npos);
    CHECK(text.find("Bearer") == std::string::npos);
}
