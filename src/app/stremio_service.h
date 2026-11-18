// Stremio add-on sources on the job system (iptv/stremio.h): the manifest at sign-in, the Movies / Series catalogs
// (their saved copies), details (meta) and, when a title plays, its stream. Callbacks run on the main thread. The same
// outcomes as the other services, so the shared screens and the movie player work unchanged.
//
// The add-on's address may carry its configuration: it is never logged (only m3u::displayUrl: host and file name)
// and its long path parts are registered secrets (storage/profile_store.cpp). Nothing runs in the background.

#ifndef PS4IPTV_APP_STREMIO_SERVICE_H
#define PS4IPTV_APP_STREMIO_SERVICE_H

#include <map>
#include <memory>
#include <string>

#include "source_types.h"
#include "../iptv/models.h"
#include "../iptv/stremio.h"
#include "../network/jobs.h"

class StremioService {

public:

    static constexpr const char *CACHE_MOVIES = "stremio_movies";
    static constexpr const char *CACHE_SERIES = "stremio_series";

    explicit StremioService(JobSystem &jobs) : jobs(jobs) {}

    // reads the manifest: ok when the add-on lists movies / series and offers streams
    CancelToken authenticate(const iptv::Profile &profile, sources::AuthCallback callback);

    CancelToken loadMovies(const iptv::Profile &profile, const std::string &dataDir, sources::Source source,
                           std::function<void(sources::MoviesOutcome &)> callback);

    CancelToken loadSeriesList(const iptv::Profile &profile, const std::string &dataDir, sources::Source source,
                               std::function<void(sources::SeriesListOutcome &)> callback);

    CancelToken loadMovieInfo(const iptv::Profile &profile, const std::string &id,
                              std::function<void(sources::InfoOutcome<iptv::MovieInfo> &)> callback);

    CancelToken loadSeriesInfo(const iptv::Profile &profile, const std::string &id,
                               std::function<void(sources::InfoOutcome<iptv::SeriesInfo> &)> callback);

    // the first stream of a movie / an episode (video id) the player can open
    CancelToken resolve(const iptv::Profile &profile, bool episode, const std::string &id,
                        sources::LinkCallback callback);

private:

    struct Addon {
        std::string manifestUrl;
        std::string base;
        stremio::Manifest manifest;
    };

    std::shared_ptr<const Addon> addonFor(const iptv::Profile &profile) const;

    JobSystem &jobs;
    std::map<std::string, std::shared_ptr<const Addon>> addons;   // profile id -> its manifest (main thread)
};

#endif // PS4IPTV_APP_STREMIO_SERVICE_H
