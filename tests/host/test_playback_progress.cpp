// The Continue Watching regression of v0.4.0 and the first v0.5.0 package (app/playback_progress.h): exiting a series episode near its end removed the
// series from Continue Watching. Every way the playback of an item ends is driven here through PlaybackProgress - the
// object VodPlayerScreen records through - with the screen's own call order (tick: observe, update, due -> record;
// Circle / Stop / app exit / pause / episode change: record with the position queried from the player; end of file and
// the transition to the next episode: record at the end; a player error: the periodic record).
//
// The player is modelled the way the PS4 build sees it: `live` is mpv's position right now (Playback::queryPosition),
// `sampled` is what the 0.25 s poll last read (Playback::position, not refreshed while paused). The bug needed only
// the two to disagree across the 45 s threshold.

#include <cmath>
#include <memory>
#include <vector>

#include "check.h"
#include "../../src/app/auto_next.h"
#include "../../src/app/playback_progress.h"
#include "../../src/storage/library_store.h"

using namespace iptv;
using screens::VodItem;

namespace {
    const double DUR = 2700;   // 45-minute episodes
    const double POLL = 0.25;  // Playback::pollStats

    Episode ep(const std::string &id, int season, int number) {
        Episode e;
        e.id = id;
        e.season = season;
        e.number = number;
        e.extension = "mkv";
        e.title = "Ep " + id;
        e.durationSeconds = (int) DUR;
        return e;
    }

    // S01: 1 2 3 | S02: 1 2 5 (non-contiguous) | S03: 2 (the final episode, numbered 2)
    std::shared_ptr<const SeriesInfo> catalog() {
        auto info = std::make_shared<SeriesInfo>();
        info->seriesId = "88";
        Season s1, s2, s3;
        s1.number = 1;
        s1.episodes = {ep("e101", 1, 1), ep("e102", 1, 2), ep("e103", 1, 3)};
        s2.number = 2;
        s2.episodes = {ep("e201", 2, 1), ep("e202", 2, 2), ep("e205", 2, 5)};
        s3.number = 3;
        s3.episodes = {ep("e302", 3, 2)};
        info->seasons = {s1, s2, s3};
        return info;
    }

    VodItem episode(const SeriesInfo &info, const std::string &id) {
        for (const auto &s: info.seasons) {
            for (const auto &e: s.episodes) {
                if (e.id == id) {
                    VodItem it;
                    it.type = ContentType::Series;
                    it.id = e.id;
                    it.seriesId = "88";
                    it.seriesName = "Example Series";
                    it.title = e.title;
                    it.image = "https://img.example.com/cover.jpg";
                    it.extension = "mkv";
                    it.season = e.season;
                    it.episode = e.number;
                    it.durationHint = e.durationSeconds;
                    return it;
                }
            }
        }
        return VodItem();
    }

    VodItem movie(const std::string &id) {
        VodItem it;
        it.type = ContentType::Movie;
        it.id = id;
        it.title = "Example Movie";
        it.extension = "mp4";
        it.durationHint = 6000;
        return it;
    }

    int64_t fixedClock() { return 1700000000; }

    // VodPlayerScreen's calls into PlaybackProgress, in its order
    struct PlayerScreen {
        LibraryStore &lib;
        std::shared_ptr<const SeriesInfo> info;
        bool autoPlay = true;
        double cardShow = autonext::CARD_SHOW_REMAINING;   // Settings: the card appears with this much left
        double duration = DUR;      // what the player reports (0: unknown)
        std::vector<VodItem> queue;
        int index = 0;
        int next = -1;
        PlaybackProgress tracker;
        autonext::Countdown card;
        bool finished = false;
        bool closed = false;
        bool paused = false;
        double t = 1000;
        double live = 0;            // mpv's time-pos right now (queryPosition)
        double sampled = 0;         // the last 0.25 s poll (Playback::position)
        double sinceSample = 0;
        int transitions = 0;

        PlayerScreen(LibraryStore &l, std::shared_ptr<const SeriesInfo> i) : lib(l), info(std::move(i)),
                                                                        tracker(l, &fixedClock) {}

        // Series Detail / Continue Watching: screens::playSeriesEpisode
        void open(const VodItem &item, bool resume) {
            int p = 0;
            queue = autonext::episodeQueue(item, info.get(), {}, false, p);
            startItem(p, resume);
        }

        void openMovie(const VodItem &item, bool resume) {
            queue = {item};
            startItem(0, resume);
        }

        // VodPlayerScreen::startItem (resolveNext sets the context before begin)
        void startItem(int i, bool resume) {
            index = i;
            finished = false;
            closed = false;
            paused = false;
            card.reset();
            next = queue[(size_t) i].type == ContentType::Series ? autonext::nextIndex(queue, index, info.get(), false)
                                                                 : -1;
            tracker.setContext(queue, index, next, queue[(size_t) i].type == ContentType::Series ? info : nullptr);
            live = sampled = tracker.begin(queue[(size_t) i], resume, t);
            sinceSample = 0;
        }

        // VodPlayerScreen::tick (one frame)
        bool tick(double dt) {
            t += dt;
            sinceSample += dt;
            if (!paused && sinceSample >= POLL - 1e-9) {
                sampled = live;   // pollStats: only while playing
                sinceSample = 0;
            }
            tracker.observe(sampled, duration, true, t);
            if (finished) {
                return false;
            }
            tracker.update(t, tracker.duration() > 0 ? tracker.duration() : duration);   // updateCompletion
            if (tracker.due(t) != VodProgress::Due::None) {
                tracker.record(t);   // saveProgress
            }
            autonext::Frame f;
            f.hasNext = next >= 0;
            f.autoPlay = autoPlay;
            f.running = true;
            f.paused = paused;
            f.remaining = duration > 0 ? duration - sampled : -1;
            f.now = t;
            f.showRemaining = cardShow;
            if (card.update(f)) {
                advance();   // the card's countdown ran out
                return true;
            }
            return false;
        }

        // plays at 60 fps until mpv is at `to`; false when the card started the next episode on the way
        bool playTo(double to) {
            while (live < to - 1e-9) {
                double dt = 1.0 / 60;
                live = std::min(to, live + dt);
                if (tick(dt)) {
                    return false;
                }
            }
            return true;
        }

        // mpv moves on by `seconds` between two polls: no frame sees it yet (the bug's timing)
        void runAhead(double seconds) { live += seconds; }

        // Playback::queryPosition: mpv's position now (the poll's value becomes it too)
        double query() {
            sampled = live;
            return live;
        }

        // Circle / Back / Stop: VodPlayerScreen::exit -> saveFinal("stop")
        void circle() {
            if (!finished) {
                tracker.record(t + 0.01, query());
            }
            closed = true;
        }

        // the system closes the app while the item plays: onAppExit -> saveFinal("app exit")
        void appExit() { circle(); }

        // Cross pauses: saveFinal("pause", false); the poll stops
        void pause() {
            tracker.record(t + 0.01, query());
            paused = true;
        }

        // R1 / L1: saveFinal("episode change"), then the other episode starts
        void episodeChange(int to) {
            tracker.record(t + 0.01, query());
            startItem(to, true);
        }

        // the player failed for good: supervise -> saveProgress(true) (the periodic record, no query)
        void playerError() {
            tracker.record(t + 0.01);
            finished = true;   // the error panel; Circle then leaves without another record
        }

        // R2 / Right: a pending seek (committed after SEEK_COMMIT_DELAY)
        void seekPending(double target) { tracker.seekPending(target); }

        // the end of file: reachedEnd
        void endOfFile() {
            finished = true;
            tracker.record(t, -1, true);
            card.endOfFile(next >= 0, autoPlay, t);
        }

        // advance: the card's countdown / Play now
        void advance() {
            finished = true;
            tracker.record(t, -1, true);
            transitions++;
            startItem(next, true);
        }

        // Watch Credits / Cancel on the card
        bool watchCredits() { return card.cancel(); }
    };

    const HistoryEntry *continueEntry(LibraryStore &lib, const std::string &seriesId = "88") {
        for (const HistoryEntry *h: lib.continueWatching(20)) {
            if (h->type == ContentType::Series && h->seriesId == seriesId) {
                return h;
            }
        }
        return nullptr;
    }

    const HistoryEntry *movieEntry(LibraryStore &lib, const std::string &id) {
        for (const HistoryEntry *h: lib.continueWatching(20)) {
            if (h->type == ContentType::Movie && h->id == id) {
                return h;
            }
        }
        return nullptr;
    }

    bool isNextAtZero(LibraryStore &lib, const std::string &id) {
        const HistoryEntry *cw = continueEntry(lib);
        return cw && cw->id == id && cw->position == 0 && cw->upNext && !cw->watched;
    }

    bool isAt(LibraryStore &lib, const std::string &id, double position) {
        const HistoryEntry *cw = continueEntry(lib);
        return cw && cw->id == id && std::fabs(cw->position - position) < 1e-6 && !cw->watched;
    }

    // an episode watched from 20:00 (resumed) to `to`
    PlayerScreen &watch(PlayerScreen &s, const std::string &id, double to) {
        s.open(episode(*s.info, id), false);
        s.live = s.sampled = 1200;
        s.playTo(to);
        return s;
    }
}

// 1. more than 45 s left, Circle: the same episode at its exact position
TEST(pp_more_than_45s_left_circle_keeps_the_episode_at_its_exact_position) {
    for (double left: {600.0, 100.0, 45.5}) {
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        watch(s, "e102", DUR - left);
        s.runAhead(0.2);   // mpv a little past the last poll: the queried position is the one stored
        s.circle();
        CHECK(isAt(lib, "e102", DUR - left + 0.2));
        CHECK(!s.tracker.progressionCommitted());
    }
}

// 2. 45 s or less left, Circle: the next episode at 0 - whatever the frames saw before Circle
TEST(pp_45s_or_less_left_circle_moves_to_the_next_episode_at_0) {
    {   // THE REGRESSION: mpv crossed the 45 s threshold after the last poll the latch saw, then Circle
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        watch(s, "e102", DUR - 45.1);
        CHECK(!s.tracker.latched());
        s.runAhead(0.2);   // 44.9 s left at Circle
        s.circle();
        CHECK(isNextAtZero(lib, "e103"));
        const HistoryEntry *done = lib.progressOf(ContentType::Series, "e102");
        CHECK(done && done->watched);
    }
    {   // R2 into the credits, Circle before the seek was committed (same frame)
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        watch(s, "e102", DUR - 120);
        s.seekPending(DUR - 30);
        s.circle();
        CHECK(isNextAtZero(lib, "e103"));
    }
    {   // Cross pressed 45.05 s before the end, mpv pauses 0.1 s later; no poll runs while paused, Circle
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        watch(s, "e102", DUR - 45.2);
        s.runAhead(0.15);
        s.pause();   // saveFinal("pause"): not complete yet
        CHECK(!s.tracker.progressionCommitted());
        s.runAhead(0.1);
        for (int i = 0; i < 600; i++) {
            s.tick(1.0 / 60);   // 10 s of paused frames (periodic records at the stale sample)
        }
        CHECK(!s.tracker.latched());
        s.circle();
        CHECK(isNextAtZero(lib, "e103"));
    }
    {   // paused past the threshold, then Circle
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        watch(s, "e102", DUR - 45.1);
        s.runAhead(0.3);
        s.pause();
        CHECK(isNextAtZero(lib, "e103"));   // the pause record already carries the progression
        for (int i = 0; i < 600; i++) {
            s.tick(1.0 / 60);
        }
        s.circle();
        CHECK(isNextAtZero(lib, "e103"));
    }
    {   // well inside the last 45 s (the latch saw it): unchanged behaviour
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        watch(s, "e102", DUR - 40);
        CHECK(s.tracker.latched() && s.tracker.progressionCommitted());
        s.circle();
        CHECK(isNextAtZero(lib, "e103"));
    }
    {   // app exit at the same moment as the regression
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        watch(s, "e102", DUR - 45.05);
        s.runAhead(0.1);
        s.appExit();
        CHECK(isNextAtZero(lib, "e103"));
    }
}

// 3. across a season boundary: S01's last episode -> S02E01
TEST(pp_season_boundary) {
    LibraryStore lib("unused");
    PlayerScreen s(lib, catalog());
    watch(s, "e103", DUR - 45.1);
    s.runAhead(0.25);
    s.circle();
    CHECK(isNextAtZero(lib, "e201"));
    // and the end of file of S02's last episode -> S03 (which starts at number 2)
    PlayerScreen s2(lib, catalog());
    s2.autoPlay = false;
    watch(s2, "e205", DUR);
    s2.endOfFile();
    s2.circle();
    CHECK(isNextAtZero(lib, "e302"));
}

// 4. non-contiguous numbering: S02E02 -> S02E05
TEST(pp_non_contiguous_numbering) {
    LibraryStore lib("unused");
    PlayerScreen s(lib, catalog());
    watch(s, "e202", DUR - 45.2);
    s.runAhead(0.3);
    s.circle();
    CHECK(isNextAtZero(lib, "e205"));
}

// 5. the final episode: no invented next entry, on every path
TEST(pp_final_episode_creates_no_fake_next) {
    for (int path = 0; path < 4; path++) {
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        watch(s, "e302", DUR - 45.1);
        CHECK(s.next < 0);
        s.runAhead(0.2);
        if (path == 0) {
            s.circle();
        } else if (path == 1) {
            s.playTo(DUR - 5);
            s.circle();
        } else if (path == 2) {
            s.playTo(DUR);
            s.endOfFile();
            s.circle();
        } else {
            s.pause();
            s.appExit();
        }
        CHECK(continueEntry(lib) == nullptr);   // the series is complete: it leaves Continue Watching
        for (const auto &id: {"e101", "e102", "e103", "e201", "e202", "e205"}) {
            CHECK(lib.progressOf(ContentType::Series, id) == nullptr);   // nothing invented
        }
        CHECK(s.tracker.lastUpNext() == autonext::UpNext::None);
        const HistoryEntry *done = lib.progressOf(ContentType::Series, "e302");
        CHECK(done && done->watched && !done->upNext);
    }
}

// 6. replaying an older episode never moves the progression backwards
TEST(pp_replaying_an_older_episode_never_moves_backwards) {
    LibraryStore lib("unused");
    PlayerScreen s(lib, catalog());
    watch(s, "e201", DUR - 10);
    s.circle();
    CHECK(isNextAtZero(lib, "e202"));
    // S01E01 again, to its last seconds (the regression's timing too), then Circle
    PlayerScreen r(lib, catalog());
    r.open(episode(*r.info, "e101"), false);
    r.playTo(30);
    // playing an old episode moves no pointer (Continue Watching shows that episode in progress meanwhile, as before)
    const HistoryEntry *pointer = lib.progressOf(ContentType::Series, "e202");
    CHECK(pointer && pointer->upNext && !pointer->watched && pointer->position == 0);
    CHECK(!r.tracker.progressionCommitted());
    r.live = r.sampled = DUR - 45.1;
    r.playTo(DUR - 45.05);
    r.runAhead(0.2);
    r.circle();
    CHECK(isNextAtZero(lib, "e202"));
    CHECK(r.tracker.lastUpNext() == autonext::UpNext::Kept);
    CHECK(lib.progressOf(ContentType::Series, "e102") == nullptr);
    // to its end and the automatic next episode: S01E02 plays, the entry stays at S02E02
    PlayerScreen a(lib, catalog());
    a.open(episode(*a.info, "e101"), false);
    a.live = a.sampled = DUR - 60;
    CHECK(!a.playTo(DUR));   // the card started S01E02
    CHECK(a.transitions == 1 && a.queue[(size_t) a.index].id == "e102");
    a.playTo(3);
    a.circle();
    CHECK(isNextAtZero(lib, "e202"));   // 3 s of S01E02 is no progress: the entry stays at S02E02
    const HistoryEntry *s2e2 = lib.progressOf(ContentType::Series, "e202");
    CHECK(s2e2 && s2e2->upNext && !s2e2->watched);   // the pointer itself did not move back
    // replaying an already watched episode does not commit anything when it starts
    PlayerScreen w(lib, catalog());
    w.open(episode(*w.info, "e201"), false);
    w.playTo(20);
    w.circle();
    CHECK(!w.tracker.progressionCommitted());
}

// 7. Watch Credits, then Circle
TEST(pp_watch_credits_then_circle) {
    {   // the card at 45 s: Watch Credits, a few seconds of credits, Circle
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        watch(s, "e102", DUR - 40);
        CHECK(s.card.visible());
        CHECK(s.watchCredits());
        s.playTo(DUR - 25);
        s.runAhead(0.2);
        s.circle();
        CHECK(isNextAtZero(lib, "e103"));
        CHECK(s.transitions == 0);
    }
    {   // the card at 90 s (Settings): Watch Credits and Circle with more than 45 s left -> the same episode
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        s.cardShow = 90;
        watch(s, "e102", DUR - 85);
        CHECK(s.card.visible());
        CHECK(s.watchCredits());
        s.playTo(DUR - 70);
        s.circle();
        CHECK(isAt(lib, "e102", DUR - 70));
    }
    {   // the card at 90 s, Watch Credits, Circle right after the threshold (the regression's timing)
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        s.cardShow = 90;
        watch(s, "e102", DUR - 85);
        CHECK(s.watchCredits());
        s.playTo(DUR - 45.1);
        s.runAhead(0.2);
        s.circle();
        CHECK(isNextAtZero(lib, "e103"));
    }
}

// 8. Auto-play OFF, Circle near the end (and after the end of file)
TEST(pp_autoplay_off_then_circle_near_the_end) {
    {
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        s.autoPlay = false;
        watch(s, "e102", DUR - 45.1);
        s.runAhead(0.2);
        s.circle();
        CHECK(isNextAtZero(lib, "e103"));
    }
    {
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        s.autoPlay = false;
        watch(s, "e102", DUR - 15);
        CHECK(s.card.visible() && !s.card.counting());
        s.circle();
        CHECK(isNextAtZero(lib, "e103"));
        CHECK(s.transitions == 0);
    }
    {   // the end of file with the card waiting for Play now, then Circle
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        s.autoPlay = false;
        watch(s, "e102", DUR);
        s.endOfFile();
        CHECK(s.card.afterEnd());
        s.circle();
        CHECK(isNextAtZero(lib, "e103"));
    }
    {   // the end of file without any known duration (the latch never ran)
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        s.duration = 0;
        VodItem e = episode(*s.info, "e102");
        e.durationHint = 0;
        s.open(e, false);
        s.playTo(600);
        CHECK(!s.tracker.latched());
        s.endOfFile();
        s.circle();
        CHECK(isNextAtZero(lib, "e103"));
    }
}

// 9. the newly selected next episode, left at once: it stays the series' entry
TEST(pp_quick_exit_from_the_new_next_episode_keeps_it) {
    for (double quit: {0.0, 0.5, 3.0, 6.0}) {
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        watch(s, "e102", DUR - 60);
        CHECK(!s.playTo(DUR));   // the card's countdown started S01E03
        CHECK(s.transitions == 1 && s.queue[(size_t) s.index].id == "e103");
        s.playTo(quit);
        s.circle();
        const HistoryEntry *cw = continueEntry(lib);
        CHECK(cw && cw->id == "e103");
        CHECK(cw && (quit < 5 ? cw->position <= quit + 1e-6 : std::fabs(cw->position - quit) < 1e-6));
    }
    {   // Play now from the card, the next one opened, Circle within a second
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        watch(s, "e202", DUR - 41);
        CHECK(s.card.playNow());
        s.advance();
        s.playTo(0.4);
        s.circle();
        CHECK(isNextAtZero(lib, "e205"));
    }
    {   // R1 during the last 45 s: the episode is complete, the next one opens and is left at once
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        watch(s, "e102", DUR - 45.1);
        s.runAhead(0.2);
        s.episodeChange(s.next);
        s.playTo(0.5);
        s.circle();
        CHECK(isNextAtZero(lib, "e103"));
    }
}

// 10. movies: unchanged (93 %, exact positions, never a series pointer)
TEST(pp_movies_unchanged) {
    LibraryStore lib("unused");
    PlayerScreen s(lib, nullptr);
    s.duration = 6000;
    s.openMovie(movie("m1"), false);
    s.playTo(3000);
    s.runAhead(0.2);
    s.circle();
    const HistoryEntry *m = movieEntry(lib, "m1");
    CHECK(m && std::fabs(m->position - 3000.2) < 1e-6 && !m->watched);
    // 45 s left is not a movie rule: 91.7 % stays in progress at its position, 93 % is watched
    PlayerScreen s2(lib, nullptr);
    s2.duration = 6000;
    s2.openMovie(movie("m1"), true);
    CHECK(std::fabs(s2.live - 3000.2) < 1e-6);
    s2.playTo(5500);   // 91.7 %
    s2.circle();
    m = movieEntry(lib, "m1");
    CHECK(m && std::fabs(m->position - 5500) < 1e-6);
    PlayerScreen s3(lib, nullptr);
    s3.duration = 6000;
    s3.openMovie(movie("m1"), true);
    s3.playTo(5600);   // 93.3 %
    CHECK(!s3.tracker.latched());
    s3.circle();
    CHECK(movieEntry(lib, "m1") == nullptr);
    const HistoryEntry *done = lib.progressOf(ContentType::Movie, "m1");
    CHECK(done && done->watched && !done->upNext);
    CHECK(!s3.tracker.progressionCommitted());
    CHECK(continueEntry(lib) == nullptr);
}

// every exit path in the last 45 s converges on the same state: the completed episode watched, the next one at 0
TEST(pp_every_exit_path_converges) {
    enum Path { Circle, Stop, AppExit, Pause, EpisodeChange, Error, EndOfFile, AutoNext, PlayNow, Credits };
    for (int p = Circle; p <= Credits; p++) {
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        watch(s, "e202", DUR - 45.1);
        s.runAhead(0.2);
        switch (p) {
            case Circle:
            case Stop:
                s.circle();
                break;
            case AppExit:
                s.appExit();
                break;
            case Pause:
                s.pause();
                s.circle();
                break;
            case EpisodeChange:
                s.episodeChange(s.next);
                break;
            case Error:
                s.tick(POLL);   // the last poll before the failure
                s.playerError();
                s.circle();
                break;
            case EndOfFile:
                s.autoPlay = false;
                s.playTo(DUR);
                s.endOfFile();
                s.circle();
                break;
            case AutoNext:
                CHECK(!s.playTo(DUR));
                break;
            case PlayNow:
                s.playTo(DUR - 41);
                CHECK(s.card.playNow());
                s.advance();
                break;
            case Credits:
                s.playTo(DUR - 40);
                CHECK(s.watchCredits());
                s.playTo(DUR - 2);
                s.circle();
                break;
        }
        CHECK(isNextAtZero(lib, "e205"));
        const HistoryEntry *done = lib.progressOf(ContentType::Series, "e202");
        CHECK(done && done->watched);
    }
    // and with more than 45 s left, every path that keeps the episode keeps its exact position
    for (int p = Circle; p <= Error; p++) {
        LibraryStore lib("unused");
        PlayerScreen s(lib, catalog());
        watch(s, "e202", DUR - 200);
        if (p == Error) {
            s.tick(POLL);
            s.playerError();
            CHECK(isAt(lib, "e202", s.sampled));
            continue;
        }
        s.runAhead(0.2);
        double at = s.live;
        if (p == Pause) {
            s.pause();
        } else if (p == EpisodeChange) {
            s.episodeChange(s.next);   // R1: the next episode opens, the left one stays where it was
            const HistoryEntry *left = lib.progressOf(ContentType::Series, "e202");
            CHECK(left && std::fabs(left->position - at) < 1e-6 && !left->watched);
            continue;
        } else {
            s.circle();
        }
        CHECK(isAt(lib, "e202", at));
    }
}

// the catalog arrives after the episode is complete (Continue Watching right after start): committed then
TEST(pp_next_episode_known_after_completion) {
    LibraryStore lib("unused");
    auto info = catalog();
    PlayerScreen s(lib, nullptr);   // no catalog in memory: a one-item queue
    s.open(episode(*info, "e102"), false);
    CHECK(s.next < 0);
    s.live = s.sampled = DUR - 50;
    s.playTo(DUR - 30);
    CHECK(s.tracker.latched() && !s.tracker.progressionCommitted());
    // completeQueue: the screen's queue from the catalog, resolveNext sets the context again
    int p = 0;
    s.info = info;
    s.queue = autonext::episodeQueue(s.queue[0], info.get(), {}, false, p);
    s.index = p;
    s.next = autonext::nextIndex(s.queue, s.index, info.get(), false);
    s.tracker.setContext(s.queue, s.index, s.next, info);
    s.tick(1.0 / 60);
    CHECK(s.tracker.progressionCommitted());
    s.circle();
    CHECK(isNextAtZero(lib, "e103"));
    // complete, then seeked back out of the last 45 s before the catalog arrived: nothing is committed for an
    // episode in progress again; back in the last 45 s it is
    LibraryStore lib2("unused");
    PlayerScreen b(lib2, nullptr);
    b.autoPlay = false;
    b.open(episode(*info, "e102"), false);
    b.live = b.sampled = DUR - 50;
    b.playTo(DUR - 30);
    CHECK(b.tracker.latched());
    b.seekPending(DUR - 600);
    b.tracker.seekCommitted(DUR - 600, b.t);
    b.live = b.sampled = DUR - 600;
    b.playTo(DUR - 595);
    CHECK(!b.tracker.latched());
    b.info = info;
    b.queue = autonext::episodeQueue(b.queue[0], info.get(), {}, false, p);
    b.index = p;
    b.next = autonext::nextIndex(b.queue, b.index, info.get(), false);
    b.tracker.setContext(b.queue, b.index, b.next, info);
    b.tick(1.0 / 60);
    CHECK(!b.tracker.progressionCommitted());
    CHECK(lib2.progressOf(ContentType::Series, "e103") == nullptr);
    b.live = b.sampled = DUR - 46;
    b.playTo(DUR - 45.05);
    b.runAhead(0.2);
    b.circle();
    CHECK(isNextAtZero(lib2, "e103"));
}

// seeking back out of the last 45 s after the progression was committed: the episode is in progress again at its
// position (as before), the next episode's entry is not taken back
TEST(pp_seek_back_after_completion) {
    LibraryStore lib("unused");
    PlayerScreen s(lib, catalog());
    s.autoPlay = false;
    watch(s, "e102", DUR - 30);
    CHECK(s.tracker.progressionCommitted());
    s.seekPending(DUR - 600);
    s.tracker.seekCommitted(DUR - 600, s.t);
    s.live = s.sampled = DUR - 600;
    s.playTo(DUR - 590);
    CHECK(!s.tracker.latched());
    s.circle();
    const HistoryEntry *e = lib.progressOf(ContentType::Series, "e102");
    CHECK(e && std::fabs(e->position - (DUR - 590)) < 1e-6 && !e->watched);
    const HistoryEntry *n = lib.progressOf(ContentType::Series, "e103");
    CHECK(n && n->upNext);
    CHECK(continueEntry(lib) != nullptr);
}
