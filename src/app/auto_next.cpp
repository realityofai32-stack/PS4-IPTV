#include <algorithm>
#include <cmath>

#include "auto_next.h"
#include "series_plan.h"

namespace autonext {

    namespace {
        // without the catalog: the queue's next item must be the obvious successor
        bool obviousSuccessor(const screens::VodItem &cur, const screens::VodItem &n) {
            if (n.season == cur.season) {
                return n.episode == cur.episode + 1;
            }
            return n.season == cur.season + 1 && n.episode <= 1;
        }
    }

    std::vector<screens::VodItem> episodeQueue(const screens::VodItem &current, const iptv::SeriesInfo *info,
                                               const std::vector<screens::VodItem> &downloaded, bool offline,
                                               int &position) {
        position = 0;
        int s = 0, e = 0;
        if (!offline && info != nullptr && current.type == iptv::ContentType::Series
            && seriesplan::locate(*info, current.id, s, e)) {
            std::vector<screens::VodItem> q;
            for (const iptv::Season &season: info->seasons) {
                for (const iptv::Episode &ep: season.episodes) {
                    if (ep.id == current.id) {
                        position = (int) q.size();
                        q.push_back(current);   // exactly as given: resume, local copy, owner profile
                        if (q.back().cmd.empty()) {
                            q.back().cmd = ep.cmd;   // Stalker: the play command from the episode list
                            q.back().linkSeries = ep.linkSeries;
                        }
                        continue;
                    }
                    screens::VodItem it;
                    it.type = iptv::ContentType::Series;
                    it.id = ep.id;
                    it.extension = ep.extension.empty() ? "mkv" : ep.extension;
                    it.title = ep.title;
                    it.image = current.image;
                    it.year = current.year;
                    it.seriesId = current.seriesId;
                    it.seriesName = current.seriesName;
                    it.season = ep.season;
                    it.episode = ep.number;
                    it.durationHint = ep.durationSeconds;
                    it.cmd = ep.cmd;
                    it.linkSeries = ep.linkSeries;
                    q.push_back(it);
                }
            }
            return q;
        }
        for (size_t i = 0; i < downloaded.size(); i++) {
            if (downloaded[i].id == current.id && downloaded[i].type == current.type) {
                std::vector<screens::VodItem> q = downloaded;
                q[i] = current;
                if (q[i].localPath.empty()) {
                    q[i].localPath = downloaded[i].localPath;   // the downloaded copy plays from the disk
                    q[i].profileId = downloaded[i].profileId;
                }
                position = (int) i;
                return q;
            }
        }
        return {current};
    }

    int nextIndex(const std::vector<screens::VodItem> &queue, int index, const iptv::SeriesInfo *info, bool offline) {
        if (index < 0 || index >= (int) queue.size()) {
            return -1;
        }
        const screens::VodItem &cur = queue[(size_t) index];
        if (cur.type != iptv::ContentType::Series || cur.id.empty() || cur.seriesId.empty()) {
            return -1;
        }
        int found = -1;
        int s = 0, e = 0;
        if (info != nullptr && seriesplan::locate(*info, cur.id, s, e)) {
            int ns = 0, ne = 0;
            if (!seriesplan::nextEpisode(*info, s, e, ns, ne)) {
                return -1;   // the series' last episode
            }
            const std::string &id = info->seasons[(size_t) ns].episodes[(size_t) ne].id;
            for (size_t j = 0; j < queue.size(); j++) {
                const screens::VodItem &q = queue[j];
                if ((int) j != index && q.id == id && q.type == iptv::ContentType::Series && q.seriesId == cur.seriesId) {
                    found = (int) j;
                    break;
                }
            }
        } else if (index + 1 < (int) queue.size()) {
            const screens::VodItem &n = queue[(size_t) index + 1];
            if (n.type == iptv::ContentType::Series && n.seriesId == cur.seriesId && !n.id.empty()
                && obviousSuccessor(cur, n)) {
                found = index + 1;
            }
        }
        if (found >= 0 && offline && queue[(size_t) found].localPath.empty()) {
            return -1;   // never a stream while the app is offline
        }
        return found;
    }

    CompletionLatch::Change CompletionLatch::update(double position, double duration, bool started) {
        if (!started || !(duration > 0) || !std::isfinite(duration) || !(position >= 0) || !std::isfinite(position)) {
            return Change::None;
        }
        double remaining = duration - position;
        if (!on) {
            if (remaining <= progress::COMPLETION_REMAINING && position >= progress::MIN_CONTINUE_SECONDS) {
                on = true;
                return Change::Latched;
            }
            return Change::None;
        }
        if (remaining > progress::COMPLETION_REMAINING + RESET_MARGIN) {
            on = false;
            return Change::Released;
        }
        return Change::None;
    }

    namespace {
        // provider order of two episodes: < 0 a before b, 0 the same / unknown, > 0 after
        int order(const std::vector<screens::VodItem> &queue, const iptv::SeriesInfo *info, const std::string &aId,
                  int aSeason, int aEpisode, const std::string &bId, int bSeason, int bEpisode) {
            if (aId == bId) {
                return 0;
            }
            int ai = -1, bi = -1;
            for (size_t i = 0; i < queue.size(); i++) {
                if (queue[i].id == aId) {
                    ai = (int) i;
                }
                if (queue[i].id == bId) {
                    bi = (int) i;
                }
            }
            if (ai >= 0 && bi >= 0) {
                return ai < bi ? -1 : 1;
            }
            int as = 0, ae = 0, bs = 0, be = 0;
            if (info != nullptr && seriesplan::locate(*info, aId, as, ae) && seriesplan::locate(*info, bId, bs, be)) {
                return as != bs ? (as < bs ? -1 : 1) : (ae < be ? -1 : ae > be ? 1 : 0);
            }
            if (aSeason != bSeason) {
                return aSeason < bSeason ? -1 : 1;
            }
            return aEpisode < bEpisode ? -1 : aEpisode > bEpisode ? 1 : 0;
        }
    }

    bool advancesContinue(const std::vector<screens::VodItem> &queue, int next, const iptv::SeriesInfo *info,
                          const HistoryEntry *current) {
        if (next < 0 || next >= (int) queue.size()) {
            return false;
        }
        if (current == nullptr || current->seriesId != queue[(size_t) next].seriesId) {
            return true;
        }
        const screens::VodItem &n = queue[(size_t) next];
        if (current->id == n.id) {
            // already the series' entry: an upNext pointer is refreshed, real progress in it is kept
            return !progress::inProgress(*current);
        }
        return order(queue, info, current->id, current->season, current->episode, n.id, n.season, n.episode) < 0;
    }

    HistoryEntry upNextEntry(const screens::VodItem &next, int64_t watchedAt) {
        HistoryEntry h;
        h.type = iptv::ContentType::Series;
        h.id = next.id;
        h.name = next.title;
        h.icon = next.image;
        h.extension = next.extension;
        h.seriesId = next.seriesId;
        h.seriesName = next.seriesName;
        h.season = next.season;
        h.episode = next.episode;
        h.duration = next.durationHint > 0 ? next.durationHint : 0;
        h.watchedAt = watchedAt;
        return h;
    }

    UpNext persistUpNext(LibraryStore &store, const std::vector<screens::VodItem> &queue, int index, int next,
                         const iptv::SeriesInfo *info, int64_t now) {
        if (next < 0 || next >= (int) queue.size() || index < 0 || index >= (int) queue.size() || next == index) {
            return UpNext::None;
        }
        const screens::VodItem &n = queue[(size_t) next];
        if (n.type != iptv::ContentType::Series || n.id.empty() || n.seriesId.empty()) {
            return UpNext::None;
        }
        const HistoryEntry *current = store.seriesContinueEntry(n.seriesId);
        if (current != nullptr && current->id == queue[(size_t) index].id) {
            current = nullptr;   // the completed episode itself
        }
        if (!advancesContinue(queue, next, info, current)) {
            return UpNext::Kept;
        }
        store.markUpNext(upNextEntry(n, now));
        return UpNext::Moved;
    }

    const char *noNextReason(const std::vector<screens::VodItem> &queue, int index, const iptv::SeriesInfo *info,
                             bool offline) {
        // i18n-exempt-begin: log values
        if (index < 0 || index >= (int) queue.size()) {
            return "no current item";
        }
        const screens::VodItem &cur = queue[(size_t) index];
        if (cur.type != iptv::ContentType::Series || cur.id.empty() || cur.seriesId.empty()) {
            return "not a series episode";
        }
        if (nextIndex(queue, index, info, offline) >= 0) {
            return "";
        }
        int s = 0, e = 0, ns = 0, ne = 0;
        if (info != nullptr && seriesplan::locate(*info, cur.id, s, e)) {
            if (!seriesplan::nextEpisode(*info, s, e, ns, ne)) {
                return "the series' final episode";
            }
            return offline ? "offline and the next episode is not downloaded" : "the next episode is not in the queue";
        }
        if (offline) {
            return "offline: no downloaded episode follows";
        }
        return info != nullptr ? "the episode is not in the series' catalog"
                               : "series catalog not in memory, no obvious successor in the queue";
        // i18n-exempt-end
    }

    int Countdown::seconds() const {
        if (!counting()) {
            return 0;
        }
        return std::min((int) CARD_SECONDS, std::max(1, (int) std::ceil(CARD_SECONDS - elapsed - 1e-9)));
    }

    float Countdown::fraction() const {
        if (!counting()) {
            return 0;
        }
        return (float) std::min(1.0, std::max(0.0, elapsed / CARD_SECONDS));
    }

    bool Countdown::update(const Frame &f) {
        if (state == Phase::Transitioning || state == Phase::Cancelled) {
            return false;
        }
        autoPlay = f.autoPlay;
        double step = lastNow >= 0 && std::isfinite(f.now - lastNow) ? std::min(MAX_STEP, std::max(0.0, f.now - lastNow)) : 0;
        if (state == Phase::PostPlay) {
            // after the end of file: nothing plays any more, the card's clock runs on its own
            lastNow = f.now;
            if (autoPlay) {
                elapsed += step;
                if (elapsed >= CARD_SECONDS - 1e-9) {
                    state = Phase::Transitioning;
                    return true;
                }
            }
            return false;
        }
        const bool known = f.remaining >= 0 && std::isfinite(f.remaining);
        if (!f.hasNext) {
            reset();
            return false;
        }
        if (state == Phase::Idle) {
            if (!f.running || !known || f.remaining > CARD_SECONDS) {
                return false;
            }
            state = Phase::Counting;
            elapsed = 0;
            lastNow = f.now;
            held = false;
            return false;
        }
        if (known && f.remaining > CARD_SECONDS + RESET_MARGIN) {
            reset();   // seeked back: it may come again at the end
            return false;
        }
        lastNow = f.now;
        held = !f.running;   // buffering, reconnecting, an error, the Options panel: hidden, the count waits
        if (held || !autoPlay) {
            return false;
        }
        if (!f.paused) {
            elapsed += step;
        }
        if (elapsed >= CARD_SECONDS - 1e-9) {
            state = Phase::Transitioning;
            return true;
        }
        return false;
    }

    bool Countdown::playNow() {
        if (!visible()) {
            return false;
        }
        state = Phase::Transitioning;
        return true;
    }

    bool Countdown::cancel() {
        if (!visible()) {
            return false;
        }
        state = Phase::Cancelled;
        return true;
    }

    bool Countdown::endOfFile(bool hasNext, bool autoPlayOn, double now) {
        if (state == Phase::Transitioning || state == Phase::Cancelled || state == Phase::PostPlay || !hasNext) {
            return false;
        }
        if (state != Phase::Counting) {
            elapsed = 0;   // the card never showed before the end: its full 7 s from now
        }
        state = Phase::PostPlay;
        held = false;
        lastNow = now;
        autoPlay = autoPlayOn;
        return true;
    }
}
