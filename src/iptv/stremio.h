// Stremio add-ons (host-testable; no networking here). An add-on is a plain HTTP(S) JSON service
// (https://github.com/Stremio/stremio-addon-sdk/blob/master/docs/protocol.md); V7 uses one add-on per source, with
// no Stremio code, browser or runtime:
//
//   manifest   GET <addon>/manifest.json: name, resources, types, catalogs
//   catalog    GET <addon>/catalog/<type>/<id>[/skip=<n>].json -> {"metas":[...]}: Movies / Series lists; every
//              movie / series catalog is a category (catalogs that need a search word or another required value are
//              left out: there is nothing to list without it). At most MAX_PAGES pages per catalog, MAX_ITEMS titles.
//   meta       GET <addon>/meta/<type>/<id>.json -> {"meta":{...,"videos":[...]}}: details and a series' episodes
//   stream     GET <addon>/stream/<type>/<id>.json -> {"streams":[...]}: asked when a title plays. Only direct
//              http(s) addresses are used; torrents (infoHash), YouTube ids and "open in a browser" links are not
//              playable here and are skipped, and so are streams that need request headers other than a User-Agent
//              (the player cannot send them).
//
// A configured add-on carries its configuration (sometimes an API key) in its address: the address is never shown
// or logged in full (m3u::displayUrl: host and "manifest.json"). V7 bundles and suggests no add-on.

#ifndef PS4IPTV_IPTV_STREMIO_H
#define PS4IPTV_IPTV_STREMIO_H

#include <string>
#include <vector>

#include "models.h"

namespace stremio {

    constexpr int MAX_CATALOGS = 20;     // catalogs listed per type
    constexpr int MAX_PAGES = 5;         // requests per catalog
    constexpr int MAX_ITEMS = 10000;     // titles per type

    // the manifest address of what was entered: "stremio://" install links become https, a missing
    // "/manifest.json" is added. "" + error when it is not an http(s) address.
    std::string manifestUrl(const std::string &input, std::string &error);

    // the add-on's base address (the manifest address without "/manifest.json")
    std::string baseOf(const std::string &manifestUrl);

    struct Catalog {
        std::string type;      // "movie" / "series"
        std::string id;
        std::string name;
        bool pages = false;    // supports "skip"
    };

    struct Manifest {
        std::string id;
        std::string name;
        std::string version;
        std::vector<Catalog> catalogs;   // movie / series catalogs that list without a required value
        bool meta = false;               // offers details (meta)
        bool stream = false;             // offers streams
        bool configurationRequired = false;
    };

    // false: not an add-on manifest
    bool parseManifest(const std::string &body, Manifest &out);

    std::string catalogUrl(const std::string &base, const Catalog &catalog, int skip);

    std::string metaUrl(const std::string &base, const std::string &type, const std::string &id);

    std::string streamUrl(const std::string &base, const std::string &type, const std::string &id);

    // the category id of a catalog ("movie/top")
    std::string categoryId(const Catalog &catalog);

    // a catalog page -> movies / series (titles without an id are skipped); count: titles on the page
    bool parseMovies(const std::string &body, const std::string &categoryId, std::vector<iptv::Movie> &out, int &count);

    bool parseSeries(const std::string &body, const std::string &categoryId, std::vector<iptv::Series> &out, int &count);

    bool parseMovieMeta(const std::string &body, iptv::MovieInfo &out);

    // a series' meta: details + episodes (videos) as seasons (season 0 = specials; sorted; videos without an id
    // skipped)
    bool parseSeriesMeta(const std::string &body, iptv::SeriesInfo &out);

    struct Stream {
        std::string url;
        std::string userAgent;   // behaviorHints.proxyHeaders.request["User-Agent"]
        std::string label;       // name / title, for the log (counts only) and the error text
    };

    struct Streams {
        std::vector<Stream> playable;   // direct http(s) addresses, in the add-on's order
        int skipped = 0;                // torrents, YouTube, external links, streams needing other headers
    };

    bool parseStreams(const std::string &body, Streams &out);

    // "120 min" / "1h 30min" / "2h" -> seconds (0 when unreadable)
    int runtimeSeconds(const std::string &runtime);

    // "2019", "2019-2021", "2019–" -> 2019 (0 when none)
    int yearOf(const std::string &releaseInfo);
}

#endif // PS4IPTV_IPTV_STREMIO_H
