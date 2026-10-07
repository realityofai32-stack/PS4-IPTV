// Movies and Series catalogs, loaded lazily (never at sign-in).
//
// open*(): the first time a section is opened, the saved copy (dataDir/cache/<profile>/) is loaded and
// shown at once; when it is older than REFRESH_AFTER, or missing, the provider list is fetched in the
// background and replaces it. Detail responses (get_vod_info / get_series_info) are fetched only when a
// detail screen asks, de-duplicated, and kept in small LRU caches.
//
// Screens hold indices into the catalogs: when generation() changes they must rebuild their views.

#ifndef PS4IPTV_APP_VOD_LIBRARY_H
#define PS4IPTV_APP_VOD_LIBRARY_H

#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include "xtream_service.h"
#include "../images/lru_cache.h"
#include "../iptv/catalog.h"

enum class CatalogStatus {
    NotLoaded,
    Loading,      // nothing to show yet
    Ready,
    Failed        // nothing to show, message says why
};

struct SectionStatus {
    CatalogStatus status = CatalogStatus::NotLoaded;
    bool refreshing = false;   // a provider refresh is running while the saved list is shown
    bool fromCache = false;    // the list shown is the saved copy
    int64_t savedAt = 0;
    std::string message;       // user-facing problem ("" when fine)
    // provider refreshes finished so far and how the last one ended (the browser toasts the outcome)
    unsigned refreshes = 0;
    bool lastRefreshOk = false;
    std::string lastRefreshError;
};

class VodLibrary {

public:

    static const int64_t REFRESH_AFTER = 6 * 3600;   // seconds

    VodLibrary(XtreamService &xtream, std::string dataDir);

    // a new sign-in: forget the previous profile's catalogs and details
    void reset();

    void openMovies(const iptv::Profile &profile);

    void openSeries(const iptv::Profile &profile);

    // provider refresh now (Triangle in the browser), keeps the current list until it succeeds
    void refreshMovies(const iptv::Profile &profile);

    void refreshSeries(const iptv::Profile &profile);

    const iptv::MovieCatalog &movies() const { return *movieCatalog; }

    const iptv::SeriesCatalog &series() const { return *seriesCatalog; }

    const SectionStatus &movieStatus() const { return movieState; }

    const SectionStatus &seriesStatus() const { return seriesState; }

    // details: request (no-op when cached or in flight), then read
    void requestMovieInfo(const iptv::Profile &profile, const std::string &streamId);

    std::shared_ptr<const iptv::MovieInfo> movieInfo(const std::string &streamId);

    void requestSeriesInfo(const iptv::Profile &profile, const std::string &seriesId, bool force = false);

    std::shared_ptr<const iptv::SeriesInfo> seriesInfo(const std::string &seriesId);

    // "" unless the last detail request for the id failed
    std::string detailError(const std::string &key) const;

    // changes whenever a catalog or a detail arrives
    unsigned generation() const { return gen; }

private:

    template<typename Catalog>
    void open(const iptv::Profile &profile, bool movies);

    void refresh(const iptv::Profile &profile, bool movies);

    void store(std::shared_ptr<iptv::MovieCatalog> c) { movieCatalog = std::move(c); }

    void store(std::shared_ptr<iptv::SeriesCatalog> c) { seriesCatalog = std::move(c); }

    XtreamService &xtream;
    std::string dir;
    std::shared_ptr<iptv::MovieCatalog> movieCatalog = std::make_shared<iptv::MovieCatalog>();
    std::shared_ptr<iptv::SeriesCatalog> seriesCatalog = std::make_shared<iptv::SeriesCatalog>();
    SectionStatus movieState;
    SectionStatus seriesState;
    images::LruCache<std::shared_ptr<const iptv::MovieInfo>> movieInfos{64, 4ll * 1024 * 1024};
    images::LruCache<std::shared_ptr<const iptv::SeriesInfo>> seriesInfos{12, 8ll * 1024 * 1024};
    std::set<std::string> inFlight;                       // "m<id>" / "s<id>"
    std::unordered_map<std::string, std::string> errors;  // same keys
    std::shared_ptr<int> epoch = std::make_shared<int>(0);  // bumps on reset(): late callbacks are dropped
    unsigned gen = 0;
};

#endif // PS4IPTV_APP_VOD_LIBRARY_H
