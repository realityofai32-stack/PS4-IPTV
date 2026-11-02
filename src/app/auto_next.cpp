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

    bool Countdown::update(const Frame &f) {
        if (state == Phase::Transitioning || state == Phase::Cancelled) {
            return false;
        }
        const bool known = f.remaining >= 0 && std::isfinite(f.remaining);
        if (!f.canAdvance || !f.running || !known) {
            state = Phase::Idle;
            shownSeconds = 0;
            shownFraction = 0;
            return false;
        }
        if (state == Phase::Idle) {
            if (f.remaining > COUNTDOWN_SECONDS) {
                return false;
            }
            state = Phase::Counting;
        } else if (f.remaining > COUNTDOWN_SECONDS + RESET_MARGIN) {
            state = Phase::Idle;   // seeked back: it may come again at the end
            shownSeconds = 0;
            shownFraction = 0;
            return false;
        }
        if (f.remaining <= START_AT) {
            state = Phase::Transitioning;
            shownSeconds = 0;
            shownFraction = 0;
            return true;
        }
        shownSeconds = std::min((int) COUNTDOWN_SECONDS, std::max(1, (int) std::ceil(f.remaining)));
        shownFraction = (float) std::min(1.0, f.remaining / COUNTDOWN_SECONDS);
        return false;
    }

    bool Countdown::playNow() {
        if (state != Phase::Counting) {
            return false;
        }
        state = Phase::Transitioning;
        return true;
    }

    bool Countdown::cancel() {
        if (state != Phase::Counting) {
            return false;
        }
        state = Phase::Cancelled;
        shownSeconds = 0;
        shownFraction = 0;
        return true;
    }

    bool Countdown::endOfFile(bool canAdvance) {
        if (state == Phase::Transitioning || state == Phase::Cancelled || !canAdvance) {
            return false;
        }
        state = Phase::Transitioning;
        return true;
    }
}
