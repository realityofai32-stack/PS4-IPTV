// Sort modes of the Movies / Series grids (host-testable).

#ifndef PS4IPTV_IPTV_SORTING_H
#define PS4IPTV_IPTV_SORTING_H

#include <string>

#include "models.h"

namespace iptv {

    enum class SortMode {
        Provider,          // the provider's order (default)
        TitleAZ,
        TitleZA,
        AddedNewest,       // movies: `added`; series: `last_modified` (shown as "Recently updated")
        AddedOldest,
        YearNewest,        // movies: year in the name; series: releaseDate
        YearOldest,
        RatingHigh,
        RecentlyWatched,   // last playback activity of this profile first, then the rest in provider order
        Count
    };

    // stable key stored in settings.json ("provider", "az", ...)
    const char *sortModeKey(SortMode mode);

    SortMode sortModeFromKey(const std::string &key);

    // user-facing name; series have no "added" date, only "last modified"
    const char *sortModeName(SortMode mode, ContentType type);

    // which metadata the catalog actually has (a mode is offered only when enough items carry its value)
    struct SortSupport {
        bool added = false;
        bool year = false;
        bool rating = false;

        bool supports(SortMode mode) const;
    };
}

#endif // PS4IPTV_IPTV_SORTING_H
