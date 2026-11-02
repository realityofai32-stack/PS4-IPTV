// Next-episode countdown / auto-play (app/auto_next.h): which episode is next (catalog order, season boundaries,
// the last episode, movies / live, offline downloads), the countdown state machine (5..1, Play now, Cancel, seeking
// back, end-of-file fallback, exactly one transition, errors) and the Auto-play next episode setting's migration.

#include <cmath>
#include <vector>

#include "check.h"
#include "../../src/app/auto_next.h"
#include "../../src/app/series_plan.h"
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

    Frame frame(double remaining, bool canAdvance = true, bool running = true) {
        Frame f;
        f.canAdvance = canAdvance;
        f.running = running;
        f.remaining = remaining;
        return f;
    }

    // plays from `from` seconds before the end at 60 fps; returns the number of starts, collects the shown numbers
    int playOut(Countdown &c, double from, std::vector<int> &shown, double to = -0.1) {
        int starts = 0;
        for (double r = from; r >= to; r -= 1.0 / 60) {
            if (c.update(frame(std::max(0.0, r)))) {
                starts++;
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

TEST(autonext_countdown_5_4_3_2_1_then_one_transition) {
    // 6. counts 5 4 3 2 1 (never 0), then starts the next episode exactly once
    Countdown c;
    std::vector<int> shown;
    CHECK(!c.update(frame(30)) && !c.visible() && c.phase() == Phase::Idle);
    int starts = playOut(c, 7.0, shown);
    CHECK_EQ(starts, 1);
    CHECK(shown == std::vector<int>({5, 4, 3, 2, 1}));
    CHECK(c.phase() == Phase::Transitioning && !c.visible());
    // the bar shrinks with the time left
    Countdown d;
    d.update(frame(5.0));
    float full = d.fraction();
    d.update(frame(2.5));
    CHECK(std::fabs(full - 1.0f) < 1e-4f && std::fabs(d.fraction() - 0.5f) < 1e-4f && d.seconds() == 3);
    // 12. after the transition nothing triggers again: not the timer, not EOF, not Cross
    CHECK(!c.update(frame(0)) && !c.endOfFile(true) && !c.playNow() && !c.cancel());
    // reset() for the next item: counts again
    c.reset();
    shown.clear();
    CHECK_EQ(playOut(c, 6.0, shown), 1);
    CHECK(shown.front() == 5 && shown.back() == 1);
}

TEST(autonext_play_now_and_cancel) {
    // 7. Cross while shown: at once, once
    Countdown c;
    CHECK(!c.playNow() && !c.cancel());              // not shown: Cross / Circle keep their normal meaning
    c.update(frame(4.2));
    CHECK(c.visible() && c.seconds() == 5);
    CHECK(c.playNow());
    CHECK(!c.playNow() && !c.update(frame(0.1)) && !c.endOfFile(true));
    // 8. Circle: cancelled, the episode plays on to its end without a transition
    Countdown k;
    k.update(frame(3.0));
    CHECK(k.cancel() && !k.visible() && k.phase() == Phase::Cancelled);
    std::vector<int> shown;
    CHECK_EQ(playOut(k, 3.0, shown), 0);
    CHECK(shown.empty());
    CHECK(!k.endOfFile(true));                        // EOF: normal end, no auto-next
    CHECK(!k.playNow() && !k.cancel());               // Circle means Back again
    // 10. a cancelled countdown does not come back in this playback, even after seeking back and reaching the end
    k.update(frame(600));
    CHECK_EQ(playOut(k, 8.0, shown), 0);
    CHECK(shown.empty() && k.phase() == Phase::Cancelled);
    // the next item starts fresh (Cancel is not a setting)
    k.reset();
    CHECK_EQ(playOut(k, 6.0, shown), 1);
}

TEST(autonext_seek_back_resets_the_countdown) {
    // 9. seeking back out of the range hides it; reaching the end again shows it again
    Countdown c;
    c.update(frame(4.0));
    CHECK(c.visible());
    c.update(frame(5.6));                             // jitter / a small step back: stays (hysteresis)
    CHECK(c.visible() && c.seconds() == 5);
    c.update(frame(14.0));                            // Left: 10 s back
    CHECK(!c.visible() && c.phase() == Phase::Idle && c.seconds() == 0);
    std::vector<int> shown;
    CHECK_EQ(playOut(c, 14.0, shown), 1);
    CHECK(shown == std::vector<int>({5, 4, 3, 2, 1}));
    // a seek forward into the range enters it at the right number
    Countdown f;
    f.update(frame(2.4));
    CHECK(f.visible() && f.seconds() == 3);
}

TEST(autonext_end_of_file_fallback_and_errors) {
    // 11. EOF without a countdown (the duration was never known): the same transition, once
    Countdown c;
    CHECK(!c.update(frame(-1)) && !c.visible());
    CHECK(c.endOfFile(true));
    CHECK(!c.endOfFile(true) && !c.update(frame(0)) && !c.playNow());
    // EOF while counting (the last second was never reported): one transition
    Countdown d;
    d.update(frame(1.5));
    CHECK(d.visible() && d.endOfFile(true) && !d.endOfFile(true));
    // 13. a playback error never advances: the countdown hides while playback is not running normally, and an
    // error is never an end of file (the player only calls endOfFile for a genuine end)
    Countdown e;
    e.update(frame(3.0));
    CHECK(e.visible());
    CHECK(!e.update(frame(3.0, true, false)) && !e.visible());
    CHECK(!e.update(frame(0.1, true, false)) && !e.update(frame(0, true, false)));
    CHECK(e.phase() == Phase::Idle);
    // when it recovers near the end the countdown may continue
    CHECK(e.update(frame(0.2)));
    // NaN / infinite times are unknown, never a trigger
    Countdown n;
    CHECK(!n.update(frame(std::nan(""))) && !n.update(frame(INFINITY)) && !n.visible());
}

TEST(autonext_setting_off_and_migration) {
    // 17. Auto-play next episode off: no countdown, EOF ends normally
    Countdown c;
    std::vector<int> shown;
    for (double r = 6; r >= 0; r -= 0.1) {
        CHECK(!c.update(frame(r, false)));
    }
    CHECK(!c.visible() && !c.endOfFile(false));
    // turned off while counting: hidden at once
    Countdown d;
    d.update(frame(3));
    CHECK(d.visible() && !d.update(frame(2.9, false)) && !d.visible());

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
