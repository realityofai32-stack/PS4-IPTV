#include "series_plan.h"
#include "../i18n/i18n.h"
#include "../core/format.h"

namespace seriesplan {

    namespace {
        bool locate(const iptv::SeriesInfo &info, const std::string &episodeId, int &season, int &episode) {
            for (size_t s = 0; s < info.seasons.size(); s++) {
                const auto &eps = info.seasons[s].episodes;
                for (size_t e = 0; e < eps.size(); e++) {
                    if (eps[e].id == episodeId) {
                        season = (int) s;
                        episode = (int) e;
                        return true;
                    }
                }
            }
            return false;
        }

        // first episode of the first regular season (specials, season 0, only when nothing else exists)
        bool first(const iptv::SeriesInfo &info, int &season, int &episode) {
            int fallback = -1;
            for (size_t s = 0; s < info.seasons.size(); s++) {
                if (info.seasons[s].episodes.empty()) {
                    continue;
                }
                if (info.seasons[s].number >= 1) {
                    season = (int) s;
                    episode = 0;
                    return true;
                }
                if (fallback < 0) {
                    fallback = (int) s;
                }
            }
            if (fallback >= 0) {
                season = fallback;
                episode = 0;
                return true;
            }
            return false;
        }
    }

    EpisodeState episodeState(const HistoryEntry *p) {
        if (p == nullptr) {
            return EpisodeState::Untouched;
        }
        if (p->watched || progress::isWatched(p->position, p->duration)) {
            return EpisodeState::Watched;
        }
        return progress::inProgress(p->position, p->duration, p->watched) ? EpisodeState::InProgress
                                                                         : EpisodeState::Untouched;
    }

    Action defaultAction(const iptv::SeriesInfo &info, const std::string &seriesId, const LibraryStore &store) {
        Action a;
        std::vector<const HistoryEntry *> played = store.seriesProgress(seriesId);   // most recent first
        // 1. an episode in progress
        for (const HistoryEntry *p: played) {
            int s, e;
            if (episodeState(p) == EpisodeState::InProgress && locate(info, p->id, s, e)) {
                a.kind = Kind::Resume;
                a.season = s;
                a.episode = e;
                a.position = p->position;
                return a;
            }
        }
        // 2. the most recently played episode decides: completed -> the next one, barely started -> again
        for (const HistoryEntry *p: played) {
            int s, e;
            if (!locate(info, p->id, s, e)) {
                continue;   // no longer offered by the provider
            }
            if (episodeState(p) == EpisodeState::Watched) {
                const auto &eps = info.seasons[(size_t) s].episodes;
                if ((size_t) e + 1 < eps.size()) {
                    a.kind = Kind::Next;
                    a.season = s;
                    a.episode = e + 1;
                    return a;
                }
                for (size_t ns = (size_t) s + 1; ns < info.seasons.size(); ns++) {
                    if (!info.seasons[ns].episodes.empty()) {
                        a.kind = Kind::Next;
                        a.season = (int) ns;
                        a.episode = 0;
                        return a;
                    }
                }
                break;   // the last episode was completed: start from the beginning
            }
            a.kind = Kind::Play;
            a.season = s;
            a.episode = e;
            return a;
        }
        int s, e;
        if (first(info, s, e)) {
            a.kind = Kind::Play;
            a.season = s;
            a.episode = e;
        }
        return a;
    }

    std::string label(const Action &a, const iptv::SeriesInfo &info) {
        if (a.kind == Kind::None || a.season < 0 || a.season >= (int) info.seasons.size()
            || a.episode < 0 || a.episode >= (int) info.seasons[(size_t) a.season].episodes.size()) {
            return "";
        }
        const iptv::Episode &ep = info.seasons[(size_t) a.season].episodes[(size_t) a.episode];
        std::string code = fmt::episodeCode(ep.season, ep.number);
        switch (a.kind) {
            case Kind::Resume:
                return i18n::tr("series.resume_episode", {code, fmt::clock(a.position)});
            case Kind::Next:
                return i18n::tr("series.next_episode", {code});
            default:
                return i18n::tr("series.play_episode", {code});
        }
    }
}
