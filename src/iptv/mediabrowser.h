// Jellyfin / Emby media servers (host-testable; no networking here). Both speak the same REST API (Jellyfin is a
// fork of Emby); one implementation serves both. app/mediabrowser_service.cpp makes the requests.
//
// What V7 uses, and how it maps:
//   sign-in          GET /System/Info/Public (is it a Jellyfin / Emby server; the address after redirects), then
//                    POST /Users/AuthenticateByName {"Username","Pw"} -> AccessToken + User.Id. Every later request
//                    carries the token in the Authorization / X-Emby-Authorization header ("MediaBrowser ...").
//   categories       GET /Users/{user}/Views: the "movies" libraries are Movies' categories, the "tvshows" ones
//                    Series' categories (other library kinds - music, photos, live TV - are not shown).
//   lists            GET /Users/{user}/Items?ParentId={library}&IncludeItemTypes=Movie|Series&Recursive=true, paged
//                    (PAGE items a request, at most MAX_ITEMS per kind).
//   details          GET /Users/{user}/Items/{id} (overview, genres, people, runtime, media streams, the user's
//                    saved position); a series' episodes: GET /Shows/{id}/Episodes?UserId={user}.
//   playback         the file as it is, never transcoded: /Videos/{id}/stream?static=true with the token as a query
//                    value (the player cannot send headers); played by the same player as every movie / episode.
//   images           /Items/{id}/Images/Primary|Backdrop?tag=... (no token needed).
//   progress         POST /Sessions/Playing, /Sessions/Playing/Progress, /Sessions/Playing/Stopped: the server
//                    keeps the position (and marks the item played near its end, as for every client); its saved
//                    position is offered as Resume when the console has none of its own.
//
// Nothing here is logged with the token or the password; the service registers both as secrets (platform/redact).

#ifndef PS4IPTV_IPTV_MEDIABROWSER_H
#define PS4IPTV_IPTV_MEDIABROWSER_H

#include <cstdint>
#include <string>
#include <vector>

#include "models.h"

namespace mediabrowser {

    constexpr int PAGE = 500;            // items per list request
    constexpr int MAX_ITEMS = 50000;     // movies / series kept per catalog

    // the server address as entered -> scheme://host[:port][/prefix]: a pasted web client address
    // ("http://host:8096/web/index.html#!/home") loses its "/web/..." part. "" + error when it is not an address.
    std::string serverBase(const std::string &input, std::string &error);

    // a stable device id for this console and source (no console identifier: derived from the profile id)
    std::string deviceId(const std::string &profileId);

    // request headers: the MediaBrowser authorization in both header names the servers read (+ the token)
    std::vector<std::string> headers(const std::string &deviceId, const std::string &appVersion,
                                     const std::string &token);

    std::string loginBody(const std::string &user, const std::string &password);

    struct ServerInfo {
        std::string name;
        std::string version;
        std::string product;    // "Jellyfin Server" / "Emby Server"
        std::string id;
    };

    // false: not a Jellyfin / Emby server's public information
    bool parsePublicInfo(const std::string &body, ServerInfo &out);

    struct Login {
        std::string token;
        std::string userId;
        std::string userName;
        std::string serverId;
    };

    bool parseLogin(const std::string &body, Login &out);

    struct Library {
        std::string id;
        std::string name;
        std::string type;       // "movies", "tvshows", ...
    };

    bool parseViews(const std::string &body, std::vector<Library> &out);

    // path + query of one page of a library's movies / series
    std::string itemsPath(const std::string &userId, const std::string &libraryId, bool series, int start, int limit);

    struct Page {
        int total = 0;      // TotalRecordCount
        int items = 0;      // items in this page
    };

    bool parseMovies(const std::string &body, const std::string &base, const std::string &categoryId,
                     std::vector<iptv::Movie> &out, Page &page);

    bool parseSeries(const std::string &body, const std::string &base, const std::string &categoryId,
                     std::vector<iptv::Series> &out, Page &page);

    bool parseMovieInfo(const std::string &body, const std::string &base, iptv::MovieInfo &out);

    // a series' own item (overview, people...): fills `out` (the list entry's fields stay when the item lacks them)
    bool parseSeriesDetail(const std::string &body, const std::string &base, iptv::Series &out);

    // a series' episodes -> its seasons (sorted, only seasons with episodes); virtual / missing episodes are skipped
    bool parseEpisodes(const std::string &body, const std::string &base, iptv::SeriesInfo &out);

    std::string streamUrl(const std::string &base, const std::string &token, const std::string &itemId);

    std::string imageUrl(const std::string &base, const std::string &itemId, const char *kind, const std::string &tag,
                         int maxHeight);

    // "2023-04-12T10:22:33.1234567Z" / "+03:00" -> unix time (0 when unreadable)
    int64_t parseDate(const std::string &iso);

    // "mkv" -> "mkv", "mov,mp4,m4a,3gp,3g2,mj2" -> "mp4"; "" -> "mkv"
    std::string containerOf(const std::string &raw);

    // progress reports
    enum class Report {
        Start,
        Progress,
        Stop
    };

    const char *reportPath(Report report);

    std::string reportBody(const std::string &itemId, const std::string &playSessionId, double positionSeconds,
                           bool paused);

    // the saved copy of a catalog (a flat JSON array: categories, then items)
    std::string encodeMovies(const std::vector<iptv::Category> &categories, const std::vector<iptv::Movie> &movies);

    bool decodeMovies(const std::string &text, std::vector<iptv::Category> &categories, std::vector<iptv::Movie> &movies);

    std::string encodeSeries(const std::vector<iptv::Category> &categories, const std::vector<iptv::Series> &series);

    bool decodeSeries(const std::string &text, std::vector<iptv::Category> &categories, std::vector<iptv::Series> &series);
}

#endif // PS4IPTV_IPTV_MEDIABROWSER_H
