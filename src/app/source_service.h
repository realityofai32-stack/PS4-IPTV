// One entry point for the source operations the shared screens need (sign-in, Live TV, Movies / Series catalogs
// and details), routed by the profile's source type: Xtream -> XtreamService, Stalker / MAG -> StalkerService.
// Playlist (M3U) sources load through M3uService and have no Movies / Series. Source-specific network logic stays in
// the services; the screens, the VOD library, search, favorites and the players only see the shared outcomes.

#ifndef PS4IPTV_APP_SOURCE_SERVICE_H
#define PS4IPTV_APP_SOURCE_SERVICE_H

#include "stalker_service.h"
#include "xtream_service.h"

class SourceService {

public:

    SourceService(XtreamService &x, StalkerService &s) : xtream(x), stalker(s) {}

    CancelToken authenticate(const iptv::Profile &p, sources::AuthCallback cb) {
        return p.isStalker() ? stalker.authenticate(p, std::move(cb)) : xtream.authenticate(p, std::move(cb));
    }

    CancelToken loadLive(const iptv::Profile &p, const std::string &dataDir, sources::LiveCallback cb,
                         bool cacheOnly = false) {
        return p.isStalker() ? stalker.loadLive(p, dataDir, std::move(cb), cacheOnly)
                             : xtream.loadLive(p, dataDir, std::move(cb), cacheOnly);
    }

    CancelToken loadMovies(const iptv::Profile &p, const std::string &dataDir, sources::Source source,
                           std::function<void(sources::MoviesOutcome &)> cb) {
        return p.isStalker() ? stalker.loadMovies(p, dataDir, source, std::move(cb))
                             : xtream.loadMovies(p, dataDir, source, std::move(cb));
    }

    CancelToken loadSeriesList(const iptv::Profile &p, const std::string &dataDir, sources::Source source,
                               std::function<void(sources::SeriesListOutcome &)> cb) {
        return p.isStalker() ? stalker.loadSeriesList(p, dataDir, source, std::move(cb))
                             : xtream.loadSeriesList(p, dataDir, source, std::move(cb));
    }

    CancelToken loadMovieInfo(const iptv::Profile &p, const std::string &id,
                              std::function<void(sources::InfoOutcome<iptv::MovieInfo> &)> cb) {
        return p.isStalker() ? stalker.loadMovieInfo(p, id, std::move(cb)) : xtream.loadMovieInfo(p, id, std::move(cb));
    }

    CancelToken loadSeriesInfo(const iptv::Profile &p, const std::string &id,
                               std::function<void(sources::InfoOutcome<iptv::SeriesInfo> &)> cb) {
        return p.isStalker() ? stalker.loadSeriesInfo(p, id, std::move(cb)) : xtream.loadSeriesInfo(p, id, std::move(cb));
    }

    // Movies / Series lists older than this are refreshed in the background (a portal's lists are paged: a refresh
    // is many requests, so once a day)
    static int64_t refreshAfter(const iptv::Profile &p) {
        return p.isStalker() ? 24 * 3600 : 6 * 3600;
    }

    // entries read so far by a running first download of a paged (Stalker) list; 0 otherwise
    int progress(const iptv::Profile &p, bool movies) const {
        return p.isStalker() ? stalker.progress(movies) : 0;
    }

    StalkerService &portal() { return stalker; }

private:

    XtreamService &xtream;
    StalkerService &stalker;
};

#endif // PS4IPTV_APP_SOURCE_SERVICE_H
