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
// Two separate moments at the end of an episode:
//
// 1. Continue Watching completion (CompletionLatch, progress::COMPLETION_REMAINING = 45 s): once 45 s or less of the
//    episode remain (and it played at least MIN_CONTINUE_SECONDS) it is latched as completed for this playback:
//    VodProgress records it as watched from then on, and the next episode (nextIndex) becomes the series' Continue
//    Watching entry at position 0 (LibraryStore::markUpNext) at once - not only at the end of file. Exiting, EOF,
//    Auto-play off, "Watch credits" and cancelling all keep it. Seeking back beyond 46 s left releases the latch
//    (the episode is in progress again). advancesContinue() keeps the series' pointer from moving backwards when an
//    older episode is replayed. Without a valid next episode (final episode, offline and not downloaded, unknown)
//    nothing is created: the existing completion behaviour applies.
//
// 2. The visible Next Episode card (Countdown, CARD_SECONDS = 7 s): it appears when ~7 s remain and counts its own
//    7 seconds of playback (a pause pauses it; buffering / the Options panel hide it and hold the count):
//      Idle          -> Counting       remaining <= CARD_SECONDS while the episode plays normally
//      Counting      -> Idle           seeked back: remaining > CARD_SECONDS + RESET_MARGIN, or no next episode /
//                                      Auto-play turned off
//      Counting      -> Cancelled      Watch Credits (Circle / Esc): no automatic next episode for this playback
//      Counting      -> Transitioning  Play Now (Cross / Enter); the card's 7 s ran out (no wait for the end of
//                                      file); or the position reached the duration
//      Idle/Counting -> Transitioning  a normal end of file (also when the duration was never known)
// Transitioning is final: every trigger after the first returns false, so the next episode starts exactly once.
// The player calls reset() when it starts any item.

#ifndef PS4IPTV_APP_AUTO_NEXT_H
#define PS4IPTV_APP_AUTO_NEXT_H

#include <vector>

#include "screens.h"
#include "../iptv/models.h"
#include "../storage/library_store.h"

namespace autonext {

    constexpr double CARD_SECONDS = 7;       // the Next Episode card: shown with ~7 s left, counts 7 .. 1
    constexpr double RESET_MARGIN = 1;       // a seek back must leave this much more than a window to reset it
    constexpr double END_TOLERANCE = 0.001;  // remaining seconds that count as "at the end" (rounding only)
    constexpr double MAX_STEP = 0.25;        // the card's clock never jumps more than this per frame (a hitch)

    // the player's queue for `current` (see above); position: its index in the result. info: the series' catalog
    // when it is in memory and belongs to the episode's profile, else nullptr. downloaded: the series' completed
    // downloads of that profile (offline::itemFor), in manifest order.
    std::vector<screens::VodItem> episodeQueue(const screens::VodItem &current, const iptv::SeriesInfo *info,
                                               const std::vector<screens::VodItem> &downloaded, bool offline,
                                               int &position);

    // the queue index of the episode to play after queue[index], or -1 (see above). info: the series' catalog
    // when it is in memory, else nullptr.
    int nextIndex(const std::vector<screens::VodItem> &queue, int index, const iptv::SeriesInfo *info, bool offline);

    // ---------------------------------------------------------------- Continue Watching completion (45 s)

    class CompletionLatch {
    public:
        enum class Change {
            None,
            Latched,     // entered the last 45 s: complete for Continue Watching
            Released     // seeked back out of them: in progress again
        };

        void reset() { on = false; }

        bool latched() const { return on; }

        // every frame: position / duration in seconds (< 0 or 0: unknown), started: the first frame was shown
        Change update(double position, double duration, bool started);

    private:
        bool on = false;
    };

    // Should Continue Watching move to queue[next] now? false when the series' current Continue Watching entry
    // (current, nullptr = none; the playing episode excluded) is that episode already in progress (its position is
    // kept), or an episode after it in the provider's order (an older episode was replayed: never backwards).
    // Order: the queue (catalog order), else the catalog, else season / episode numbers.
    bool advancesContinue(const std::vector<screens::VodItem> &queue, int next, const iptv::SeriesInfo *info,
                          const HistoryEntry *current);

    // the progress entry that marks queue[next] as the series' next episode (LibraryStore::markUpNext)
    HistoryEntry upNextEntry(const screens::VodItem &next, int64_t watchedAt);

    enum class UpNext {
        None,    // no valid next episode: nothing created (the series' completion behaviour applies)
        Kept,    // the series' entry is that episode in progress, or a later one: unchanged
        Moved    // queue[next] is the series' Continue Watching entry now (position 0)
    };

    // queue[index] is complete for Continue Watching: makes queue[next] the series' entry when advancesContinue()
    // allows it (the series' current entry is looked up in `store`, the completed episode itself excluded)
    UpNext persistUpNext(LibraryStore &store, const std::vector<screens::VodItem> &queue, int index, int next,
                         const iptv::SeriesInfo *info, int64_t now);

    // ---------------------------------------------------------------- the visible Next Episode card (7 s)

    enum class Phase {
        Idle,
        Counting,
        Cancelled,
        Transitioning
    };

    struct Frame {
        bool canAdvance = false;   // a next episode exists and Auto-play next episode is on
        bool running = false;      // the episode is playing normally (or paused) and no panel has the controls
        bool paused = false;       // paused: the card stays, its count waits
        double remaining = -1;     // duration - position (the pending seek target when one is pending); < 0 unknown
        double now = 0;            // monotonic seconds
    };

    class Countdown {
    public:
        void reset() {
            state = Phase::Idle;
            elapsed = 0;
            lastNow = -1;
            held = false;
        }

        Phase phase() const { return state; }

        // the card is on screen (counting and playback running normally)
        bool visible() const { return state == Phase::Counting && !held; }

        // the number shown while counting: 7 6 .. 2 1 (never 0)
        int seconds() const;

        // how much of the card's 7 s has passed, 0..1 (the thin bar fills), 0 when not counting
        float fraction() const;

        // once per frame; true = start the next episode now (returned once)
        bool update(const Frame &frame);

        // Play Now while the card is shown; true = start the next episode now
        bool playNow();

        // Watch Credits while the card is shown; true = consumed (no automatic next episode for this playback)
        bool cancel();

        // a normal end of file (never a playback error); true = start the next episode now
        bool endOfFile(bool canAdvance);

    private:
        Phase state = Phase::Idle;
        double elapsed = 0;
        double lastNow = -1;
        bool held = false;
    };
}

#endif // PS4IPTV_APP_AUTO_NEXT_H
