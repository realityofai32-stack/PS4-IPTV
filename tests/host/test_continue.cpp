// Series Continue Watching progression (storage/library_store.h upNext, app/auto_next.h CompletionLatch /
// persistUpNext, app/vod_progress.h): a player is simulated frame by frame with the same calls VodPlayerScreen makes
// (VodProgress observe / record, the 45 s latch, the up-next pointer, the 7 s card, end of file, exit).
// Includes the hardware regression: a series opened from Continue Watching, watched into its last minutes, exited -
// the series must stay in Continue Watching.

#include <cmath>
#include <vector>

#include "check.h"
#include "../../src/app/auto_next.h"
#include "../../src/app/series_plan.h"
#include "../../src/app/vod_progress.h"
#include "../../src/storage/library_store.h"

using namespace iptv;
using screens::VodItem;

namespace {
    const double DUR = 2700;   // 45-minute episodes: 93 % is 189 s before the end, far more than 45 s

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

    // S01: 1 2 3 | S02: 1 2 5 (non-contiguous) | S03: 2 (starts at number 2)
    SeriesInfo catalog() {
        SeriesInfo info;
        info.seriesId = "77";
        Season s1, s2, s3;
        s1.number = 1;
        s1.episodes = {ep("101", 1, 1), ep("102", 1, 2), ep("103", 1, 3)};
        s2.number = 2;
        s2.episodes = {ep("201", 2, 1), ep("202", 2, 2), ep("205", 2, 5)};
        s3.number = 3;
        s3.episodes = {ep("302", 3, 2)};
        info.seasons = {s1, s2, s3};
        return info;
    }

    VodItem itemOf(const Episode &e) {
        VodItem it;
        it.type = ContentType::Series;
        it.id = e.id;
        it.seriesId = "77";
        it.seriesName = "The Series";
        it.title = e.title;
        it.image = "https://img.example/cover.jpg";
        it.extension = "mkv";
        it.season = e.season;
        it.episode = e.number;
        it.durationHint = e.durationSeconds;
        return it;
    }

    VodItem byId(const SeriesInfo &info, const std::string &id) {
        for (const auto &s: info.seasons) {
            for (const auto &e: s.episodes) {
                if (e.id == id) {
                    return itemOf(e);
                }
            }
        }
        return VodItem();
    }

    // What Home builds from a Continue Watching entry (HomeScreen::itemOf)
    VodItem fromContinue(const HistoryEntry &h) {
        VodItem it;
        it.type = h.type;
        it.id = h.id;
        it.extension = h.extension.empty() ? "mkv" : h.extension;
        it.title = h.name;
        it.image = h.icon;
        it.seriesId = h.seriesId;
        it.seriesName = h.seriesName;
        it.season = h.season;
        it.episode = h.episode;
        it.durationHint = h.duration;
        return it;
    }

    // VodPlayerScreen's progress / completion / card logic, frame by frame (4 frames a second of playback)
    struct Player {
        LibraryStore &lib;
        const SeriesInfo *info;
        bool offline = false;
        bool autoplay = true;
        std::vector<VodItem> downloads;
        std::vector<VodItem> queue;
        int index = 0;
        int next = -1;
        VodProgress tracker;
        autonext::CompletionLatch latch;
        autonext::Countdown card;
        bool upNextDone = false;
        bool finished = false;
        double t = 1000;
        double pos = 0;
        int transitions = 0;
        bool cardSeen = false;

        Player(LibraryStore &l, const SeriesInfo *i) : lib(l), info(i), tracker(l) {}

        void open(const VodItem &item, bool resume) {
            int p = 0;
            queue = autonext::episodeQueue(item, offline ? nullptr : info, downloads, offline, p);
            start(p, resume);
        }

        void start(int i, bool resume) {
            index = i;
            finished = false;
            latch.reset();
            card.reset();
            upNextDone = false;
            cardSeen = false;
            next = autonext::nextIndex(queue, index, offline ? nullptr : info, offline);
            pos = tracker.begin(queue[(size_t) index], resume, t);
        }

        void persist() {
            if (upNextDone || next < 0) {
                return;
            }
            upNextDone = true;
            autonext::persistUpNext(lib, queue, index, next, offline ? nullptr : info, 1700000000);
        }

        // one frame at the current position; true = the card started the next episode
        bool frame(double dt = 0.25) {
            t += dt;
            tracker.observe(pos, DUR, true, t);
            switch (latch.update(tracker.position(), DUR, tracker.started())) {
                case autonext::CompletionLatch::Change::Latched:
                    tracker.setCompleted(true);
                    tracker.record(t);
                    persist();
                    break;
                case autonext::CompletionLatch::Change::Released:
                    tracker.setCompleted(false);
                    break;
                default:
                    break;
            }
            if (latch.latched() && !upNextDone && next >= 0) {
                persist();
            }
            if (tracker.due(t) != VodProgress::Due::None) {
                tracker.record(t);
            }
            autonext::Frame f;
            f.hasNext = next >= 0;
            f.autoPlay = autoplay;
            f.running = true;
            f.remaining = DUR - pos;
            f.now = t;
            if (card.update(f)) {
                return advance();
            }
            cardSeen = cardSeen || card.visible();
            return false;
        }

        // plays (4 fps) up to `to` seconds; stops early when the card starts the next episode
        bool playTo(double to) {
            while (pos < to - 1e-9) {
                pos = std::min(to, pos + 0.25);
                if (frame()) {
                    return true;
                }
            }
            return false;
        }

        bool advance() {
            finished = true;
            tracker.record(t, -1, true);
            persist();
            transitions++;
            start(next, true);
            return true;
        }

        void endOfFile() {
            finished = true;
            tracker.record(t, -1, true);
            persist();
            if (card.endOfFile(next >= 0, autoplay, t)) {
                // the card after the end: Auto-play ON starts the next episode when its 7 s have run
                for (int i = 0; i < 8 * 60; i++) {
                    t += 1.0 / 60;
                    autonext::Frame f;
                    f.hasNext = true;
                    f.autoPlay = autoplay;
                    f.now = t;
                    if (card.update(f)) {
                        transitions++;
                        start(next, true);
                        return;
                    }
                }
            }
        }

        // Circle / Back: the exact position written, the player closes
        void exit() {
            if (!finished) {
                tracker.record(t + 0.1, pos);
            }
        }

        void seekTo(double target) {
            tracker.seekPending(target);
            tracker.seekCommitted(target, t);
            pos = target;
            frame();
        }
    };

    const HistoryEntry *continueEntry(LibraryStore &lib) {
        for (const HistoryEntry *h: lib.continueWatching(10)) {
            if (h->seriesId == "77") {
                return h;
            }
        }
        return nullptr;
    }

    // a fresh library with S01E02 in progress at 20:00 (as Continue Watching shows it after earlier viewing)
    void seedInProgress(LibraryStore &lib, const SeriesInfo &info, const std::string &id, double at) {
        Player p(lib, &info);
        p.open(byId(info, id), false);
        p.playTo(at);
        p.exit();
    }
}

// THE HARDWARE BUG: opened from Continue Watching, watched into the credits (past 93 % but more than 45 s left), then
// Back - the series vanished (93 % marked the episode watched and nothing pointed at the next one)
TEST(continue_regression_exit_near_the_end_keeps_the_series) {
    SeriesInfo info = catalog();
    LibraryStore lib("unused");
    seedInProgress(lib, info, "102", 1200);
    const HistoryEntry *cw = continueEntry(lib);
    CHECK(cw && cw->id == "102" && std::fabs(cw->position - 1200) < 1);
    // from Continue Watching, resumed
    Player p(lib, &info);
    p.open(fromContinue(*cw), true);
    CHECK(std::fabs(p.pos - 1200) < 1);
    p.playTo(DUR - 100);   // 96 %: the old rule called it watched
    p.exit();
    cw = continueEntry(lib);
    CHECK(cw != nullptr);
    CHECK(cw && cw->id == "102" && std::fabs(cw->position - (DUR - 100)) < 0.5 && !cw->watched);
    // reopened: the same episode at that exact position
    Player again(lib, &info);
    again.open(fromContinue(*cw), true);
    CHECK(again.queue[(size_t) again.index].id == "102" && std::fabs(again.pos - (DUR - 100)) < 0.5);
    // ... and this time into the last 45 s, then Back: the next episode is Continue Watching at 0
    again.playTo(DUR - 30);
    again.exit();
    cw = continueEntry(lib);
    CHECK(cw && cw->id == "103" && cw->position == 0 && cw->upNext);
    CHECK(lib.progressOf(ContentType::Series, "102")->watched);
}

TEST(continue_normal_unfinished_episode) {
    SeriesInfo info = catalog();
    LibraryStore lib("unused");
    Player p(lib, &info);
    p.open(byId(info, "201"), false);   // from Series Detail
    p.playTo(DUR - 46);                 // 46 s left: not complete
    CHECK(!p.latch.latched() && !p.cardSeen);
    p.exit();
    const HistoryEntry *cw = continueEntry(lib);
    CHECK(cw && cw->id == "201" && std::fabs(cw->position - (DUR - 46)) < 0.5);
    CHECK(lib.progressOf(ContentType::Series, "202") == nullptr);   // nothing invented for the next one
    // Series Detail's main button agrees: Resume that episode
    seriesplan::Action a = seriesplan::defaultAction(info, "77", lib);
    CHECK(a.kind == seriesplan::Kind::Resume && a.season == 1 && a.episode == 0);
}

TEST(continue_latch_at_45_seconds_persists_the_next_episode_at_once) {
    SeriesInfo info = catalog();
    LibraryStore lib("unused");
    Player p(lib, &info);
    p.autoplay = false;
    p.open(byId(info, "201"), false);
    p.playTo(DUR - 45.25);
    CHECK(!p.latch.latched());
    CHECK(lib.progressOf(ContentType::Series, "202") == nullptr);
    p.playTo(DUR - 45);                  // the threshold: remaining <= 45
    CHECK(p.latch.latched());
    // persisted when the threshold is crossed, not at the end of file
    const HistoryEntry *n = lib.progressOf(ContentType::Series, "202");
    CHECK(n && n->upNext && n->position == 0 && !n->watched && n->season == 2 && n->episode == 2);
    CHECK(lib.progressOf(ContentType::Series, "201")->watched);
    const HistoryEntry *cw = continueEntry(lib);
    CHECK(cw && cw->id == "202");
    // the periodic saves after the latch keep the current episode complete (not back in progress)
    p.playTo(DUR - 20);
    CHECK(lib.progressOf(ContentType::Series, "201")->watched);
    CHECK(continueEntry(lib)->id == "202");
    // Series Detail: "Next S02E02"
    seriesplan::Action a = seriesplan::defaultAction(info, "77", lib);
    CHECK(a.kind == seriesplan::Kind::Next && a.season == 1 && a.episode == 1);
}

TEST(continue_kept_after_eof_autoplay_off_watch_credits_and_exit) {
    SeriesInfo info = catalog();
    // 1. end of file with Auto-play off: the end panel, the series stays with the next episode
    {
        LibraryStore lib("unused");
        Player p(lib, &info);
        p.autoplay = false;
        p.open(byId(info, "101"), false);
        p.playTo(DUR);
        CHECK(p.cardSeen && !p.card.counting());   // Auto-play OFF: the manual card, no countdown
        p.endOfFile();
        CHECK(p.transitions == 0 && p.card.visible());
        const HistoryEntry *cw = continueEntry(lib);
        CHECK(cw && cw->id == "102" && cw->upNext);
    }
    // 2. Watch credits (the card dismissed), then Back during the credits
    {
        LibraryStore lib("unused");
        Player p(lib, &info);
        p.open(byId(info, "101"), false);
        p.playTo(DUR - 45.25);
        CHECK(!p.card.visible());                 // the card appears with 45 s left ...
        p.playTo(DUR - 42);
        CHECK(p.card.visible() && p.card.counting() && p.card.seconds() == 4);   // ... counting 7 s at once (3 s gone)
        CHECK(p.card.cancel() && !p.card.visible());
        p.playTo(DUR - 2);                        // the credits play on: no automatic next episode
        CHECK(p.transitions == 0);
        p.exit();
        const HistoryEntry *cw = continueEntry(lib);
        CHECK(cw && cw->id == "102" && cw->upNext && cw->position == 0);
        // and the end of file after Watch credits: no automatic next episode, the pointer stays
        p.playTo(DUR);
        p.endOfFile();
        CHECK(p.transitions == 0 && continueEntry(lib)->id == "102");
    }
    // 3. Back while the card is counting (before its 7 s ran out): the series moves on all the same
    {
        LibraryStore lib("unused");
        Player p(lib, &info);
        p.open(byId(info, "101"), false);
        p.playTo(DUR - 41);
        CHECK(p.cardSeen && p.card.visible() && p.transitions == 0);
        p.exit();
        CHECK(continueEntry(lib)->id == "102");
    }
    // 4. no input: the next episode starts 7 s after the card appeared (~38 s before the end), once
    {
        LibraryStore lib("unused");
        Player p(lib, &info);
        p.open(byId(info, "101"), false);
        CHECK(p.playTo(DUR));
        CHECK(p.transitions == 1 && p.queue[(size_t) p.index].id == "102");
        CHECK(lib.progressOf(ContentType::Series, "101")->watched);
    }
}

// the auto-next path: the card starts the next episode; leaving it within its first seconds keeps the series
TEST(continue_auto_next_then_immediate_exit) {
    SeriesInfo info = catalog();
    LibraryStore lib("unused");
    Player p(lib, &info);
    p.open(byId(info, "102"), false);
    CHECK(p.playTo(DUR));                // the card ran out: next episode started
    CHECK(p.transitions == 1 && p.queue[(size_t) p.index].id == "103" && p.pos == 0);
    // 3 s into the next episode, Back (the old code: < 5 s was never Continue Watching -> the series vanished)
    p.playTo(3);
    p.exit();
    const HistoryEntry *cw = continueEntry(lib);
    CHECK(cw && cw->id == "103");
    // opened again from Continue Watching: starts at 0 (nothing to resume yet), the same episode
    Player again(lib, &info);
    again.open(fromContinue(*cw), true);
    CHECK(again.queue[(size_t) again.index].id == "103" && again.pos == 0);
    // real progress replaces the pointer: in progress at 10:00
    again.playTo(600);
    again.exit();
    cw = continueEntry(lib);
    CHECK(cw && cw->id == "103" && !cw->upNext && std::fabs(cw->position - 600) < 0.5);
}

TEST(continue_opening_never_removes_the_series) {
    SeriesInfo info = catalog();
    LibraryStore lib("unused");
    seedInProgress(lib, info, "202", 900);
    // opened from Continue Watching and left at once (the first frame never came / under a second)
    Player p(lib, &info);
    p.open(fromContinue(*continueEntry(lib)), true);
    p.exit();
    CHECK(continueEntry(lib) && continueEntry(lib)->id == "202" && std::fabs(continueEntry(lib)->position - 900) < 0.5);
    // another episode opened from Series Detail for a few seconds: the in-progress one still stands for the series
    Player q(lib, &info);
    q.open(byId(info, "205"), false);
    q.playTo(3);
    q.exit();
    CHECK(continueEntry(lib) && continueEntry(lib)->id == "202");
}

TEST(continue_season_transition_and_non_contiguous_numbers) {
    SeriesInfo info = catalog();
    // S01E03 (last of season 1) -> S02E01
    {
        LibraryStore lib("unused");
        Player p(lib, &info);
        p.open(byId(info, "103"), false);
        p.playTo(DUR - 40);
        p.exit();
        const HistoryEntry *cw = continueEntry(lib);
        CHECK(cw && cw->id == "201" && cw->season == 2 && cw->episode == 1);
    }
    // S02E02 -> S02E05 (the provider skips 3 and 4: never fabricated)
    {
        LibraryStore lib("unused");
        Player p(lib, &info);
        p.open(byId(info, "202"), false);
        p.playTo(DUR - 40);
        p.exit();
        const HistoryEntry *cw = continueEntry(lib);
        CHECK(cw && cw->id == "205" && cw->episode == 5);
    }
    // S02E05 -> S03E02 (the next season starts at number 2)
    {
        LibraryStore lib("unused");
        Player p(lib, &info);
        p.open(byId(info, "205"), false);
        p.playTo(DUR - 40);
        p.exit();
        const HistoryEntry *cw = continueEntry(lib);
        CHECK(cw && cw->id == "302" && cw->season == 3 && cw->episode == 2);
    }
}

TEST(continue_final_episode_completes_the_series) {
    SeriesInfo info = catalog();
    LibraryStore lib("unused");
    Player p(lib, &info);
    p.open(byId(info, "302"), false);
    CHECK(p.next < 0);
    p.playTo(DUR - 10);
    CHECK(p.latch.latched() && !p.cardSeen);   // no card: there is no next episode
    p.playTo(DUR);
    p.endOfFile();
    CHECK(continueEntry(lib) == nullptr);       // the existing completion behaviour: the series leaves the row
    for (const HistoryEntry *h: lib.seriesProgress("77")) {
        CHECK(!h->upNext);                       // nothing invalid created
    }
}

TEST(continue_seek_back_releases_the_latch) {
    SeriesInfo info = catalog();
    LibraryStore lib("unused");
    Player p(lib, &info);
    p.open(byId(info, "201"), false);
    p.playTo(DUR - 40);
    CHECK(p.latch.latched() && continueEntry(lib)->id == "202");
    p.seekTo(DUR - 600);                        // ten minutes back: in progress again
    CHECK(!p.latch.latched());
    p.playTo(DUR - 590);
    p.exit();
    const HistoryEntry *cw = continueEntry(lib);
    CHECK(cw && cw->id == "201" && !cw->watched && std::fabs(cw->position - (DUR - 590)) < 0.5);
}

TEST(continue_replaying_an_older_episode_never_moves_backwards) {
    SeriesInfo info = catalog();
    LibraryStore lib("unused");
    seedInProgress(lib, info, "205", 700);   // the series is at S02E05
    Player p(lib, &info);
    p.open(byId(info, "102"), false);        // S01E02 replayed from Series Detail
    p.playTo(DUR - 10);
    p.exit();
    const HistoryEntry *cw = continueEntry(lib);
    CHECK(cw && cw->id == "205" && std::fabs(cw->position - 700) < 0.5);   // unchanged
    CHECK(lib.progressOf(ContentType::Series, "103") == nullptr);           // no pointer to S01E03
    // the next episode itself in progress is kept (its position is not reset to 0)
    LibraryStore lib2("unused");
    seedInProgress(lib2, info, "202", 500);
    Player q(lib2, &info);
    q.open(byId(info, "201"), false);
    q.playTo(DUR - 10);
    q.exit();
    cw = continueEntry(lib2);
    CHECK(cw && cw->id == "202" && std::fabs(cw->position - 500) < 0.5 && !cw->upNext);
    // the rule itself: the next episode (queue index 4 = S02E02) already in progress is not moved
    CHECK(q.queue[4].id == "202" && autonext::advancesContinue(q.queue, 4, &info, cw) == false);
}

TEST(continue_offline_playback) {
    SeriesInfo info = catalog();
    auto local = [&](const std::string &id) {
        VodItem d = byId(info, id);
        d.localPath = "/data/PS4IPTV/downloads/" + id + ".mkv";
        return d;
    };
    // the next episode is downloaded: it becomes Continue Watching
    {
        LibraryStore lib("unused");
        Player p(lib, &info);
        p.offline = true;
        p.downloads = {local("201"), local("202")};
        p.open(local("201"), false);
        CHECK(p.next >= 0 && p.queue[(size_t) p.next].id == "202");
        p.playTo(DUR - 40);
        p.exit();
        CHECK(continueEntry(lib) && continueEntry(lib)->id == "202");
    }
    // the next episode is not downloaded: no card, no pointer (nothing playable offline is invented)
    {
        LibraryStore lib("unused");
        Player p(lib, &info);
        p.offline = true;
        p.downloads = {local("201"), local("205")};
        p.open(local("201"), false);
        CHECK(p.next < 0);
        p.playTo(DUR - 3);
        CHECK(!p.cardSeen);
        p.exit();
        CHECK(continueEntry(lib) == nullptr);
    }
}

TEST(continue_storage_rules) {
    LibraryStore lib("unused");
    HistoryEntry e;
    e.type = ContentType::Series;
    e.id = "202";
    e.seriesId = "77";
    e.season = 2;
    e.episode = 2;
    e.duration = DUR;
    lib.markUpNext(e);
    const HistoryEntry *p = lib.progressOf(ContentType::Series, "202");
    CHECK(p && p->upNext && p->position == 0 && progress::continueEligible(*p));
    CHECK(lib.recentlyWatched(10).empty());      // nothing was played: not Recently Watched
    // a second pointer of the same series clears the first
    HistoryEntry f = e;
    f.id = "205";
    lib.markUpNext(f);
    CHECK(!lib.progressOf(ContentType::Series, "202")->upNext && lib.progressOf(ContentType::Series, "205")->upNext);
    CHECK(lib.continueWatching(10).size() == 1 && lib.continueWatching(10)[0]->id == "205");
    // removable from Continue Watching like an episode in progress; real progress brings it back
    CHECK(lib.dismissFromContinueWatching(ContentType::Series, "205"));
    CHECK(lib.continueWatching(10).empty());
    lib.markUpNext(f);                           // progress in the series again
    CHECK(lib.continueWatching(10).size() == 1);
    // saved and loaded: the pointer survives (and an older version simply sees a position-0 entry)
    LibraryStore back("unused");
    std::string err;
    CHECK(back.deserializeHistory(lib.serializeHistory(), &err));
    const HistoryEntry *r = back.progressOf(ContentType::Series, "205");
    CHECK(r && r->upNext && back.continueWatching(10).size() == 1);
    CHECK(lib.serializeHistory().find("\"upNext\"") != std::string::npos);
    // movies never get pointers
    HistoryEntry m;
    m.type = ContentType::Movie;
    m.id = "m1";
    m.seriesId = "x";
    lib.markUpNext(m);
    CHECK(lib.progressOf(ContentType::Movie, "m1") == nullptr);
    // the type-aware rule: an episode with 100 s left is in progress, a movie at 96 % is watched
    CHECK(!progress::isWatched(DUR - 100, DUR, ContentType::Series) && progress::isWatched(DUR - 45, DUR, ContentType::Series));
    CHECK(progress::isWatched(DUR - 100, DUR, ContentType::Movie));
    // a reset (Watch again) clears the pointer
    lib.resetProgress(ContentType::Series, "205");
    CHECK(!lib.progressOf(ContentType::Series, "205")->upNext);
}

TEST(continue_latch_edges) {
    autonext::CompletionLatch l;
    CHECK(l.update(DUR - 10, DUR, false) == autonext::CompletionLatch::Change::None);   // no frame yet
    CHECK(l.update(DUR - 10, 0, true) == autonext::CompletionLatch::Change::None);      // duration unknown
    CHECK(l.update(DUR - 10, std::nan(""), true) == autonext::CompletionLatch::Change::None);
    CHECK(l.update(2, 30, true) == autonext::CompletionLatch::Change::None);            // a 30 s file: not before 5 s
    CHECK(l.update(DUR - 45.01, DUR, true) == autonext::CompletionLatch::Change::None);
    CHECK(l.update(DUR - 45, DUR, true) == autonext::CompletionLatch::Change::Latched);
    CHECK(l.update(DUR - 45.9, DUR, true) == autonext::CompletionLatch::Change::None);  // jitter: kept
    CHECK(l.update(DUR - 47, DUR, true) == autonext::CompletionLatch::Change::Released);
    CHECK(!l.latched());
}
