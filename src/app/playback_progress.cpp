#include "playback_progress.h"

PlaybackProgress::PlaybackProgress(LibraryStore &s, int64_t (*unixNow)())
        : store(s), wallClock(unixNow), tracker(s, unixNow) {
}

void PlaybackProgress::setContext(const std::vector<screens::VodItem> &q, int i, int n,
                                  std::shared_ptr<const iptv::SeriesInfo> catalog) {
    queue = q;
    index = i;
    next = n;
    info = std::move(catalog);
}

double PlaybackProgress::begin(const screens::VodItem &item, bool resumeWanted, double now) {
    latch.reset();
    upNextDone = false;
    upNextResult = autonext::UpNext::None;
    recordedComplete = false;
    return tracker.begin(item, resumeWanted, now);
}

bool PlaybackProgress::episodeComplete() const {
    const screens::VodItem &it = tracker.item();
    if (it.type != iptv::ContentType::Series || it.localMedia || it.id.empty()) {
        return false;
    }
    // this playback's state only: an episode watched in an earlier session and replayed now is not complete again
    // until this playback completes it
    return tracker.isCompleted() || recordedComplete;
}

bool PlaybackProgress::commit() {
    if (upNextDone || next < 0 || index < 0 || index >= (int) queue.size() || next >= (int) queue.size()) {
        return false;
    }
    if (queue[(size_t) index].id != tracker.item().id || !episodeComplete()) {
        return false;   // (a queue of another item) / not complete: nothing moves
    }
    upNextDone = true;
    upNextResult = autonext::persistUpNext(store, queue, index, next, info.get(), wallClock ? wallClock() : 0);
    return true;
}

bool PlaybackProgress::record(double now, double finalPosition, bool atEnd) {
    bool wrote = tracker.record(now, finalPosition, atEnd);
    if (wrote) {
        const screens::VodItem &it = tracker.item();
        const HistoryEntry *e = it.type == iptv::ContentType::Series ? store.progressOf(it.type, it.id) : nullptr;
        recordedComplete = e != nullptr && e->watched;   // what this record stored (the last 45 s, the end)
        commit();   // an episode recorded as complete carries its progression, whichever path recorded it
    }
    return wrote;
}

PlaybackProgress::Update PlaybackProgress::update(double now, double duration) {
    Update u;
    const screens::VodItem &it = tracker.item();
    if (it.type != iptv::ContentType::Series || it.localMedia) {
        return u;
    }
    u.change = latch.update(tracker.position(), duration, tracker.started());
    bool before = upNextDone;
    switch (u.change) {
        case autonext::CompletionLatch::Change::Latched:
            tracker.setCompleted(true);
            record(now);   // watched from now on, whatever happens next (commits the progression)
            break;
        case autonext::CompletionLatch::Change::Released:
            tracker.setCompleted(false);   // seeked back: in progress again; a committed next episode stays
            recordedComplete = false;      // (the next record stores it so)
            break;
        default:
            break;
    }
    if (!upNextDone && next >= 0) {
        commit();   // the next episode became known after the item was complete (the catalog arrived)
    }
    u.committed = upNextDone && !before;
    u.upNext = upNextResult;
    return u;
}
