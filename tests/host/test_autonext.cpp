// Series episode context and next-episode countdown / auto-play (app/auto_next.h): the queue every entry point
// builds (Series Detail, Continue Watching, Downloads; catalog, downloads, the episode alone, the late catalog),
// which episode is next (catalog order, season boundaries, the last episode, movies / live, offline downloads),
// the Next Episode card's state machine (7..1 on its own clock, Play now, Watch credits, pause, seeking back, end of
// file, exactly one transition, errors) and the Auto-play next episode setting's migration.

#include <cmath>
#include <vector>

#include "check.h"
#include "../../src/app/auto_next.h"
#include "../../src/app/series_plan.h"
#include "../../src/app/vod_progress.h"
#include "../../src/storage/library_store.h"
#include "../../src/storage/settings_store.h"

using namespace iptv;
using screens::VodItem;
using autonext::Countdown;
using autonext::Frame;
using autonext::Phase;

namespace {
    Episode ep(const std::string &id, int season, int number) {
        Episode e;
        e.id = id;
        e.season = season;
        e.number = number;
        e.extension = "mkv";
        return e;
    }

    // S01: 1 2 | S02: 1 2 5 (the provider skips numbers) | S03: 2 3 (its first episode is number 2)
    SeriesInfo catalog() {
        SeriesInfo info;
        info.seriesId = "77";
        Season s1, s2, s3;
        s1.number = 1;
        s1.episodes = {ep("101", 1, 1), ep("102", 1, 2)};
        s2.number = 2;
        s2.episodes = {ep("201", 2, 1), ep("202", 2, 2), ep("205", 2, 5)};
        s3.number = 3;
        s3.episodes = {ep("302", 3, 2), ep("303", 3, 3)};
        info.seasons = {s1, s2, s3};
        return info;
    }

    // the queue Series Detail builds: every episode in catalog order
    std::vector<VodItem> queueOf(const SeriesInfo &info) {
        std::vector<VodItem> q;
        for (const Season &s: info.seasons) {
            for (const Episode &e: s.episodes) {
                VodItem it;
                it.type = ContentType::Series;
                it.id = e.id;
                it.seriesId = info.seriesId;
                it.season = e.season;
                it.episode = e.number;
                q.push_back(it);
            }
        }
        return q;
    }

    int at(const std::vector<VodItem> &q, const std::string &id) {
        for (size_t i = 0; i < q.size(); i++) {
            if (q[i].id == id) {
                return (int) i;
            }
        }
        return -1;
    }

    std::string nextId(const std::vector<VodItem> &q, const std::string &id, const SeriesInfo *info, bool offline) {
        int n = autonext::nextIndex(q, at(q, id), info, offline);
        return n < 0 ? "" : q[(size_t) n].id;
    }

    // the player's clock (monotonic seconds) for the card's own 7 s
    double clockNow = 100;

    Frame frame(double remaining, bool hasNext = true, bool running = true, bool paused = false, bool autoPlay = true) {
        Frame f;
        f.hasNext = hasNext;
        f.autoPlay = autoPlay;
        f.running = running;
        f.paused = paused;
        f.remaining = remaining;
        f.now = clockNow;
        return f;
    }

    // `seconds` of frames at 60 fps at a fixed remaining time (a file stuck short of its end, a pause, buffering);
    // returns true when the card started the next episode
    bool hold(Countdown &c, double seconds, double remaining, bool running = true, bool paused = false,
              bool autoPlay = true) {
        for (int i = 0; i < (int) std::lround(seconds * 60); i++) {
            clockNow += 1.0 / 60;
            if (c.update(frame(remaining, true, running, paused, autoPlay))) {
                return true;
            }
        }
        return false;
    }

    // plays from `from` seconds before the end at 60 fps (playback time and the clock advance together); returns the
    // number of starts, collects the shown numbers
    int playOut(Countdown &c, double from, std::vector<int> &shown, double to = -0.1) {
        int starts = 0;
        for (double r = from; r >= to; r -= 1.0 / 60) {
            clockNow += 1.0 / 60;
            if (c.update(frame(std::max(0.0, r)))) {
                starts++;
                break;
            }
            if (c.visible() && (shown.empty() || shown.back() != c.seconds())) {
                shown.push_back(c.seconds());
            }
        }
        return starts;
    }
}

TEST(autonext_next_episode_in_catalog_order) {
    SeriesInfo info = catalog();
    std::vector<VodItem> q = queueOf(info);
    // 1. a middle episode: S02E02 -> S02E05 (the provider's next one, not number + 1)
    CHECK_EQ(nextId(q, "201", &info, false), std::string("202"));
    CHECK_EQ(nextId(q, "202", &info, false), std::string("205"));
    // 2. the season boundary: the last episode of S01 / S02 -> the first episode of the next season
    CHECK_EQ(nextId(q, "102", &info, false), std::string("201"));
    CHECK_EQ(nextId(q, "205", &info, false), std::string("302"));
    // 3. the series' last episode: none
    CHECK_EQ(nextId(q, "303", &info, false), std::string(""));
    // Series Detail's "Next" uses the same rule
    int ns = -1, ne = -1;
    CHECK(seriesplan::nextEpisode(info, 1, 2, ns, ne) && ns == 2 && ne == 0);
    CHECK(!seriesplan::nextEpisode(info, 2, 1, ns, ne));
    CHECK(!seriesplan::nextEpisode(info, 9, 0, ns, ne) && !seriesplan::nextEpisode(info, 0, -1, ns, ne));
    // the catalog's next episode must be playable from the queue (a one-episode queue has none)
    std::vector<VodItem> single = {q[(size_t) at(q, "202")]};
    CHECK_EQ(autonext::nextIndex(single, 0, &info, false), -1);
    // an out-of-range index
    CHECK_EQ(autonext::nextIndex(q, -1, &info, false), -1);
    CHECK_EQ(autonext::nextIndex(q, (int) q.size(), &info, false), -1);
}

TEST(autonext_never_for_movies_live_or_unknown_items) {
    SeriesInfo info = catalog();
    // 4. movies
    std::vector<VodItem> movies(2);
    movies[0].type = movies[1].type = ContentType::Movie;
    movies[0].id = "m1";
    movies[1].id = "m2";
    CHECK_EQ(autonext::nextIndex(movies, 0, nullptr, false), -1);
    CHECK_EQ(autonext::nextIndex(movies, 0, &info, false), -1);
    // 5. live channels (and M3U entries, which are live)
    std::vector<VodItem> live(2);
    live[0].type = live[1].type = ContentType::Live;
    live[0].id = "1";
    live[1].id = "2";
    CHECK_EQ(autonext::nextIndex(live, 0, nullptr, false), -1);
    // an "episode" that is not a known series episode (no series id), or another series in the queue
    std::vector<VodItem> q = queueOf(info);
    std::vector<VodItem> anon = q;
    anon[0].seriesId = "";
    CHECK_EQ(autonext::nextIndex(anon, 0, &info, false), -1);
    std::vector<VodItem> mixed = q;
    mixed[1].seriesId = "other";
    CHECK_EQ(autonext::nextIndex(mixed, 0, &info, false), -1);
}

TEST(autonext_without_catalog_only_an_obvious_successor) {
    SeriesInfo info = catalog();
    std::vector<VodItem> q = queueOf(info);
    CHECK_EQ(nextId(q, "201", nullptr, false), std::string("202"));    // E01 -> E02
    CHECK_EQ(nextId(q, "102", nullptr, false), std::string("201"));    // S01 end -> S02E01
    CHECK_EQ(nextId(q, "202", nullptr, false), std::string(""));       // E02 -> E05: a gap, maybe not downloaded
    CHECK_EQ(nextId(q, "205", nullptr, false), std::string(""));       // S03 starts at E02: unclear
    CHECK_EQ(nextId(q, "303", nullptr, false), std::string(""));       // last in the queue
}

TEST(autonext_offline_needs_the_downloaded_next_episode) {
    SeriesInfo info = catalog();
    std::vector<VodItem> q = queueOf(info);
    q[(size_t) at(q, "202")].localPath = "/data/PS4IPTV/downloads/202.mkv";
    // 14. offline, the next episode is downloaded: allowed
    CHECK_EQ(nextId(q, "201", &info, true), std::string("202"));
    CHECK_EQ(nextId(q, "201", nullptr, true), std::string("202"));
    // 15. offline, the next episode is not downloaded: no countdown (never a stream while offline)
    CHECK_EQ(nextId(q, "202", &info, true), std::string(""));
    CHECK_EQ(nextId(q, "102", &info, true), std::string(""));
    // online the same episode streams
    CHECK_EQ(nextId(q, "202", &info, false), std::string("205"));
    // the Downloads screen's queue (downloaded episodes only): E01, E05 - the catalog knows E02 comes first
    std::vector<VodItem> dl = {q[(size_t) at(q, "201")], q[(size_t) at(q, "205")]};
    dl[0].localPath = "/d/201.mkv";
    dl[1].localPath = "/d/205.mkv";
    CHECK_EQ(autonext::nextIndex(dl, 0, &info, true), -1);
    CHECK_EQ(autonext::nextIndex(dl, 0, nullptr, true), -1);    // and without the catalog the gap is not crossed
}

namespace {
    // 7 6 5 4 3 2 1
    std::vector<int> fullSequence() {
        std::vector<int> v;
        for (int i = (int) autonext::CARD_SECONDS; i >= 1; i--) {
            v.push_back(i);
        }
        return v;
    }
}

// the card appears with 45 s of the episode left and decides in 7 s (two separate numbers, never a 45 s count)
TEST(autonext_card_appears_at_45_seconds_and_counts_7) {
    CHECK(autonext::CARD_SHOW_REMAINING == 45 && autonext::CARD_SECONDS == 7 && autonext::RESET_MARGIN == 1);
    CHECK(progress::COMPLETION_REMAINING == 45);          // the same moment as the Continue Watching completion
    // REGRESSION: not before 45 s left, at ~45 s left it appears, and its 7 s count starts at once
    Countdown c;
    CHECK(!c.update(frame(120)) && !c.update(frame(60)) && !c.update(frame(45.01)) && !c.visible());
    CHECK(c.phase() == Phase::Idle);
    CHECK(!c.update(frame(45.0)) && c.visible() && c.counting() && c.seconds() == 7 && c.fraction() == 0);
    clockNow += 1.0 / 60;
    c.update(frame(45.0 - 1.0 / 60));
    CHECK(c.fraction() > 0 && c.seconds() == 7);          // counting from the first frame
    // auto-next after 7 s: the next episode starts ~38 s before the end, not at the end
    Countdown d;
    std::vector<int> shown;
    CHECK_EQ(playOut(d, 50.0, shown), 1);
    CHECK(shown == fullSequence());                        // 7 6 5 4 3 2 1, never a 45 .. 1 count
    for (int n: shown) {
        CHECK(n >= 1 && n <= 7);
    }
    CHECK(d.phase() == Phase::Transitioning && !d.visible());
    // after the transition nothing triggers again: not the end, not EOF, not Play now
    CHECK(!d.update(frame(0)) && !d.endOfFile(true, true, clockNow) && !d.playNow() && !d.cancel());
    // reset() for the next item: appears and counts again
    d.reset();
    shown.clear();
    CHECK_EQ(playOut(d, 46.0, shown), 1);
    CHECK(shown.front() == 7 && shown.back() == 1);
}

TEST(autonext_card_counts_its_own_seconds) {
    // the 7 s count runs on its own clock: the position (45 s .. 38 s left) does not drive it
    Countdown c;
    clockNow = 500;
    CHECK(!c.update(frame(45)) && c.visible());
    CHECK(!hold(c, 6.9, 41.0));
    CHECK(c.visible() && c.seconds() == 1);
    CHECK(hold(c, 0.2, 41.0) && c.phase() == Phase::Transitioning);
    CHECK(std::fabs(clockNow - 507) < 0.05);
    // the bar fills smoothly: half way after 3.5 s
    Countdown h;
    h.update(frame(44.9));
    hold(h, 3.5, 41.4);
    CHECK(std::fabs(h.fraction() - 0.5f) < 2e-3f && h.seconds() == 4);
    // a hitch (one 2 s frame) advances the card by at most MAX_STEP: no skipped countdown
    clockNow += 2;
    h.update(frame(39.4));
    CHECK(h.visible() && std::fabs(h.fraction() - (3.75f / 7)) < 2e-3f);
    // a seek forward into the last seconds shows the card there, with its full 7 s
    Countdown f;
    f.update(frame(3.0));
    CHECK(f.visible() && f.seconds() == 7);
    // the end of file while counting: the card stays after the end and finishes its 7 s
    Countdown e;
    e.update(frame(2.0));
    hold(e, 2.0, 0.1);
    CHECK(e.endOfFile(true, true, clockNow) && e.phase() == Phase::PostPlay && e.visible() && e.seconds() == 5);
    CHECK(!hold(e, 4.9, -1) && e.visible());
    CHECK(hold(e, 0.2, -1) && e.phase() == Phase::Transitioning);
}

TEST(autonext_pause_holds_the_card) {
    Countdown c;
    c.update(frame(44));
    hold(c, 2, 42);
    CHECK(c.seconds() == 5);
    // paused for a minute: the card stays, the count waits
    CHECK(!hold(c, 60, 42, true, true));
    CHECK(c.visible() && c.seconds() == 5);
    // buffering / the Options panel: hidden, the count waits; shown again afterwards at the same number
    CHECK(!hold(c, 10, 42, false));
    CHECK(!c.visible() && c.phase() == Phase::Counting);
    clockNow += 1.0 / 60;
    c.update(frame(42));
    CHECK(c.visible() && c.seconds() == 5);
}

TEST(autonext_play_now_and_watch_credits) {
    // REGRESSION Play now: at once (right after the card appeared, 45 s before the end), once
    Countdown c;
    CHECK(!c.playNow() && !c.cancel());              // not shown: Cross / Circle keep their normal meaning
    c.update(frame(45));
    CHECK(c.visible() && c.seconds() == 7);
    CHECK(c.playNow());
    CHECK(!c.playNow() && !c.update(frame(44)) && !c.endOfFile(true, true, clockNow));
    // REGRESSION Watch credits: dismissed, auto-next off for this playback, the episode plays on to its end
    Countdown k;
    k.update(frame(44));
    hold(k, 3, 41);
    CHECK(k.cancel() && !k.visible() && k.phase() == Phase::Cancelled);
    std::vector<int> shown;
    CHECK_EQ(playOut(k, 41.0, shown), 0);           // the remaining 41 s of credits play, no transition
    CHECK(shown.empty());
    CHECK(!k.endOfFile(true, true, clockNow));        // EOF: the Up next panel, no card, no auto-next
    CHECK(!k.playNow() && !k.cancel());               // Circle means Back again
    // it does not come back in this playback, even after seeking back and reaching the last 45 s again
    k.update(frame(600));
    CHECK_EQ(playOut(k, 50.0, shown), 0);
    CHECK(shown.empty() && k.phase() == Phase::Cancelled);
    // the next item starts fresh (Watch credits is not a setting)
    k.reset();
    CHECK_EQ(playOut(k, 46.0, shown), 1);
    // hidden (buffering): neither Play now nor Watch credits act (the keys mean their usual thing)
    Countdown hidden;
    hidden.update(frame(40));
    hidden.update(frame(40, true, false));
    CHECK(!hidden.playNow() && !hidden.cancel());
    // after the end: Play now and Cancel act on the post-play card
    Countdown post;
    CHECK(post.endOfFile(true, true, clockNow) && post.playNow() && post.phase() == Phase::Transitioning);
    Countdown post2;
    CHECK(post2.endOfFile(true, true, clockNow) && post2.cancel() && !post2.visible());
    CHECK(!hold(post2, 10, -1));                      // cancelled: never starts by itself
}

TEST(autonext_seek_back_resets_the_card) {
    Countdown c;
    c.update(frame(44.0));
    CHECK(c.visible());
    clockNow += 1.0 / 60;
    c.update(frame(45.8));                            // a small step back: stays (hysteresis up to 46 s)
    CHECK(c.visible());
    clockNow += 1.0 / 60;
    c.update(frame(46.2));                            // beyond 46 s: reset
    CHECK(!c.visible() && c.phase() == Phase::Idle && c.seconds() == 0 && c.fraction() == 0);
    c.update(frame(30.0));
    CHECK(c.visible() && c.seconds() == 7);           // counts its full 7 s again
    c.update(frame(95.0));                            // a minute back
    CHECK(!c.visible() && c.phase() == Phase::Idle);
    std::vector<int> shown;
    CHECK_EQ(playOut(c, 50.0, shown), 1);
    CHECK(shown == fullSequence());
}

// REGRESSION EOF fallback (the v7-ux hardware bug): the reported duration is longer than the media, so mpv's end of
// file comes while more than the card's 45 s "remain" (or the duration is unknown) - the card is shown after the end
TEST(autonext_regression_end_of_file_before_the_card_window) {
    Countdown c;
    std::vector<int> shown;
    // time-pos stops 52 s before a reported duration that is too long
    CHECK_EQ(playOut(c, 120.0, shown, 52.0), 0);
    CHECK(shown.empty() && c.phase() == Phase::Idle);
    // the end of file: the card is shown after the end, with its full 7 s countdown
    CHECK(c.endOfFile(true, true, clockNow));
    CHECK(c.visible() && c.afterEnd() && c.counting() && c.seconds() == 7);
    CHECK(!hold(c, 3.5, -1) && std::fabs(c.fraction() - 0.5f) < 2e-3f && c.seconds() == 4);
    CHECK(!hold(c, 3.4, -1) && c.visible());
    CHECK(hold(c, 0.2, -1) && c.phase() == Phase::Transitioning);   // exactly once, after 7 s
    CHECK(!c.endOfFile(true, true, clockNow) && !c.update(frame(-1)) && !c.playNow());
    // an overshoot of only a few seconds: the card appeared at 45 s left and decided long before the end
    Countdown small;
    shown.clear();
    CHECK_EQ(playOut(small, 60.0, shown, 8.2), 1);
    CHECK(shown == fullSequence());
}

TEST(autonext_regression_unknown_or_inaccurate_duration) {
    // no duration at all (some streams): never before the end, always after it
    Countdown none;
    for (int i = 0; i < 600; i++) {
        clockNow += 1.0 / 60;
        CHECK(!none.update(frame(-1)));
    }
    CHECK(!none.visible() && none.endOfFile(true, true, clockNow) && none.visible());
    CHECK(hold(none, 7.1, -1));
    // NaN / infinite times are unknown, never a trigger
    Countdown n;
    CHECK(!n.update(frame(std::nan(""))) && !n.update(frame(INFINITY)) && !n.visible());
    CHECK(n.endOfFile(true, true, clockNow));
    // a duration shorter than the media (the position runs past it): remaining clamps to 0, the card counts its 7 s
    Countdown shortDur;
    shortDur.update(frame(0));
    CHECK(shortDur.visible() && !hold(shortDur, 6.5, 0) && hold(shortDur, 0.6, 0));
    // a playback error never advances: hidden while not running, and an error is never an end of file
    Countdown e;
    e.update(frame(30.0));
    CHECK(e.visible());
    CHECK(!hold(e, 60, 30.0, false));
    CHECK(!e.visible() && e.phase() == Phase::Counting);
}

TEST(autonext_regression_auto_play_off_shows_a_manual_card) {
    // REGRESSION Auto-play OFF: the same card at 45 s left, no countdown, no automatic transition; Play now works
    Countdown c;
    for (double r = 90; r > 45.05; r -= 0.1) {
        clockNow += 0.1;
        CHECK(!c.update(frame(r, true, true, false, false)));
    }
    CHECK(!c.visible());
    c.update(frame(45.0, true, true, false, false));
    CHECK(c.visible() && !c.counting() && c.seconds() == 0 && c.fraction() == 0);
    CHECK(!hold(c, 44, 3.0, true, false, false) && c.visible());   // never starts by itself
    CHECK(c.endOfFile(true, false, clockNow) && c.afterEnd() && c.visible() && !c.counting());
    CHECK(!hold(c, 60, -1, true, false, false) && c.visible());     // after the end: still waiting
    CHECK(c.playNow() && c.phase() == Phase::Transitioning);
    // the end of file first (no card before it): the manual card after the end
    Countdown d;
    CHECK(d.endOfFile(true, false, clockNow) && d.visible() && !d.counting());
    CHECK(!hold(d, 30, -1, true, false, false));
    CHECK(d.cancel() && !d.visible());
    // Watch credits on the manual card, then the end: the Up next panel (no card)
    Countdown w;
    w.update(frame(40, true, true, false, false));
    CHECK(w.cancel() && !w.endOfFile(true, false, clockNow));
}

TEST(autonext_regression_no_card_without_a_next_episode) {
    // the series' final episode, or offline without the next episode downloaded (nextIndex == -1): nothing, before
    // or after the end
    Countdown c;
    for (double r = 30; r >= 0; r -= 0.1) {
        clockNow += 0.1;
        CHECK(!c.update(frame(r, false)));
    }
    CHECK(!c.visible() && !c.endOfFile(false, true, clockNow) && !c.visible());
    CHECK(!c.endOfFile(false, false, clockNow));
    // the next episode disappears while the card is shown (e.g. a queue change): hidden at once
    Countdown d;
    d.update(frame(3));
    CHECK(d.visible() && !d.update(frame(2.9, false)) && !d.visible());
    // the reasons the log gives
    SeriesInfo info = catalog();
    std::vector<VodItem> q = queueOf(info);
    CHECK_EQ(std::string(autonext::noNextReason(q, at(q, "303"), &info, false)), std::string("the series' final episode"));
    CHECK_EQ(std::string(autonext::noNextReason(q, at(q, "202"), &info, true)),
             std::string("offline and the next episode is not downloaded"));
    CHECK_EQ(std::string(autonext::noNextReason(q, at(q, "201"), &info, false)), std::string(""));
    std::vector<VodItem> alone = {q[(size_t) at(q, "201")]};
    CHECK_EQ(std::string(autonext::noNextReason(alone, 0, nullptr, false)),
             std::string("series catalog not in memory, no obvious successor in the queue"));
}

TEST(autonext_setting_off_and_migration) {
    // 16. the setting: fresh install ON; a v0.2.0 file (old key, default false, no new key) ON; the new key wins
    SettingsStore fresh("unused");
    CHECK(fresh.get().autoPlayNextEpisode);
    SettingsStore old("unused");
    CHECK(old.deserialize("{\"version\":2,\"resumeVod\":true,\"autoPlayNextEpisode\":false,\"language\":\"tr\"}",
                          nullptr));
    CHECK(old.get().autoPlayNextEpisode && old.get().resumeVod && old.get().language == "tr");
    SettingsStore none("unused");
    CHECK(none.deserialize("{}", nullptr) && none.get().autoPlayNextEpisode);
    SettingsStore off("unused");
    CHECK(off.deserialize("{\"version\":2,\"autoPlayNext\":false,\"autoPlayNextEpisode\":true}", nullptr));
    CHECK(!off.get().autoPlayNextEpisode);
    // round trip: OFF stays OFF; both keys are written (older builds read the old one)
    SettingsStore back("unused");
    CHECK(back.deserialize(off.serialize(), nullptr) && !back.get().autoPlayNextEpisode);
    CHECK(off.serialize().find("\"autoPlayNext\": false") != std::string::npos);
    CHECK(off.serialize().find("\"autoPlayNextEpisode\": false") != std::string::npos);
    off.get().autoPlayNextEpisode = true;
    SettingsStore on("unused");
    CHECK(on.deserialize(off.serialize(), nullptr) && on.get().autoPlayNextEpisode);
}

namespace {
    // what Home builds from a Continue Watching entry (HomeScreen::itemOf): ids, names, numbers, no neighbours
    VodItem continueWatching(const std::string &id, int season, int episode) {
        VodItem it;
        it.type = ContentType::Series;
        it.id = id;
        it.seriesId = "77";
        it.seriesName = "The Series";
        it.title = "Title " + id;
        it.image = "https://img.example/cover.jpg";
        it.extension = "mp4";
        it.season = season;
        it.episode = episode;
        it.durationHint = 3420;
        return it;
    }

    VodItem downloadOf(const VodItem &it) {
        VodItem d = it;
        d.localPath = "/data/PS4IPTV/downloads/" + it.id + ".mkv";
        d.profileId = "p1";
        return d;
    }
}

TEST(autonext_continue_watching_gets_the_series_detail_context) {
    SeriesInfo info = catalog();
    std::vector<VodItem> seriesDetail = queueOf(info);
    // 1. a middle episode resumed from Continue Watching: the whole series in catalog order, like Series Detail
    VodItem cw = continueWatching("202", 2, 2);
    int pos = -1;
    std::vector<VodItem> q = autonext::episodeQueue(cw, &info, {}, false, pos);
    CHECK_EQ(q.size(), seriesDetail.size());
    for (size_t i = 0; i < q.size(); i++) {
        CHECK(q[i].id == seriesDetail[i].id && q[i].season == seriesDetail[i].season);
        CHECK(q[i].seriesId == "77" && q[i].seriesName == "The Series" && q[i].type == ContentType::Series);
    }
    CHECK(pos == at(q, "202"));
    // the current episode exactly as given (its container, duration, image)
    CHECK(q[(size_t) pos].extension == "mp4" && q[(size_t) pos].title == "Title 202" && q[(size_t) pos].durationHint == 3420);
    CHECK_EQ(q[(size_t) autonext::nextIndex(q, pos, &info, false)].id, std::string("205"));
    // 8 / 9. the end screen's "Up next" (auto-play off / cancelled) has the next episode too
    CHECK(pos + 1 < (int) q.size() && q[(size_t) pos + 1].id == "205");
    // 6. season boundary: the last S02 episode -> S03's first one
    std::vector<VodItem> q2 = autonext::episodeQueue(continueWatching("205", 2, 5), &info, {}, false, pos);
    CHECK_EQ(q2[(size_t) autonext::nextIndex(q2, pos, &info, false)].id, std::string("302"));
    // 7. the final episode of the final season: none
    std::vector<VodItem> q3 = autonext::episodeQueue(continueWatching("303", 3, 3), &info, {}, false, pos);
    CHECK_EQ(autonext::nextIndex(q3, pos, &info, false), -1);
    CHECK(pos == (int) q3.size() - 1);
    // the v3.2 bug: without the catalog the queue was this episode alone - no countdown, "last episode" at the end
    std::vector<VodItem> alone = autonext::episodeQueue(cw, nullptr, {}, false, pos);
    CHECK(alone.size() == 1 && pos == 0 && autonext::nextIndex(alone, 0, nullptr, false) == -1);
    // ... the player asks for the catalog once and rebuilds the same queue around the playing episode
    std::vector<VodItem> late = autonext::episodeQueue(alone[0], &info, {}, false, pos);
    CHECK(late.size() == seriesDetail.size() && late[(size_t) pos].id == "202");
    CHECK_EQ(late[(size_t) autonext::nextIndex(late, pos, &info, false)].id, std::string("205"));
    // an episode the catalog no longer lists: played alone, no next (no guessing)
    std::vector<VodItem> gone = autonext::episodeQueue(continueWatching("999", 2, 9), &info, {}, false, pos);
    CHECK(gone.size() == 1 && autonext::nextIndex(gone, pos, &info, false) == -1);
    // 12. Series Detail builds the same queue for the same episode
    VodItem fromDetail = seriesDetail[(size_t) at(seriesDetail, "201")];
    std::vector<VodItem> qd = autonext::episodeQueue(fromDetail, &info, {}, false, pos);
    CHECK(qd.size() == seriesDetail.size() && qd[(size_t) autonext::nextIndex(qd, pos, &info, false)].id == "202");
}

TEST(autonext_continue_watching_keeps_the_resume_position) {
    // 5. the episode's saved position is found by its id in the shared queue: the resume start is unchanged
    LibraryStore lib("unused");
    VodItem cw = continueWatching("202", 2, 2);
    {
        VodProgress p(lib);
        double t = 1000;
        p.begin(cw, false, t);
        for (double pos = 0; pos < 1500; pos += 0.25) {
            t += 0.25;
            p.observe(pos, 3420, true, t);
            if (p.due(t) != VodProgress::Due::None) {
                p.record(t);
            }
        }
        p.record(t + 0.1, 1500.5);
    }
    SeriesInfo info = catalog();
    int pos = -1;
    std::vector<VodItem> q = autonext::episodeQueue(cw, &info, {}, false, pos);
    VodProgress resume(lib);
    CHECK(std::fabs(resume.begin(q[(size_t) pos], true, 5000) - 1500.5) < 0.01);
    // the neighbours start from the beginning (nothing saved for them)
    VodProgress fresh(lib);
    CHECK(fresh.begin(q[(size_t) pos + 1], true, 5000) == 0);
}

TEST(autonext_continue_watching_offline_uses_the_downloads) {
    SeriesInfo info = catalog();
    std::vector<VodItem> all = queueOf(info);
    VodItem e201 = downloadOf(all[(size_t) at(all, "201")]);
    VodItem e202 = downloadOf(all[(size_t) at(all, "202")]);
    VodItem e205 = downloadOf(all[(size_t) at(all, "205")]);
    VodItem cw = downloadOf(continueWatching("201", 2, 1));   // Home: the downloaded copy preferred
    int pos = -1;
    // 10. offline, the next episode is downloaded: the downloads are the queue, the countdown is allowed
    std::vector<VodItem> q = autonext::episodeQueue(cw, &info, {e201, e202, e205}, true, pos);
    CHECK(q.size() == 3 && q[(size_t) pos].id == "201" && q[(size_t) pos].extension == "mp4");
    int n = autonext::nextIndex(q, pos, &info, true);
    CHECK(n >= 0 && q[(size_t) n].id == "202" && !q[(size_t) n].localPath.empty());
    // 11. offline, the next episode (202) is not downloaded: no countdown (205 is not the next one)
    std::vector<VodItem> gap = autonext::episodeQueue(cw, &info, {e201, e205}, true, pos);
    CHECK_EQ(autonext::nextIndex(gap, pos, &info, true), -1);
    CHECK_EQ(autonext::nextIndex(gap, pos, nullptr, true), -1);
    // offline the catalog never makes the queue (no stream offline), even when it is in memory
    CHECK_EQ(autonext::episodeQueue(cw, &info, {}, true, pos).size(), 1u);
    // the Continue Watching item without its local path still plays the downloaded copy
    VodItem plain = continueWatching("201", 2, 1);
    std::vector<VodItem> q2 = autonext::episodeQueue(plain, nullptr, {e201, e202}, true, pos);
    CHECK(!q2[(size_t) pos].localPath.empty() && q2[(size_t) pos].profileId == "p1");
    // online with the catalog: downloaded copies are kept for the current episode
    std::vector<VodItem> on = autonext::episodeQueue(cw, &info, {e201, e202}, false, pos);
    CHECK(on.size() == all.size() && on[(size_t) pos].localPath == cw.localPath);
}

TEST(autonext_movies_and_live_get_no_episode_context) {
    // 14 / 15. a movie or a channel: no series queue, no next episode
    SeriesInfo info = catalog();
    VodItem movie;
    movie.type = ContentType::Movie;
    movie.id = "202";                                  // even with an id the catalog happens to contain
    int pos = -1;
    std::vector<VodItem> q = autonext::episodeQueue(movie, &info, {}, false, pos);
    CHECK(q.size() == 1 && pos == 0 && autonext::nextIndex(q, 0, &info, false) == -1);
    VodItem channel;
    channel.type = ContentType::Live;
    channel.id = "201";
    std::vector<VodItem> l = autonext::episodeQueue(channel, &info, {}, false, pos);
    CHECK(l.size() == 1 && autonext::nextIndex(l, 0, &info, false) == -1);
}

