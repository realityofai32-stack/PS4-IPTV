// One entry point for the source operations the shared screens need (sign-in, Live TV, Movies / Series catalogs
// and details), routed by the profile's source type: Xtream -> XtreamService, Stalker / MAG -> StalkerService.
// Playlist (M3U) sources load through M3uService and have no Movies / Series; their programme guide comes from the
// XMLTV feed their header names (XmltvService). Jellyfin / Emby media servers -> MediaBrowserService, Stremio
// add-ons -> StremioService (both Movies / Series only). Source-specific network logic stays in the services; the screens, the VOD library, search, favorites
// and the players only see the shared outcomes.

#ifndef PS4IPTV_APP_SOURCE_SERVICE_H
#define PS4IPTV_APP_SOURCE_SERVICE_H

#include "mediabrowser_service.h"
#include "stalker_service.h"
#include "stremio_service.h"
#include "xmltv_service.h"
#include "xtream_service.h"

class SourceService {

public:

    SourceService(XtreamService &x, StalkerService &s, XmltvService &g, MediaBrowserService &m, StremioService &a)
            : xtream(x), stalker(s), xmltv(g), media(m), addon(a) {}

    CancelToken authenticate(const iptv::Profile &p, sources::AuthCallback cb) {
        if (p.isMediaServer()) {
            return media.authenticate(p, std::move(cb));
        }
        if (p.isAddon()) {
            return addon.authenticate(p, std::move(cb));
        }
        return p.isStalker() ? stalker.authenticate(p, std::move(cb)) : xtream.authenticate(p, std::move(cb));
    }

    // media servers and add-ons have no Live TV: never asked (the connect screen skips the step)
    CancelToken loadLive(const iptv::Profile &p, const std::string &dataDir, sources::LiveCallback cb,
                         bool cacheOnly = false) {
        return p.isStalker() ? stalker.loadLive(p, dataDir, std::move(cb), cacheOnly)
                             : xtream.loadLive(p, dataDir, std::move(cb), cacheOnly);
    }

    CancelToken loadMovies(const iptv::Profile &p, const std::string &dataDir, sources::Source source,
                           std::function<void(sources::MoviesOutcome &)> cb) {
        if (p.isMediaServer()) {
            return media.loadMovies(p, dataDir, source, std::move(cb));
        }
        if (p.isAddon()) {
            return addon.loadMovies(p, dataDir, source, std::move(cb));
        }
        return p.isStalker() ? stalker.loadMovies(p, dataDir, source, std::move(cb))
                             : xtream.loadMovies(p, dataDir, source, std::move(cb));
    }

    CancelToken loadSeriesList(const iptv::Profile &p, const std::string &dataDir, sources::Source source,
                               std::function<void(sources::SeriesListOutcome &)> cb) {
        if (p.isMediaServer()) {
            return media.loadSeriesList(p, dataDir, source, std::move(cb));
        }
        if (p.isAddon()) {
            return addon.loadSeriesList(p, dataDir, source, std::move(cb));
        }
        return p.isStalker() ? stalker.loadSeriesList(p, dataDir, source, std::move(cb))
                             : xtream.loadSeriesList(p, dataDir, source, std::move(cb));
    }

    CancelToken loadMovieInfo(const iptv::Profile &p, const std::string &id,
                              std::function<void(sources::InfoOutcome<iptv::MovieInfo> &)> cb) {
        if (p.isMediaServer()) {
            return media.loadMovieInfo(p, id, std::move(cb));
        }
        if (p.isAddon()) {
            return addon.loadMovieInfo(p, id, std::move(cb));
        }
        return p.isStalker() ? stalker.loadMovieInfo(p, id, std::move(cb)) : xtream.loadMovieInfo(p, id, std::move(cb));
    }

    CancelToken loadSeriesInfo(const iptv::Profile &p, const std::string &id,
                               std::function<void(sources::InfoOutcome<iptv::SeriesInfo> &)> cb) {
        if (p.isMediaServer()) {
            return media.loadSeriesInfo(p, id, std::move(cb));
        }
        if (p.isAddon()) {
            return addon.loadSeriesInfo(p, id, std::move(cb));
        }
        return p.isStalker() ? stalker.loadSeriesInfo(p, id, std::move(cb)) : xtream.loadSeriesInfo(p, id, std::move(cb));
    }

    MediaBrowserService &mediaServer() { return media; }

    StremioService &addons() { return addon; }

    // one channel's short programme guide: only when the live player's guide panel asks, and only for a channel
    // epg::sourceFor gives a source. live: the session's channel list (a playlist's guide addresses and channels);
    // offline: a playlist answers from its saved guide only.
    void loadGuide(const iptv::Profile &p, const iptv::LiveCatalog &live, const iptv::LiveChannel &c, bool offline,
                   sources::GuideCallback cb) {
        if (p.isPlaylist()) {
            xmltv.load(p, live, c, offline, std::move(cb));
        } else if (p.isStalker()) {
            stalker.loadShortEpg(p, c.id, std::move(cb));
        } else {
            xtream.loadShortEpg(p, c.id, std::move(cb));
        }
    }

    XmltvService &guides() { return xmltv; }

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
    XmltvService &xmltv;
    MediaBrowserService &media;
    StremioService &addon;
};

#endif // PS4IPTV_APP_SOURCE_SERVICE_H
