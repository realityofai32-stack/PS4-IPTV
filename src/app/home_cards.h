// What a Home history card shows besides its title (host-testable).
//
//   Continue Watching   where the user stopped: "S01E03 · Title" / Movie, then the time left and a progress bar, or
//                       "Next episode" for a series' next episode (not started yet)
//   Recently Watched    only what the item is (v7-ux-9): "S01E03 · Title" + Series, Movie, Live TV + its channel
//                       number - never a position, time, progress bar or "Watched" (that is Continue Watching's job)

#ifndef PS4IPTV_APP_HOME_CARDS_H
#define PS4IPTV_APP_HOME_CARDS_H

#include <string>

#include "../storage/library_store.h"

namespace homecards {

    enum class Rail {
        Continue,
        Recent
    };

    struct Text {
        std::string second;     // the line under the title
        std::string third;      // the line under that
        float progress = 0;     // > 0: a progress bar (Continue Watching only)
    };

    // channelNumber: a live channel's number in the current list ("" unknown)
    Text historyCard(const HistoryEntry &h, Rail rail, const std::string &channelNumber = "");

    // "S01E03 · Title" (an untitled episode: "Episode 3")
    std::string episodeLine(const HistoryEntry &h);

    // a series' Continue Watching entry that points at its next episode (not started yet)
    bool isUpNext(const HistoryEntry &h);
}

#endif // PS4IPTV_APP_HOME_CARDS_H
