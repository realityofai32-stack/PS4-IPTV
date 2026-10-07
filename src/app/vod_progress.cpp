#include <cmath>

#include "vod_progress.h"

double VodProgress::begin(const screens::VodItem &item, bool resumeWanted, double now) {
    current = item;
    knownDuration = item.durationHint > 0 ? item.durationHint : 0;
    hadFrame = false;
    pendingTarget = -1;
    settleTarget = -1;
    lastMemory = now;
    lastDisk = now;
    start = 0;
    const HistoryEntry *p = store.progressOf(item.type, item.id);
    if (resumeWanted && p != nullptr && progress::inProgress(p->position, p->duration, p->watched)) {
        start = p->position;   // exactly where playback stopped
        if (knownDuration <= 0 && p->duration > 0) {
            knownDuration = p->duration;
        }
    }
    lastPosition = start;
    return start;
}

void VodProgress::observe(double position, double duration, bool started, double now) {
    if (duration > 0) {
        knownDuration = duration;
    }
    if (!started) {
        return;
    }
    hadFrame = true;
    if (pendingTarget >= 0 || position < 0 || (position <= 0 && lastPosition > 1)) {
        return;   // seeking, unknown, or the not yet polled 0 right after a resumed start
    }
    if (settleTarget >= 0) {
        // mpv may report the old position until the seek is done
        if (std::fabs(position - settleTarget) > 2.5 && now < settleUntil) {
            return;
        }
        settleTarget = -1;
    }
    lastPosition = position;
}

void VodProgress::seekPending(double target) {
    pendingTarget = target < 0 ? 0 : target;
}

void VodProgress::seekCommitted(double target, double now) {
    pendingTarget = -1;
    settleTarget = target < 0 ? 0 : target;
    settleUntil = now + SEEK_SETTLE;
    lastPosition = settleTarget;
}

VodProgress::Due VodProgress::due(double now) const {
    if (!hadFrame) {
        return Due::None;
    }
    if (now - lastDisk >= DISK_EVERY) {
        return Due::Disk;
    }
    return now - lastMemory >= MEMORY_EVERY ? Due::Memory : Due::None;
}

double VodProgress::position() const {
    return pendingTarget >= 0 ? pendingTarget : lastPosition;
}

bool VodProgress::record(double now, double finalPosition, bool atEnd) {
    lastMemory = now;
    lastDisk = now;
    double pos;
    if (atEnd) {
        pos = knownDuration > 0 ? knownDuration : lastPosition;
    } else if (pendingTarget >= 0) {
        pos = pendingTarget;                 // the user asked for this position
    } else if (finalPosition >= 0 && hadFrame
               && (settleTarget < 0 || std::fabs(finalPosition - settleTarget) <= 2.5 || now >= settleUntil)) {
        pos = finalPosition;                 // queried from the player right now
    } else {
        pos = lastPosition;
    }
    if (!atEnd && pos < 1) {
        return false;   // never got going: keep whatever was saved before
    }
    if (finalPosition >= 0 && hadFrame) {
        lastPosition = pos;
    }
    const screens::VodItem &it = current;
    bool episode = it.type == iptv::ContentType::Series;
    HistoryEntry h;
    h.type = episode ? iptv::ContentType::Series : iptv::ContentType::Movie;
    h.id = it.id;
    h.name = it.title.empty() && episode ? "Episode " + std::to_string(it.episode) : it.title;
    h.icon = it.image;
    h.extension = it.extension;
    h.seriesId = it.seriesId;
    h.seriesName = it.seriesName;
    h.season = it.season;
    h.episode = it.episode;
    h.position = pos;
    h.duration = knownDuration;
    h.watched = atEnd || progress::isWatched(pos, knownDuration);
    h.watchedAt = wallClock ? wallClock() : 0;
    store.updateProgress(h);
    return true;
}
