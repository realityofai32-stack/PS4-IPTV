// Asynchronous Xtream API operations on the job system. Callbacks run on the main thread.

#ifndef PS4IPTV_APP_XTREAM_SERVICE_H
#define PS4IPTV_APP_XTREAM_SERVICE_H

#include <functional>
#include <string>
#include <vector>

#include <memory>

#include "source_types.h"
#include "../iptv/catalog.h"
#include "../iptv/models.h"
#include "../network/jobs.h"

class XtreamService {

public:

    explicit XtreamService(JobSystem &jobs) : jobs(jobs) {}

    using AuthOutcome = sources::AuthOutcome;
    using AuthCallback = sources::AuthCallback;

    CancelToken authenticate(const iptv::Profile &profile, AuthCallback callback);

    using CategoriesOutcome = sources::CategoriesOutcome;
    using CategoriesCallback = sources::CategoriesCallback;

    CancelToken loadCategories(const iptv::Profile &profile, iptv::ContentType type, CategoriesCallback callback);

    using LiveOutcome = sources::LiveOutcome;
    using LiveCallback = sources::LiveCallback;

    // get_live_categories + get_live_streams, parsed on the worker. On network failure the cached copy
    // (dataDir/cache/<profile>/) is used and fromCache is set. Successful responses refresh the cache.
    // cacheOnly (offline mode): the saved copy only, no network request.
    CancelToken loadLive(const iptv::Profile &profile, const std::string &dataDir, LiveCallback callback,
                         bool cacheOnly = false);

    // ------------------------------------------------------------------ Movies / Series (lazy)
    using Source = sources::Source;

    template<typename Catalog>
    using CatalogOutcome = sources::CatalogOutcome<Catalog>;

    using MoviesOutcome = sources::MoviesOutcome;
    using SeriesListOutcome = sources::SeriesListOutcome;

    // get_vod_categories + get_vod_streams (cache names vod_categories / vod_streams)
    CancelToken loadMovies(const iptv::Profile &profile, const std::string &dataDir, Source source,
                           std::function<void(MoviesOutcome &)> callback);

    // get_series_categories + get_series (cache names series_categories / series)
    CancelToken loadSeriesList(const iptv::Profile &profile, const std::string &dataDir, Source source,
                               std::function<void(SeriesListOutcome &)> callback);

    template<typename Info>
    using InfoOutcome = sources::InfoOutcome<Info>;

    // one get_vod_info / get_series_info call (only when a detail screen opens)
    CancelToken loadMovieInfo(const iptv::Profile &profile, const std::string &streamId,
                              std::function<void(InfoOutcome<iptv::MovieInfo> &)> callback);

    CancelToken loadSeriesInfo(const iptv::Profile &profile, const std::string &seriesId,
                               std::function<void(InfoOutcome<iptv::SeriesInfo> &)> callback);

private:

    JobSystem &jobs;
};

#endif // PS4IPTV_APP_XTREAM_SERVICE_H
