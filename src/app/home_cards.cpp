#include <algorithm>

#include "home_cards.h"
#include "../core/format.h"
#include "../i18n/i18n.h"

namespace homecards {

    std::string episodeLine(const HistoryEntry &h) {
        std::string code = fmt::episodeCode(h.season, h.episode);
        return code + " \xC2\xB7 " + (h.name.empty() ? i18n::tr("episode.number", {std::to_string(h.episode)}) : h.name);
    }

    bool isUpNext(const HistoryEntry &h) {
        return h.type == iptv::ContentType::Series && h.upNext && !progress::inProgress(h);
    }

    Text historyCard(const HistoryEntry &h, Rail rail, const std::string &channelNumber) {
        Text t;
        const bool live = h.type == iptv::ContentType::Live;
        const bool series = h.type == iptv::ContentType::Series;
        t.second = series ? episodeLine(h) : i18n::tr(h.type == iptv::ContentType::Movie ? "home.kind_movie"
                                                                                        : "home.kind_live");
        if (rail == Rail::Recent) {
            // what it is, nothing about where playback stopped
            t.third = series ? i18n::tr("home.kind_series") : live ? channelNumber : "";
            return t;
        }
        if (!live && isUpNext(h)) {
            t.third = i18n::tr("home.up_next");
        } else if (!live && progress::inProgress(h)) {
            t.third = fmt::remaining(h.position, h.duration);
            t.progress = std::max(0.02f, (float) progress::fraction(h.position, h.duration));
        } else if (h.watched) {
            t.third = "\xE2\x9C\x93 " + i18n::tr("progress.watched");
        }
        return t;
    }
}
