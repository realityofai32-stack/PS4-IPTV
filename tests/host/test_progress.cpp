// Exact resume, Continue Watching, Recently Watched, persistence, Series main button and HUD timing.

#include <cstdlib>

#include "check.h"
#include "../../src/app/series_plan.h"
#include "../../src/app/vod_progress.h"
#include "../../src/platform/fs.h"
#include "../../src/player/hud_logic.h"
#include "../../src/storage/library_store.h"

using namespace iptv;
using screens::VodItem;

namespace {
    std::string tempDir(const char *name) {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string d = fs::join(base ? base : ".", name);
        fs::ensureDir(d);
        for (const auto &e: fs::listDir(d)) {
            if (!e.dir) {
                fs::removeFile(fs::join(d, e.name));
            }
        }
        return d;
    }

    VodItem movieItem(const std::string &id, const std::string &title = "Movie", double duration = 6000) {
        VodItem it;
        it.type = ContentType::Movie;
        it.id = id;
        it.title = title;
        it.extension = "mkv";
        it.durationHint = duration;
        return it;
    }

    VodItem episodeItem(const std::string &id, const std::string &seriesId, int season, int episode,
                        double duration = 3420) {
        VodItem it;
        it.type = ContentType::Series;
        it.id = id;
        it.title = "Episode title " + id;
        it.seriesId = seriesId;
        it.seriesName = "Series " + seriesId;
        it.season = season;
        it.episode = episode;
        it.extension = "mkv";
        it.durationHint = duration;
        return it;
    }

    // plays `item` from where it would resume up to `stopAt` (samples every 0.25 s like the player), then stops
    // with the player's position queried at `stopAt`
    double playAndStop(LibraryStore &lib, const VodItem &item, double stopAt, bool resume = true,
                       double clock = 1000) {
        VodProgress p(lib);
        double start = p.begin(item, resume, clock);
        double t = clock;
        for (double pos = start; pos < stopAt; pos += 0.25) {
            t += 0.25;
            p.observe(pos, item.durationHint, true, t);
            if (p.due(t) != VodProgress::Due::None) {
                p.record(t);
            }
        }
        p.record(t + 0.1, stopAt);   // Circle: the position mpv reports right now
        return start;
    }

    seriesplan::Action planOf(const SeriesInfo &info, const LibraryStore &lib) {
        return seriesplan::defaultAction(info, info.seriesId, lib);
    }

    SeriesInfo showWithEpisodes() {
        SeriesInfo info;
        info.seriesId = "s1";
        for (int s = 0; s <= 2; s++) {
            Season season;
            season.number = s;   // season 0 = specials
            for (int e = 1; e <= (s == 0 ? 1 : 4); e++) {
                Episode ep;
                ep.id = "e" + std::to_string(s) + std::to_string(e);
                ep.season = s;
                ep.number = e;
                ep.durationSeconds = 3420;
                season.episodes.push_back(ep);
            }
            info.seasons.push_back(season);
        }
        return info;
    }
}

TEST(resume_exact_position_movie_and_episode) {
    LibraryStore lib("unused");
    lib.setProfile("p1");
    // Movie: play to 17:42, stop -> stored 17:42 -> resume target 17:42 (no -5 s)
    VodItem m = movieItem("m1");
    CHECK(playAndStop(lib, m, 17 * 60 + 42) == 0);
    const HistoryEntry *e = lib.progressOf(ContentType::Movie, "m1");
    CHECK(e && e->position == 1062.0 && e->duration == 6000 && !e->watched);
    VodProgress again(lib);
    CHECK(again.begin(m, true, 0) == 1062.0);
    // sub-second precision survives (mpv reports double seconds)
    playAndStop(lib, m, 1062.533);
    CHECK(VodProgress(lib).begin(m, true, 0) == 1062.533);

    // Episode: 24:16
    VodItem ep = episodeItem("e13", "s1", 1, 3);
    playAndStop(lib, ep, 24 * 60 + 16);
    const HistoryEntry *pe = lib.progressOf(ContentType::Series, "e13");
    CHECK(pe && pe->position == 1456.0 && pe->seriesId == "s1" && pe->season == 1 && pe->episode == 3);
    CHECK(VodProgress(lib).begin(ep, true, 0) == 1456.0);

    // resume not wanted (Play / Start over) or disabled: from the start
    CHECK(VodProgress(lib).begin(ep, false, 0) == 0);
    // resuming starts exactly at the saved position and stopping again keeps the new exact position
    CHECK(playAndStop(lib, ep, 1500.25) == 1456.0);
    CHECK(lib.progressOf(ContentType::Series, "e13")->position == 1500.25);
}

TEST(resume_final_position_beats_periodic_sample) {
    LibraryStore lib("unused");
    lib.setProfile("p1");
    VodItem m = movieItem("m2");
    VodProgress p(lib);
    p.begin(m, true, 0);
    // periodic update at 12:30
    p.observe(750, 6000, true, 10);
    CHECK(p.due(10) == VodProgress::Due::Memory);   // 5 s store updates, 15 s disk writes
    CHECK(p.due(15) == VodProgress::Due::Disk);
    CHECK(p.record(10));
    CHECK(lib.progressOf(ContentType::Movie, "m2")->position == 750);
    // playback continues to 12:38; no periodic update in between (less than 5 s since the last one)
    p.observe(753, 6000, true, 13);
    CHECK(p.due(13) == VodProgress::Due::None);
    // Circle at 12:38: the position queried from mpv is stored, not the last periodic sample
    CHECK(p.record(18, 758.0));
    CHECK(lib.progressOf(ContentType::Movie, "m2")->position == 758.0);
    CHECK(VodProgress(lib).begin(m, true, 0) == 758.0);
}

TEST(resume_seek_pause_and_never_started) {
    LibraryStore lib("unused");
    lib.setProfile("p1");
    VodItem m = movieItem("m3");
    VodProgress p(lib);
    p.begin(m, true, 0);
    p.observe(600, 6000, true, 1);
    // seek presses accumulating: stopping now keeps the requested target
    p.seekPending(1200);
    CHECK(p.position() == 1200);
    CHECK(p.record(2, 600.5));
    CHECK(lib.progressOf(ContentType::Movie, "m3")->position == 1200);
    // seek sent; mpv still reports the old position for a moment: ignored until it arrives
    p.seekCommitted(1800, 3);
    p.observe(601, 6000, true, 3.2);
    CHECK(p.position() == 1800);
    p.observe(1800.4, 6000, true, 3.6);
    CHECK(p.position() == 1800.4);
    // pause: the exact paused position, and resuming later continues from it
    CHECK(p.record(10, 1804.75));
    CHECK(lib.progressOf(ContentType::Movie, "m3")->position == 1804.75);

    // never got past the first second (opened and closed): existing progress is left alone
    VodProgress q(lib);
    double start = q.begin(m, false, 20);   // Play from the beginning
    CHECK(start == 0);
    CHECK(!q.record(21, -1));
    CHECK(lib.progressOf(ContentType::Movie, "m3")->position == 1804.75);
    // a resumed item closed before its first frame keeps exactly its saved position
    VodProgress r(lib);
    CHECK(r.begin(m, true, 30) == 1804.75);
    CHECK(r.record(31, -1));
    CHECK(lib.progressOf(ContentType::Movie, "m3")->position == 1804.75);
    // a stale 0 right after a resumed start is not taken as the position
    r.observe(0, 6000, true, 32);
    CHECK(r.position() == 1804.75);
}

TEST(continue_watching_rules) {
    using progress::inProgress;
    CHECK(!inProgress(4.9, 6000, false));     // not genuinely started
    CHECK(inProgress(5, 6000, false));        // 5 s is enough
    CHECK(inProgress(600, 6000, false));
    CHECK(!inProgress(5580, 6000, false));    // 93 %: watched
    CHECK(inProgress(5579, 6000, false));
    CHECK(!inProgress(600, 6000, true));
    CHECK(inProgress(30, 0, false));          // unknown duration: still resumable
    CHECK(progress::isWatched(5580, 6000) && !progress::isWatched(100, 0));

    LibraryStore lib("unused");
    lib.setProfile("p1");
    // 5-second partial movie -> Continue Watching
    playAndStop(lib, movieItem("m5s"), 5.0);
    std::vector<const HistoryEntry *> cw = lib.continueWatching(10);
    CHECK(cw.size() == 1 && cw[0]->id == "m5s");
    // 3 seconds: recently watched, not Continue Watching
    playAndStop(lib, movieItem("m3s"), 3.0);
    CHECK(lib.continueWatching(10).size() == 1);
    CHECK(lib.recentlyWatched(10)[0]->id == "m3s");
    // partial episode -> Continue Watching, newest first
    playAndStop(lib, episodeItem("e11", "s1", 1, 1), 300);
    cw = lib.continueWatching(10);
    CHECK(cw.size() == 2 && cw[0]->id == "e11" && cw[1]->id == "m5s");
    // 93 % -> not Continue Watching, still Recently Watched
    playAndStop(lib, movieItem("mDone", "Done", 6000), 5600);
    CHECK(lib.progressOf(ContentType::Movie, "mDone")->watched);
    for (const HistoryEntry *e: lib.continueWatching(10)) {
        CHECK(e->id != "mDone");
    }
    CHECK(lib.recentlyWatched(10)[0]->id == "mDone");
    // completed to the end (player end of file)
    VodProgress endp(lib);
    endp.begin(movieItem("mEnd"), true, 0);
    endp.observe(5990, 6000, true, 1);
    CHECK(endp.record(2, -1, true));
    CHECK(lib.progressOf(ContentType::Movie, "mEnd")->watched && lib.progressOf(ContentType::Movie, "mEnd")->position == 6000);
    // Live TV never appears in Continue Watching
    HistoryEntry live;
    live.type = ContentType::Live;
    live.id = "ch1";
    live.position = 999;
    lib.addHistory(live);
    lib.updateProgress(live);   // even when handed to updateProgress
    for (const HistoryEntry *e: lib.continueWatching(10)) {
        CHECK(e->type != ContentType::Live);
    }
    CHECK(lib.progressOf(ContentType::Live, "ch1") == nullptr);
    CHECK(lib.recentlyWatched(10)[0]->type == ContentType::Live && lib.recentlyWatched(10)[0]->position == 0);
}

TEST(continue_watching_series_and_identity) {
    LibraryStore lib("unused");
    lib.setProfile("p1");
    // episode 1 in progress, then episode 2 finished: episode 1 stays (a finished episode no longer hides it)
    playAndStop(lib, episodeItem("e11", "s1", 1, 1), 600);
    playAndStop(lib, episodeItem("e12", "s1", 1, 2), 3400);
    std::vector<const HistoryEntry *> cw = lib.continueWatching(10);
    CHECK(cw.size() == 1 && cw[0]->id == "e11");
    // a barely opened episode does not hide the series' episode in progress either
    playAndStop(lib, episodeItem("e13", "s1", 1, 3), 2);
    cw = lib.continueWatching(10);
    CHECK(cw.size() == 1 && cw[0]->id == "e11");
    // two episodes in progress: one card, the most recently played
    playAndStop(lib, episodeItem("e14", "s1", 1, 4), 120);
    cw = lib.continueWatching(10);
    CHECK(cw.size() == 1 && cw[0]->id == "e14");
    // order: last playback activity first
    playAndStop(lib, movieItem("mA", "A"), 100);
    playAndStop(lib, movieItem("mB", "B"), 100);
    playAndStop(lib, movieItem("mA", "A"), 200);
    cw = lib.continueWatching(10);
    CHECK(cw.size() == 3 && cw[0]->id == "mA" && cw[1]->id == "mB" && cw[2]->id == "e14");
    CHECK(cw[0]->position == 200);

    // duplicate titles with different ids: independent progress
    playAndStop(lib, movieItem("dup1", "Same Title"), 300);
    playAndStop(lib, movieItem("dup2", "Same Title"), 900);
    CHECK(lib.progressOf(ContentType::Movie, "dup1")->position == 300);
    CHECK(lib.progressOf(ContentType::Movie, "dup2")->position == 900);
    // a movie and an episode with the same id never collide
    playAndStop(lib, movieItem("777"), 50);
    playAndStop(lib, episodeItem("777", "s9", 1, 1), 70);
    CHECK(lib.progressOf(ContentType::Movie, "777")->position == 50);
    CHECK(lib.progressOf(ContentType::Series, "777")->position == 70);
    // episodes of different series with the same title/number never collide (identity = episode id)
    playAndStop(lib, episodeItem("x1", "sA", 1, 1), 40);
    playAndStop(lib, episodeItem("x2", "sB", 1, 1), 80);
    CHECK(lib.progressOf(ContentType::Series, "x1")->position == 40);
    CHECK(lib.progressOf(ContentType::Series, "x2")->position == 80);

    // profiles are independent
    lib.setProfile("p2");
    CHECK(lib.continueWatching(10).empty() && lib.progressOf(ContentType::Movie, "mA") == nullptr);
    playAndStop(lib, movieItem("mA", "A"), 999);
    lib.setProfile("p1");
    CHECK(lib.progressOf(ContentType::Movie, "mA")->position == 200);
}

TEST(continue_watching_home_updates_and_survives_restart) {
    std::string dir = tempDir("progress_restart");
    LibraryStore lib(dir);
    lib.load();
    lib.setProfile("p1");
    unsigned g0 = lib.generation();
    playAndStop(lib, movieItem("m1", "One"), 1062.533);
    // the Home screen rebuilds when the generation moves: immediately after returning from playback
    CHECK(lib.generation() != g0);
    CHECK(lib.continueWatching(10).size() == 1);
    playAndStop(lib, episodeItem("e1", "s1", 1, 3), 1456);
    CHECK(lib.save());

    // full app restart
    LibraryStore restarted(dir);
    restarted.load();
    restarted.setProfile("p1");
    std::vector<const HistoryEntry *> cw = restarted.continueWatching(10);
    CHECK(cw.size() == 2 && cw[0]->id == "e1" && cw[1]->id == "m1");
    CHECK(restarted.progressOf(ContentType::Movie, "m1")->position == 1062.533);
    CHECK(VodProgress(restarted).begin(movieItem("m1"), true, 0) == 1062.533);
    const HistoryEntry *e = restarted.progressOf(ContentType::Series, "e1");
    CHECK(e && e->seriesName == "Series s1" && e->season == 1 && e->episode == 3 && e->name == "Episode title e1");
    // newer activity after the restart still sorts first
    playAndStop(restarted, movieItem("m1", "One"), 1100);
    CHECK(restarted.continueWatching(10)[0]->id == "m1");
}

TEST(continue_watching_not_evicted_by_live_zapping) {
    LibraryStore lib("unused");
    lib.setProfile("p1");
    playAndStop(lib, movieItem("m1"), 600);
    playAndStop(lib, episodeItem("e1", "s1", 1, 1), 600);
    for (int i = 0; i < 450; i++) {   // zapping through every channel, twice
        HistoryEntry live;
        live.type = ContentType::Live;
        live.id = "ch" + std::to_string(i % 225);
        lib.addHistory(live);
    }
    CHECK(lib.continueWatching(10).size() == 2);
    CHECK(lib.progressOf(ContentType::Movie, "m1") != nullptr);
    size_t liveCount = 0;
    bool movieInHistory = false;
    for (const auto &h: lib.history()) {
        liveCount += h.type == ContentType::Live;
        movieInHistory = movieInHistory || h.id == "m1";
    }
    CHECK(liveCount == LibraryStore::LIVE_HISTORY_LIMIT && movieInHistory);
    // Recently Watched: at most 3 channels while movies/episodes can fill the row
    std::vector<const HistoryEntry *> recent = lib.recentlyWatched(5, 3);
    int liveShown = 0;
    for (const HistoryEntry *h: recent) {
        liveShown += h->type == ContentType::Live;
    }
    CHECK(recent.size() == 5 && liveShown == 3);
    // only channels watched: the row is filled with them
    LibraryStore onlyLive("unused");
    onlyLive.setProfile("p");
    for (int i = 0; i < 8; i++) {
        HistoryEntry live;
        live.type = ContentType::Live;
        live.id = "c" + std::to_string(i);
        onlyLive.addHistory(live);
    }
    CHECK(onlyLive.recentlyWatched(5, 3).size() == 5);
}

TEST(history_schema_1_migrates_progress) {
    // a Checkpoint 2 history.json: one list with positions inside
    const char *v1 = R"({"version":1,"profiles":{"p1":[
        {"type":"live","id":"5","name":"TRT 1","icon":"","extra":"","watchedAt":30,"position":0,"duration":0},
        {"type":"series","id":"e2","name":"Ep","icon":"c.jpg","watchedAt":20,"position":2300,"duration":2400,
         "watched":true,"ext":"mkv","seriesId":"s1","seriesName":"Show","season":1,"episode":2},
        {"type":"movie","id":"m1","name":"Movie","icon":"p.jpg","watchedAt":10,"position":20,"duration":6000,
         "watched":false,"ext":"mkv"},
        {"type":"series","id":"e1","name":"Ep 1","watchedAt":5,"position":300,"duration":2400,"watched":false,
         "ext":"mkv","seriesId":"s1","seriesName":"Show","season":1,"episode":1}]}})";
    LibraryStore lib("unused");
    std::string err;
    CHECK(lib.deserializeHistory(v1, &err));
    lib.setProfile("p1");
    CHECK(lib.history().size() == 4 && lib.history()[0].id == "5");
    // 20 s movie (hidden by the old 30 s rule) and the episode behind a finished one are in progress now
    std::vector<const HistoryEntry *> cw = lib.continueWatching(10);
    CHECK(cw.size() == 2 && cw[0]->id == "m1" && cw[1]->id == "e1");
    CHECK(lib.progressOf(ContentType::Series, "e2")->watched);
    // written back as schema 2 and read again unchanged
    std::string v2 = lib.serializeHistory();
    CHECK(v2.find("\"version\":2") != std::string::npos);
    LibraryStore again("unused");
    CHECK(again.deserializeHistory(v2, &err));
    again.setProfile("p1");
    CHECK(again.continueWatching(10).size() == 2 && again.history().size() == 4);
    CHECK(!again.deserializeHistory(R"({"version":9,"profiles":{}})", &err));
}

TEST(series_plan_main_button) {
    SeriesInfo info = showWithEpisodes();   // S00E01, S01E01..04, S02E01..04
    LibraryStore lib("unused");
    lib.setProfile("p1");
    // nothing watched: the first regular episode, not the special
    seriesplan::Action a = planOf(info, lib);
    CHECK(a.kind == seriesplan::Kind::Play && info.seasons[(size_t) a.season].number == 1 && a.episode == 0);
    CHECK(seriesplan::label(a, info) == "Play S01E01");
    // S01E03 in progress at 24:16
    playAndStop(lib, episodeItem("e13", "s1", 1, 3), 1456);
    a = planOf(info, lib);
    CHECK(a.kind == seriesplan::Kind::Resume && a.position == 1456);
    CHECK(seriesplan::label(a, info) == "Resume S01E03 \xC2\xB7 24:16");
    // S01E03 completed, nothing else in progress: next is S01E04
    playAndStop(lib, episodeItem("e13", "s1", 1, 3), 3400);
    a = planOf(info, lib);
    CHECK(a.kind == seriesplan::Kind::Next && seriesplan::label(a, info) == "Next S01E04");
    // the last episode of a season completed: the next season's first
    playAndStop(lib, episodeItem("e14", "s1", 1, 4), 3400);
    CHECK(seriesplan::label(planOf(info, lib), info) == "Next S02E01");
    // the next episode is not created before it plays
    CHECK(lib.progressOf(ContentType::Series, "e21") == nullptr);
    // an episode in progress elsewhere wins over "next"
    playAndStop(lib, episodeItem("e12", "s1", 1, 2), 700);
    playAndStop(lib, episodeItem("e14", "s1", 1, 4), 3410);
    a = planOf(info, lib);
    CHECK(a.kind == seriesplan::Kind::Resume && seriesplan::label(a, info) == "Resume S01E02 \xC2\xB7 11:40");
    // barely opened episode last: play it again from the start
    LibraryStore fresh("unused");
    fresh.setProfile("p1");
    playAndStop(fresh, episodeItem("e22", "s1", 2, 2), 2);
    CHECK(seriesplan::label(planOf(info, fresh), info) == "Play S02E02");
    // the whole series finished: back to the first episode
    LibraryStore done("unused");
    done.setProfile("p1");
    playAndStop(done, episodeItem("e24", "s1", 2, 4), 3415);
    CHECK(seriesplan::label(planOf(info, done), info) == "Play S01E01");

    CHECK(seriesplan::episodeState(nullptr) == seriesplan::EpisodeState::Untouched);
    CHECK(seriesplan::episodeState(lib.progressOf(ContentType::Series, "e12")) == seriesplan::EpisodeState::InProgress);
    CHECK(seriesplan::episodeState(lib.progressOf(ContentType::Series, "e14")) == seriesplan::EpisodeState::Watched);
    CHECK(seriesplan::episodeState(fresh.progressOf(ContentType::Series, "e22")) == seriesplan::EpisodeState::Untouched);
    SeriesInfo none;
    CHECK(planOf(none, lib).kind == seriesplan::Kind::None && seriesplan::label(planOf(none, lib), none).empty());
}

TEST(hud_auto_hide_after_4_seconds) {
    hud::State s;
    s.visible = true;
    s.playing = true;
    s.lastActivity = 100;
    CHECK(hud::AUTO_HIDE_SECONDS == 4.0);
    CHECK(!hud::shouldAutoHide(s, 103.9));
    CHECK(hud::shouldAutoHide(s, 104.0));
    // input / seek reset the timer
    s.lastActivity = 103;
    CHECK(!hud::shouldAutoHide(s, 104.5));
    // paused: stays
    s.paused = true;
    CHECK(!hud::shouldAutoHide(s, 200));
    s.paused = false;
    // Playback options open, seek pending, end panel: stays
    s.panelOpen = true;
    CHECK(!hud::shouldAutoHide(s, 200));
    s.panelOpen = false;
    s.seekPending = true;
    CHECK(!hud::shouldAutoHide(s, 200));
    s.seekPending = false;
    s.finished = true;
    CHECK(!hud::shouldAutoHide(s, 200));
    s.finished = false;
    // buffering / reconnecting / error (not plainly playing): stays
    s.playing = false;
    CHECK(!hud::shouldAutoHide(s, 200));
    s.playing = true;
    s.visible = false;
    CHECK(!hud::shouldAutoHide(s, 200));
    s.visible = true;
    CHECK(hud::shouldAutoHide(s, 200));
}

// ---------------------------------------------------------------- Remove from Continue Watching

TEST(continue_watching_remove_keeps_progress) {
    std::string dir = tempDir("cw_remove");
    LibraryStore lib(dir);
    lib.load();
    lib.setProfile("p1");
    // 1. a partial movie is in Continue Watching
    playAndStop(lib, movieItem("m1", "Movie One"), 1456);   // 24:16
    CHECK(lib.continueWatching(10).size() == 1);
    lib.toggleFavorite(ContentType::Movie, "m1");
    // a downloaded copy of it (the library never touches download files)
    std::string media = fs::join(dir, "m1.mkv");
    CHECK(fs::writeFileAtomic(media, "media bytes"));
    size_t recentBefore = lib.recentlyWatched(10).size();

    // 2. removed: gone from the row at once
    unsigned g = lib.generation();
    CHECK(lib.dismissFromContinueWatching(ContentType::Movie, "m1"));
    CHECK(lib.generation() != g);   // Home rebuilds its rows immediately
    CHECK(lib.continueWatching(10).empty());
    CHECK(lib.isDismissed(ContentType::Movie, "m1"));
    // 3. / 4. the resume position is unchanged: Movie Detail still offers Resume 24:16
    const HistoryEntry *p = lib.progressOf(ContentType::Movie, "m1");
    CHECK(p && p->position == 1456 && !p->watched);
    CHECK(progress::inProgress(p->position, p->duration, p->watched));
    CHECK(VodProgress(lib).begin(movieItem("m1"), true, 0) == 1456);
    // 5. Recently Watched, 6. favorite, 7. downloaded media: all still there
    CHECK_EQ(lib.recentlyWatched(10).size(), recentBefore);
    CHECK(lib.recentlyWatched(10)[0]->id == "m1");
    CHECK(lib.isFavorite(ContentType::Movie, "m1"));
    CHECK(fs::exists(media));
    // removing twice changes nothing; something not in the row cannot be removed
    CHECK(lib.dismissFromContinueWatching(ContentType::Movie, "m1"));
    CHECK(!lib.dismissFromContinueWatching(ContentType::Movie, "never-played"));
    CHECK(!lib.dismissFromContinueWatching(ContentType::Live, "m1"));

    // 8. app restart: still removed, position still there
    CHECK(lib.save());
    LibraryStore restarted(dir);
    restarted.load();
    restarted.setProfile("p1");
    CHECK(restarted.continueWatching(10).empty());
    CHECK(restarted.isDismissed(ContentType::Movie, "m1"));
    CHECK(restarted.progressOf(ContentType::Movie, "m1")->position == 1456);
    CHECK(restarted.isFavorite(ContentType::Movie, "m1"));
}

TEST(continue_watching_remove_until_new_progress) {
    LibraryStore lib("unused");
    lib.setProfile("p1");
    playAndStop(lib, movieItem("m1"), 1456);
    CHECK(lib.dismissFromContinueWatching(ContentType::Movie, "m1"));
    // 9. opening the detail page only reads the progress
    lib.progressOf(ContentType::Movie, "m1");
    CHECK(lib.continueWatching(10).empty());
    // opening the player and leaving at once (same position) is not new progress
    playAndStop(lib, movieItem("m1"), 1456);
    CHECK(lib.isDismissed(ContentType::Movie, "m1") && lib.continueWatching(10).empty());
    // a short look (30 s) is not meaningful new progress either
    playAndStop(lib, movieItem("m1"), 1486);
    CHECK(lib.isDismissed(ContentType::Movie, "m1") && lib.continueWatching(10).empty());
    CHECK(lib.progressOf(ContentType::Movie, "m1")->position == 1486);
    // 10. resumed and watched on (24:16 -> 27:00): back in the row at the new position
    playAndStop(lib, movieItem("m1"), 1620);
    CHECK(!lib.isDismissed(ContentType::Movie, "m1"));
    std::vector<const HistoryEntry *> cw = lib.continueWatching(10);
    CHECK(cw.size() == 1 && cw[0]->id == "m1" && cw[0]->position == 1620);
}

TEST(continue_watching_remove_episodes) {
    LibraryStore lib("unused");
    lib.setProfile("p1");
    // 11. an episode behaves like a movie
    playAndStop(lib, episodeItem("e13", "s1", 1, 3), 900);
    CHECK(lib.dismissFromContinueWatching(ContentType::Series, "e13"));
    CHECK(lib.continueWatching(10).empty());
    CHECK(lib.progressOf(ContentType::Series, "e13")->position == 900);
    CHECK(VodProgress(lib).begin(episodeItem("e13", "s1", 1, 3), true, 0) == 900);
    playAndStop(lib, episodeItem("e13", "s1", 1, 3), 1100);
    std::vector<const HistoryEntry *> cw = lib.continueWatching(10);
    CHECK(cw.size() == 1 && cw[0]->id == "e13" && cw[0]->position == 1100);

    // 12. two partial episodes of one series: removing the card's episode lets the other stand for the series
    playAndStop(lib, episodeItem("e15", "s1", 1, 5), 600);
    playAndStop(lib, episodeItem("e13", "s1", 1, 3), 1200);   // e13 played last: it is the series card
    cw = lib.continueWatching(10);
    CHECK(cw.size() == 1 && cw[0]->id == "e13");
    CHECK(lib.dismissFromContinueWatching(ContentType::Series, "e13"));
    cw = lib.continueWatching(10);
    CHECK(cw.size() == 1 && cw[0]->id == "e15" && cw[0]->position == 600);   // independent progress
    CHECK(!lib.isDismissed(ContentType::Series, "e15"));
    // removing that one too: the series leaves the row, both positions stay
    CHECK(lib.dismissFromContinueWatching(ContentType::Series, "e15"));
    CHECK(lib.continueWatching(10).empty());
    CHECK(lib.progressOf(ContentType::Series, "e13")->position == 1200);
    CHECK(lib.progressOf(ContentType::Series, "e15")->position == 600);
    // another series is unaffected; so is a movie with the same id as a removed episode
    playAndStop(lib, episodeItem("x1", "s2", 1, 1), 300);
    playAndStop(lib, movieItem("e13"), 300);
    cw = lib.continueWatching(10);
    CHECK(cw.size() == 2);
    CHECK(!lib.isDismissed(ContentType::Movie, "e13"));
}

TEST(continue_watching_remove_profiles_reset_and_completion) {
    LibraryStore lib("unused");
    // 13. profiles are independent
    lib.setProfile("p1");
    playAndStop(lib, movieItem("m1"), 1000);
    lib.setProfile("p2");
    playAndStop(lib, movieItem("m1"), 2000);
    CHECK(lib.dismissFromContinueWatching(ContentType::Movie, "m1"));
    CHECK(lib.continueWatching(10).empty());
    lib.setProfile("p1");
    CHECK(!lib.isDismissed(ContentType::Movie, "m1"));
    CHECK(lib.continueWatching(10).size() == 1);

    // 14. Reset progress (Mark as not watched / Start over) is a different action: it clears the position and
    // the now obsolete removal; the next real playback shows the item again
    CHECK(lib.dismissFromContinueWatching(ContentType::Movie, "m1"));
    lib.resetProgress(ContentType::Movie, "m1");
    CHECK(!lib.isDismissed(ContentType::Movie, "m1"));
    CHECK(lib.progressOf(ContentType::Movie, "m1")->position == 0);
    CHECK(VodProgress(lib).begin(movieItem("m1"), true, 0) == 0);
    playAndStop(lib, movieItem("m1"), 40, false);
    CHECK(lib.continueWatching(10).size() == 1);

    // 15. completed content is never in the row and gets no removal record
    playAndStop(lib, movieItem("done", "Done", 6000), 5990);
    CHECK(!lib.dismissFromContinueWatching(ContentType::Movie, "done"));
    CHECK(!lib.isDismissed(ContentType::Movie, "done"));
    // a removed item played to the end: the record goes away, the item is watched (Recently Watched only)
    playAndStop(lib, movieItem("m2", "Two", 6000), 3000);
    CHECK(lib.dismissFromContinueWatching(ContentType::Movie, "m2"));
    playAndStop(lib, movieItem("m2", "Two", 6000), 5995);
    CHECK(!lib.isDismissed(ContentType::Movie, "m2"));
    CHECK(lib.progressOf(ContentType::Movie, "m2")->watched);
    for (const HistoryEntry *e: lib.continueWatching(10)) {
        CHECK(e->id != "m2" && e->id != "done");
    }

    // 16. Live TV is unaffected: no progress, nothing to remove, Recently Watched as before
    HistoryEntry live;
    live.type = ContentType::Live;
    live.id = "ch1";
    live.name = "Channel";
    lib.addHistory(live);
    CHECK(!lib.dismissFromContinueWatching(ContentType::Live, "ch1"));
    CHECK(lib.recentlyWatched(10)[0]->id == "ch1");
}

TEST(continue_watching_remove_storage_bounded) {
    LibraryStore lib("unused");
    lib.setProfile("p1");
    playAndStop(lib, movieItem("old"), 100);
    CHECK(lib.dismissFromContinueWatching(ContentType::Movie, "old"));
    // the removal record lives only as long as the position it hides: evicted with it
    for (size_t i = 0; i < LibraryStore::PROGRESS_LIMIT; i++) {
        HistoryEntry e;
        e.type = ContentType::Movie;
        e.id = "fill" + std::to_string(i);
        e.position = 50;
        e.duration = 6000;
        lib.updateProgress(e);
    }
    CHECK(lib.progressOf(ContentType::Movie, "old") == nullptr);
    CHECK(!lib.isDismissed(ContentType::Movie, "old"));
    // records whose progress is gone or no longer in progress are dropped on load
    std::string text = "{\"version\":2,\"profiles\":{\"p1\":{\"seq\":3,\"history\":[],\"progress\":["
                       "{\"type\":\"movie\",\"id\":\"a\",\"position\":500,\"duration\":6000,\"activity\":1},"
                       "{\"type\":\"movie\",\"id\":\"b\",\"position\":5900,\"duration\":6000,\"watched\":true,"
                       "\"activity\":2}],"
                       "\"dismissed\":[{\"key\":\"movie:a\",\"position\":500},{\"key\":\"movie:b\",\"position\":10},"
                       "{\"key\":\"movie:gone\",\"position\":10}]}}}";
    LibraryStore fromFile("unused");
    std::string err;
    CHECK(fromFile.deserializeHistory(text, &err));
    fromFile.setProfile("p1");
    CHECK(fromFile.isDismissed(ContentType::Movie, "a"));
    CHECK(!fromFile.isDismissed(ContentType::Movie, "b"));
    CHECK(!fromFile.isDismissed(ContentType::Movie, "gone"));
    CHECK(fromFile.continueWatching(10).empty());
    CHECK(fromFile.serializeHistory().find("movie:gone") == std::string::npos);
    CHECK(fromFile.serializeHistory().find("movie:a") != std::string::npos);
    // a file without the field (written by the previous version) loads as before
    CHECK(fromFile.deserializeHistory("{\"version\":2,\"profiles\":{\"p1\":{\"seq\":1,\"history\":[],\"progress\":["
                                      "{\"type\":\"movie\",\"id\":\"a\",\"position\":500,\"duration\":6000,"
                                      "\"activity\":1}]}}}", &err));
    fromFile.setProfile("p1");
    CHECK(fromFile.continueWatching(10).size() == 1);
    CHECK(fromFile.serializeHistory().find("dismissed") == std::string::npos);
}
