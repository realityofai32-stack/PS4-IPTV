// Playback position bookkeeping of one movie / episode (host-testable; the VOD player screen feeds it).
//
//   begin()    where playback starts: exactly the saved position (no seconds subtracted) when the item is
//              in progress and resume is wanted, else 0
//   observe()  every frame: the position mpv reports once the first frame is shown
//   record()   writes the entry to the progress store. On stop / Circle / app exit the caller passes the
//              position queried from mpv right before stopping, so the last seconds since the periodic
//              sample are never lost. During a seek the requested target is the position.
//   due()      periodic updates: the store every MEMORY_EVERY seconds, history.json every DISK_EVERY

#ifndef PS4IPTV_APP_VOD_PROGRESS_H
#define PS4IPTV_APP_VOD_PROGRESS_H

#include "screens.h"
#include "../storage/library_store.h"

class VodProgress {

public:

    static constexpr double MEMORY_EVERY = 5;
    static constexpr double DISK_EVERY = 15;
    static constexpr double SEEK_SETTLE = 3;      // seconds a committed seek may take to show in time-pos

    // unixNow: wall clock for HistoryEntry::watchedAt (optional)
    explicit VodProgress(LibraryStore &store, int64_t (*unixNow)() = nullptr) : store(store), wallClock(unixNow) {}

    // resumeWanted: the user chose Resume / Continue Watching and resume is enabled in Settings
    double begin(const screens::VodItem &item, bool resumeWanted, double now);

    void observe(double position, double duration, bool started, double now);

    // a seek to `target` is pending (accumulating presses)
    void seekPending(double target);

    // the pending seek was sent to mpv
    void seekCommitted(double target, double now);

    enum class Due {
        None,
        Memory,   // update the store
        Disk      // update the store and write history.json
    };

    Due due(double now) const;

    // Records the current position. finalPosition >= 0: the player's position queried just now (stop /
    // pause / exit). atEnd: the item was completed. Returns false when there is nothing worth remembering
    // (playback never got past its first second); existing progress is then left untouched.
    bool record(double now, double finalPosition = -1, bool atEnd = false);

    // what the user would resume from right now
    double position() const;

    double duration() const { return knownDuration; }

    double startPosition() const { return start; }

    const screens::VodItem &item() const { return current; }

    bool started() const { return hadFrame; }

private:

    LibraryStore &store;
    int64_t (*wallClock)();
    screens::VodItem current;
    double start = 0;
    double lastPosition = 0;
    double knownDuration = 0;
    bool hadFrame = false;
    double pendingTarget = -1;
    double settleTarget = -1;
    double settleUntil = 0;
    double lastMemory = 0;
    double lastDisk = 0;
};

#endif // PS4IPTV_APP_VOD_PROGRESS_H
