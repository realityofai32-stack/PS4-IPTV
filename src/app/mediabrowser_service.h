// Jellyfin / Emby media servers on the job system (iptv/mediabrowser.h): sign-in, the Movies / Series catalogs (their
// saved copies), details, the address a movie / episode plays from and the progress reports. Callbacks run on the
// main thread. Produces the same outcomes as XtreamService / StalkerService, so the shared screens, the VOD library,
// search, favorites and the movie player work unchanged (app/source_service.h routes by the profile's type).
//
// The sign-in's access token lives in memory only (one per signed-in source; asked again at every sign-in) and is
// registered as a secret, so no log line ever shows it - nor the password. Nothing is fetched in the background:
// the catalogs load when Movies / Series open (the saved copy first, refreshed like an Xtream source's), details when
// a detail page opens, and the progress reports are a handful of small requests while a movie / episode plays.

#ifndef PS4IPTV_APP_MEDIABROWSER_SERVICE_H
#define PS4IPTV_APP_MEDIABROWSER_SERVICE_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "source_types.h"
#include "../iptv/mediabrowser.h"
#include "../iptv/models.h"
#include "../network/jobs.h"

class MediaBrowserService {

public:

    static constexpr const char *CACHE_MOVIES = "mediaserver_movies";
    static constexpr const char *CACHE_SERIES = "mediaserver_series";

    MediaBrowserService(JobSystem &jobs, std::string appVersion);

    CancelToken authenticate(const iptv::Profile &profile, sources::AuthCallback callback);

    CancelToken loadMovies(const iptv::Profile &profile, const std::string &dataDir, sources::Source source,
                           std::function<void(sources::MoviesOutcome &)> callback);

    CancelToken loadSeriesList(const iptv::Profile &profile, const std::string &dataDir, sources::Source source,
                               std::function<void(sources::SeriesListOutcome &)> callback);

    CancelToken loadMovieInfo(const iptv::Profile &profile, const std::string &id,
                              std::function<void(sources::InfoOutcome<iptv::MovieInfo> &)> callback);

    CancelToken loadSeriesInfo(const iptv::Profile &profile, const std::string &id,
                               std::function<void(sources::InfoOutcome<iptv::SeriesInfo> &)> callback);

    // the address a movie / episode plays from ("" when the source is not signed in: offline)
    std::string streamUrl(const iptv::Profile &profile, const std::string &itemId) const;

    // tells the server what plays (fire and forget; nothing when not signed in). playSessionId: one per playback.
    void report(const iptv::Profile &profile, mediabrowser::Report report, const std::string &itemId,
                const std::string &playSessionId, double positionSeconds, bool paused);

    bool signedIn(const iptv::Profile &profile) const;

    // what the server is ("Jellyfin Server 10.10.3"), for the source's status line ("" before a sign-in)
    std::string serverName(const iptv::Profile &profile) const;

private:

    struct Session {
        std::string entered;     // the server address and user it signed in with (an edited source signs in again)
        std::string user;
        std::string base;        // after redirects (an http address the server moved to https)
        std::string token;
        std::string userId;
        std::string product;
        std::vector<std::string> headers;   // authorization (+ token) of every later request
    };

    std::shared_ptr<const Session> sessionFor(const iptv::Profile &profile) const;

    JobSystem &jobs;
    std::string appVersion;
    std::map<std::string, std::shared_ptr<const Session>> sessions;   // profile id -> its sign-in (main thread)
};

#endif // PS4IPTV_APP_MEDIABROWSER_SERVICE_H
