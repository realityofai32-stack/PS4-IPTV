// Series episode playback context and the next-episode countdown / auto-play (host-testable, no libcross2d).
//
// The queue (episodeQueue): every way of starting a series episode - Series Detail, Continue Watching / the Home
// hero, Downloads - builds the player's queue here (through screens::playSeriesEpisode), so the player always
// knows the series, the episode and its neighbours the same way:
//   - online, the series' catalog in memory and listing the episode: every episode in catalog order
//   - otherwise the series' downloaded episodes (download manifest, season / episode order) when they include it
//   - otherwise the episode alone; online, the player then asks for the catalog once and completes its queue
//     when it arrives (VodPlayerScreen)
//   The current episode is kept exactly as the caller gave it (its resume position is looked up by its id; its
//   local file and owner profile stay), at the returned position.
//
// Which episode is next (nextIndex):
//   - only for a known series episode (VodItem type Series); movies, live channels and anything else: none
//   - the provider catalog decides when it is in memory (VodLibrary::seriesInfo): seriesplan::nextEpisode, i.e.
//     the next episode of the season, else the first episode of the next season that has any - numbering is never
//     assumed to be sequential. That episode must be in the player's queue (the queue is what can be played);
//     after the series' last episode there is none
//   - without catalog data (a queue built from the download manifest) only an unambiguous successor counts: the
//     queue's next item with episode number + 1 in the same season, or episode 0 / 1 of the following season
//   - offline (the session could not reach the provider): only a downloaded next episode (VodItem::localPath)
//
// When (Countdown), driven by the player's playback time, never by a wall clock (a pause pauses it):
//   Idle         -> Counting      remaining <= COUNTDOWN_SECONDS (15) while the episode plays normally: 15 .. 1
//   Counting     -> Idle          the user seeked back: remaining > COUNTDOWN_SECONDS + RESET_MARGIN (16), or
//                                 playback stopped running normally (buffering / reconnecting / error, Options open)
//   Counting     -> Cancelled     Circle: no automatic next episode for the rest of this playback
//   Counting     -> Transitioning Cross (Play now), or the position reached the duration (END_TOLERANCE: float
//                                 rounding only - nothing of the episode is cut)
//   Idle/Counting -> Transitioning a normal end of file: the usual end, also for files whose position never reaches
//                                 the reported duration, and when the duration was never known
// Transitioning is final: every trigger after the first returns false, so the next episode starts exactly once.
// The player calls reset() when it starts any item.

#ifndef PS4IPTV_APP_AUTO_NEXT_H
#define PS4IPTV_APP_AUTO_NEXT_H

#include <vector>

#include "screens.h"
#include "../iptv/models.h"

namespace autonext {

    constexpr double COUNTDOWN_SECONDS = 15;
    constexpr double RESET_MARGIN = 1;       // a seek back must leave this much more than the countdown to reset it
    constexpr double END_TOLERANCE = 0.001;  // remaining seconds that count as "at the end" (rounding only)

    // the player's queue for `current` (see above); position: its index in the result. info: the series' catalog
    // when it is in memory and belongs to the episode's profile, else nullptr. downloaded: the series' completed
    // downloads of that profile (offline::itemFor), in manifest order.
    std::vector<screens::VodItem> episodeQueue(const screens::VodItem &current, const iptv::SeriesInfo *info,
                                               const std::vector<screens::VodItem> &downloaded, bool offline,
                                               int &position);

    // the queue index of the episode to play after queue[index], or -1 (see above). info: the series' catalog
    // when it is in memory, else nullptr.
    int nextIndex(const std::vector<screens::VodItem> &queue, int index, const iptv::SeriesInfo *info, bool offline);

    enum class Phase {
        Idle,
        Counting,
        Cancelled,
        Transitioning
    };

    struct Frame {
        bool canAdvance = false;   // a next episode exists and Auto-play next episode is on
        bool running = false;      // the episode is playing normally (or paused) and no panel has the controls
        double remaining = -1;     // duration - position (the pending seek target when one is pending); < 0 unknown
    };

    class Countdown {
    public:
        void reset() { state = Phase::Idle; }

        Phase phase() const { return state; }

        bool visible() const { return state == Phase::Counting; }

        // the number shown while counting: 15 14 .. 2 1 (never 0)
        int seconds() const { return shownSeconds; }

        // remaining / COUNTDOWN_SECONDS while counting (the panel's thin bar), 0 otherwise
        float fraction() const { return shownFraction; }

        // once per frame; true = start the next episode now (returned once)
        bool update(const Frame &frame);

        // Cross while the countdown is shown; true = start the next episode now
        bool playNow();

        // Circle while the countdown is shown; true = consumed (the countdown is gone for this playback)
        bool cancel();

        // a normal end of file (never a playback error); true = start the next episode now
        bool endOfFile(bool canAdvance);

    private:
        Phase state = Phase::Idle;
        int shownSeconds = 0;
        float shownFraction = 0;
    };
}

#endif // PS4IPTV_APP_AUTO_NEXT_H
