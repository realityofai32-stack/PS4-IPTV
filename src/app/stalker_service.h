// Asynchronous Stalker / MAG portal operations on the job system (protocol details: iptv/stalker.h). The outcomes
// are the shared source outcomes (app/source_types.h), so Live TV, Movies, Series, Search, Favorites, Continue
// Watching and the players work on a portal the same way as on an Xtream source.
//
// Session: one per profile. The first request performs the handshake (the API address is discovered from the
// portal address: portal.php / stalker_portal/server/load.php) and get_profile; the Bearer token is kept for the
// session. A request the portal rejects as unauthorized triggers one new handshake and is retried once. Tokens,
// the MAC address and the device ids are registered with the log redactor and never logged.
//
// Catalogs: Live TV comes from get_all_channels (one call; paged get_ordered_list when a portal lacks it). Movies
// and Series are paged lists (typically 14 entries per page), read category by category up to MAX_PAGES pages per
// section, then kept as a saved copy like Xtream lists and refreshed in the background when older than a day.
// Movie and series details come from the list entries themselves (portals have no detail call); seasons and
// episodes of the series module are requested when a series opens. Playback addresses are created per play with
// create_link (single-use play tokens): Stalker sources have no offline downloads.

#ifndef PS4IPTV_APP_STALKER_SERVICE_H
#define PS4IPTV_APP_STALKER_SERVICE_H

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "source_types.h"
#include "../iptv/stalker.h"
#include "../network/jobs.h"

class StalkerService {

public:

    // pages of one Movies / Series list read at most (14 entries per page: about 21,000 entries)
    static const int MAX_PAGES = 1500;

    StalkerService(JobSystem &jobs, std::string dataDir);

    CancelToken authenticate(const iptv::Profile &profile, sources::AuthCallback callback);

    CancelToken loadLive(const iptv::Profile &profile, const std::string &dataDir, sources::LiveCallback callback,
                         bool cacheOnly = false);

    CancelToken loadMovies(const iptv::Profile &profile, const std::string &dataDir, sources::Source source,
                           std::function<void(sources::MoviesOutcome &)> callback);

    CancelToken loadSeriesList(const iptv::Profile &profile, const std::string &dataDir, sources::Source source,
                               std::function<void(sources::SeriesListOutcome &)> callback);

    CancelToken loadMovieInfo(const iptv::Profile &profile, const std::string &streamId,
                              std::function<void(sources::InfoOutcome<iptv::MovieInfo> &)> callback);

    CancelToken loadSeriesInfo(const iptv::Profile &profile, const std::string &seriesId,
                               std::function<void(sources::InfoOutcome<iptv::SeriesInfo> &)> callback);

    // playable addresses (create_link), with the MAG User-Agent for the stream request
    CancelToken resolveLive(const iptv::Profile &profile, const iptv::LiveChannel &channel,
                            sources::LinkCallback callback);

    CancelToken resolveMovie(const iptv::Profile &profile, const std::string &movieId, sources::LinkCallback callback);

    CancelToken resolveEpisode(const iptv::Profile &profile, const std::string &cmd, int series,
                               sources::LinkCallback callback);

    // entries read so far by a running Movies (true) / Series list download (0 when none runs)
    int progress(bool movies) const;

    struct Session;
    struct Store;

private:

    std::shared_ptr<Session> sessionFor(const iptv::Profile &profile);

    JobSystem &jobs;
    std::string dir;
    std::map<std::string, std::shared_ptr<Session>> sessions;   // by profile id (main thread)
    std::shared_ptr<Store> store;
    std::shared_ptr<std::atomic<int>> movieProgress = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> seriesProgress = std::make_shared<std::atomic<int>>(0);
};

#endif // PS4IPTV_APP_STALKER_SERVICE_H
