// Series detail: which episode the main button plays, and per-episode progress states (host-testable).
//
// Main button:
//   Resume SxxEyy · 24:16   the most recently played episode that is in progress (exact position)
//   Next SxxEyy             no episode in progress, the most recently played one was completed: the one after
//   Play SxxEyy             otherwise: the episode last opened (under 5 s), or the first episode
// Nothing is created for an episode until its playback actually starts.

#ifndef PS4IPTV_APP_SERIES_PLAN_H
#define PS4IPTV_APP_SERIES_PLAN_H

#include <string>

#include "../iptv/models.h"
#include "../storage/library_store.h"

namespace seriesplan {

    enum class Kind {
        None,      // no episodes
        Play,
        Resume,
        Next
    };

    struct Action {
        Kind kind = Kind::None;
        int season = 0;     // index into SeriesInfo::seasons
        int episode = 0;    // index into that season's episodes
        double position = 0;   // Resume: where playback continues
    };

    Action defaultAction(const iptv::SeriesInfo &info, const std::string &seriesId, const LibraryStore &store);

    // "Play S01E01", "Resume S01E03 · 24:16", "Next S01E04"
    std::string label(const Action &action, const iptv::SeriesInfo &info);

    // The episode after seasons[season].episodes[episode] in the provider's catalog order: the next one of its
    // season, else the first episode of the next season that has any (numbering is never assumed to be
    // sequential). false after the series' last episode or for an index outside the catalog. Used by the main
    // button's "Next" and by the player's next-episode countdown (app/auto_next.h).
    bool nextEpisode(const iptv::SeriesInfo &info, int season, int episode, int &nextSeason, int &nextEpisode);

    // the season / episode indexes of an episode id in the catalog
    bool locate(const iptv::SeriesInfo &info, const std::string &episodeId, int &season, int &episode);

    enum class EpisodeState {
        Untouched,
        InProgress,   // progress bar + "24:16 / 57:00"
        Watched       // "✓ Watched"
    };

    EpisodeState episodeState(const HistoryEntry *progress);
}

#endif // PS4IPTV_APP_SERIES_PLAN_H
