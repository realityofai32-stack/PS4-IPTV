// Asynchronous Xtream API operations on the job system. Callbacks run on the main thread.

#ifndef PS4IPTV_APP_XTREAM_SERVICE_H
#define PS4IPTV_APP_XTREAM_SERVICE_H

#include <functional>
#include <string>
#include <vector>

#include <memory>

#include "../iptv/catalog.h"
#include "../iptv/models.h"
#include "../network/jobs.h"

class XtreamService {

public:

    explicit XtreamService(JobSystem &jobs) : jobs(jobs) {}

    struct AuthOutcome {
        iptv::AuthResult result;
        std::string message;      // user-facing
        bool httpsWarning = false; // API reachable but streams would need HTTPS
    };

    using AuthCallback = std::function<void(const AuthOutcome &)>;

    CancelToken authenticate(const iptv::Profile &profile, AuthCallback callback);

    struct CategoriesOutcome {
        bool ok = false;
        std::vector<iptv::Category> categories;
        std::string message;      // user-facing error
    };

    using CategoriesCallback = std::function<void(const CategoriesOutcome &)>;

    CancelToken loadCategories(const iptv::Profile &profile, iptv::ContentType type, CategoriesCallback callback);

    struct LiveOutcome {
        bool ok = false;
        bool fromCache = false;
        int64_t savedAt = 0;
        std::vector<iptv::Category> categories;
        std::vector<iptv::LiveChannel> channels;
        std::string message;       // user-facing error (or cache notice)
    };

    using LiveCallback = std::function<void(LiveOutcome &)>;

    // get_live_categories + get_live_streams, parsed on the worker. On network failure the cached copy
    // (dataDir/cache/<profile>/) is used and fromCache is set. Successful responses refresh the cache.
    CancelToken loadLive(const iptv::Profile &profile, const std::string &dataDir, LiveCallback callback);

    // ------------------------------------------------------------------ Movies / Series (lazy)
    enum class Source {
        Cache,      // the saved copy only (fast, offline)
        Network     // the provider; a good response replaces the saved copy
    };

    template<typename Catalog>
    struct CatalogOutcome {
        bool ok = false;
        int64_t savedAt = 0;
        std::shared_ptr<Catalog> catalog;   // parsed and indexed on the worker
        std::string message;               // user-facing error
    };

    using MoviesOutcome = CatalogOutcome<iptv::MovieCatalog>;
    using SeriesListOutcome = CatalogOutcome<iptv::SeriesCatalog>;

    // get_vod_categories + get_vod_streams (cache names vod_categories / vod_streams)
    CancelToken loadMovies(const iptv::Profile &profile, const std::string &dataDir, Source source,
                           std::function<void(MoviesOutcome &)> callback);

    // get_series_categories + get_series (cache names series_categories / series)
    CancelToken loadSeriesList(const iptv::Profile &profile, const std::string &dataDir, Source source,
                               std::function<void(SeriesListOutcome &)> callback);

    template<typename Info>
    struct InfoOutcome {
        bool ok = false;
        Info info;
        std::string message;
    };

    // one get_vod_info / get_series_info call (only when a detail screen opens)
    CancelToken loadMovieInfo(const iptv::Profile &profile, const std::string &streamId,
                              std::function<void(InfoOutcome<iptv::MovieInfo> &)> callback);

    CancelToken loadSeriesInfo(const iptv::Profile &profile, const std::string &seriesId,
                               std::function<void(InfoOutcome<iptv::SeriesInfo> &)> callback);

private:

    JobSystem &jobs;
};

#endif // PS4IPTV_APP_XTREAM_SERVICE_H
