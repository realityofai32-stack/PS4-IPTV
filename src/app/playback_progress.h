// The one place a movie / episode playback reaches the library (host-testable; VodPlayerScreen owns one and feeds
// it every frame and at every way the playback of an item ends).
//
// It wraps the position bookkeeping (app/vod_progress.h), the 45 s Continue Watching completion (app/auto_next.h
// CompletionLatch) and the series' progression (autonext::persistUpNext): every record of the playing item goes
// through record() below, and whenever a record leaves an episode complete for Continue Watching - the latch, the
// end of file, an automatic or manual next episode, or simply a final position in its last 45 s (Circle / Stop,
// pause, app exit, episode change, a player error) - the series' next episode becomes its Continue Watching entry in
// the same step. An episode is never stored as complete without its progression, whichever path ends it.
//
// Before (v0.4.0 and the first v0.5.0 package) only the latch, the end of file and the transition to the next episode persisted the
// progression, while VodProgress::record() marked an episode watched on its own when its final position was in the
// last 45 s: an exit whose position was newer than the latch's last 0.25 s sample (Circle / Stop / pause right after
// the threshold, a seek into the credits followed by Circle, a paused player whose sample never caught up) stored
// the episode as watched with no next episode, and the series left Continue Watching.

#ifndef PS4IPTV_APP_PLAYBACK_PROGRESS_H
#define PS4IPTV_APP_PLAYBACK_PROGRESS_H

#include <memory>
#include <vector>

#include "auto_next.h"
#include "vod_progress.h"

class PlaybackProgress {

public:

    // unixNow: wall clock for watchedAt / the up-next entry (optional)
    explicit PlaybackProgress(LibraryStore &store, int64_t (*unixNow)() = nullptr);

    // what the item's series context is: the player's queue, the playing index, its next episode (autonext::nextIndex,
    // -1 = none or not known) and the series' catalog when it is in memory. Set when an item starts and whenever the
    // queue is completed; a next episode that becomes known after the item was complete is committed at once.
    void setContext(const std::vector<screens::VodItem> &queue, int index, int next,
                    std::shared_ptr<const iptv::SeriesInfo> info);

    // a new item starts: exactly the saved position when it is in progress and resume is wanted, else 0
    double begin(const screens::VodItem &item, bool resumeWanted, double now);

    // ---------------------------------------------------------------- every frame (VodProgress)
    void observe(double position, double duration, bool started, double now) {
        tracker.observe(position, duration, started, now);
    }

    void seekPending(double target) { tracker.seekPending(target); }

    void seekCommitted(double target, double now) { tracker.seekCommitted(target, now); }

    VodProgress::Due due(double now) const { return tracker.due(now); }

    double position() const { return tracker.position(); }

    double duration() const { return tracker.duration(); }

    bool started() const { return tracker.started(); }

    const screens::VodItem &item() const { return tracker.item(); }

    // ---------------------------------------------------------------- the Continue Watching completion
    struct Update {
        autonext::CompletionLatch::Change change = autonext::CompletionLatch::Change::None;
        bool committed = false;                   // the series' progression was persisted in this call
        autonext::UpNext upNext = autonext::UpNext::None;
    };

    // every frame while the item plays (not after its end): the 45 s latch at `duration` (the known duration, else
    // the player's), and a next episode that became known after the item was complete
    Update update(double now, double duration);

    // ---------------------------------------------------------------- records (every way playback ends)
    // finalPosition >= 0: the player's position queried just now (stop / pause / exit); atEnd: the item was
    // completed (end of file, transition to the next episode). Records the item and commits the series' progression
    // when the record leaves an episode complete. false: nothing worth remembering (VodProgress::record).
    bool record(double now, double finalPosition = -1, bool atEnd = false);

    // the result of the last progression commit of this item (for the log); None while nothing was committed
    autonext::UpNext lastUpNext() const { return upNextResult; }

    // the series' progression was persisted for this item
    bool progressionCommitted() const { return upNextDone; }

    bool latched() const { return latch.latched(); }

    bool isCompleted() const { return tracker.isCompleted(); }

private:

    bool episodeComplete() const;

    // persists the series' next episode once per item when the item is complete and its next episode is known
    bool commit();

    LibraryStore &store;
    int64_t (*wallClock)();
    VodProgress tracker;
    autonext::CompletionLatch latch;
    std::vector<screens::VodItem> queue;
    int index = -1;
    int next = -1;
    std::shared_ptr<const iptv::SeriesInfo> info;
    bool upNextDone = false;
    bool recordedComplete = false;   // the last record of this playback stored the episode as complete
    autonext::UpNext upNextResult = autonext::UpNext::None;
};

#endif // PS4IPTV_APP_PLAYBACK_PROGRESS_H
