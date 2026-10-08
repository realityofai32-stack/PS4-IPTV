// IPTV data model.

#ifndef PS4IPTV_IPTV_MODELS_H
#define PS4IPTV_IPTV_MODELS_H

#include <cstdint>
#include <string>
#include <vector>

namespace iptv {

    // Kind of IPTV source a profile signs in to (profiles.json schema 2: "sourceType").
    enum class SourceType {
        Xtream,     // Xtream Codes API: Live TV, Movies, Series
        M3u         // M3U / M3U8 channel playlist: Live TV only (no Movies / Series guessing)
    };

    // "xtream" / "m3u" (profiles.json values)
    const char *sourceTypeKey(SourceType type);

    // false for unknown values
    bool sourceTypeFromKey(const std::string &key, SourceType &out);

    // One source (profile). Xtream sources use server/username/password; playlist sources use playlistUrl
    // (http(s):// URL, or a file under /data/PS4IPTV/playlists/) and an optional User-Agent.
    struct Profile {
        std::string id;
        SourceType type = SourceType::Xtream;
        std::string name;
        std::string server;     // Xtream: normalized base URL, e.g. http://host:8080
        std::string username;
        std::string password;
        std::string playlistUrl;   // M3U: as entered (may contain credentials / tokens: never shown or logged)
        std::string userAgent;     // M3U: custom User-Agent for the playlist and its streams ("" = default)
        int playlistChannels = -1; // M3U: channels in the last playlist loaded (-1 = never loaded)
        int64_t createdAt = 0;
        int64_t lastUsedAt = 0;
        std::string lastStatus; // short user-facing status of the last connection attempt

        bool isPlaylist() const { return type == SourceType::M3u; }
    };

    enum class ContentType {
        Live,
        Movie,
        Series
    };

    struct Category {
        std::string id;
        std::string name;
        std::string parentId;
    };

    // A Live TV channel of either source type. `id` is its identity for favorites, history and search:
    //   Xtream:   the provider's stream_id (playback URL built by xtream::liveUrl)
    //   playlist: a stable local id derived from the source and the entry (m3u::channelId), never a stream_id;
    //             `url` is the media URL to play as-is
    struct LiveChannel {
        std::string id;
        std::string name;
        std::string categoryId;
        std::string icon;
        std::string epgId;       // Xtream epg_channel_id / playlist tvg-id
        std::string url;         // playlist channels only ("" for Xtream)
        std::string userAgent;   // playlist #EXTVLCOPT:http-user-agent ("" = the source default)
        int64_t added = 0;
        int num = 0;             // channel number (Xtream num / playlist tvg-chno), 0 = none
        bool archive = false;

        bool isPlaylist() const { return !url.empty(); }
    };

    // get_vod_streams entry (kept compact: catalogs hold tens of thousands of these)
    struct Movie {
        std::string streamId;
        std::string name;          // as sent by the provider, e.g. "Orumcek Adam 7 2026"
        std::string title;         // name without a trailing year: "Orumcek Adam 7"
        std::string categoryId;
        std::string icon;          // stream_icon (poster)
        std::string extension;     // container_extension ("mkv"); playback URL suffix
        float rating = 0;          // 0..10, 0 = unknown
        int year = 0;              // from the name, 0 = unknown
        int64_t added = 0;
    };

    // get_series entry
    struct Series {
        std::string seriesId;
        std::string name;
        std::string title;
        std::string categoryId;
        std::string cover;
        std::string plot;
        std::string cast;
        std::string director;
        std::string genre;
        std::string releaseDate;
        float rating = 0;
        int year = 0;
        int runtimeMinutes = 0;    // episode_run_time
        int64_t lastModified = 0;
    };

    // What a list parser saw (get_vod_streams / get_series / get_live_streams). Items are kept whenever they
    // have an identity (stream_id / series_id); missing optional metadata is only counted.
    struct ParseStats {
        int raw = 0;                // top-level array elements in the response
        int parsed = 0;             // items kept
        // rejected (parsed + rejected == raw)
        int rejectedNotObject = 0;
        int rejectedMissingId = 0;
        int rejectedDuplicateId = 0;   // same id as an earlier entry: the first one is kept
        // kept, with defaults
        int missingName = 0;        // shown as "Movie <id>" / "Series <id>"
        int missingCategory = 0;    // no category_id: listed under Uncategorized
        int missingPoster = 0;
        int missingExtension = 0;   // container_extension missing: the player falls back to "mkv"
        int titleYearAliases = 0;   // movies whose name ends with a year (searchable with and without it)

        int rejected() const { return rejectedNotObject + rejectedMissingId + rejectedDuplicateId; }
    };

    // Counts shown in Movies / Series > Options > Catalog info and logged after every load. Never contains
    // credentials or URLs.
    struct CatalogDiagnostics {
        ParseStats parse;
        int cached = -1;            // items in the saved copy (-1: not saved / unknown)
        int visible = 0;            // items reachable from "All"
        int indexed = 0;            // search index entries
        int uncategorized = 0;      // items whose category is missing or not in the category list
        int categories = 0;         // categories with items (+ Uncategorized when used)
        bool fromCache = false;
        double parseMs = 0;
        double indexMs = 0;
        double sortMs = 0;
        size_t indexBytes = 0;

        int dropped() const { return parse.raw - visible; }
    };

    // ffprobe-style stream summary the panel stores per movie/episode (may be empty)
    struct MediaSummary {
        int width = 0;
        int height = 0;
        std::string videoCodec;    // "h264"
        std::string audioCodec;    // "ac3"
        int audioChannels = 0;
        std::string audioLanguage; // of the one audio stream the panel reports
        int bitrateKbps = 0;
    };

    // get_vod_info: detail fields. Standard Xtream fields are read when present; many panels (incl. the one
    // used in testing) only send duration, rating, images and stream info.
    struct MovieInfo {
        std::string streamId;
        std::string plot;
        std::string genre;
        std::string director;
        std::string cast;
        std::string releaseDate;
        std::string coverBig;
        std::string backdrop;
        std::string tmdbId;
        float rating = 0;
        int durationSeconds = 0;
        MediaSummary media;
    };

    struct Episode {
        std::string id;            // stream id for /series/<u>/<p>/<id>.<ext>
        int season = 0;
        int number = 0;            // episode_num
        std::string title;         // cleaned ("" when the provider title was only "<series> S01-E01")
        std::string extension;
        std::string image;         // info.movie_image
        std::string plot;
        int durationSeconds = 0;
        float rating = 0;
        MediaSummary media;
    };

    struct Season {
        int number = 0;
        std::string name;          // provider name, e.g. "1. Sezon"
        std::string cover;
        std::vector<Episode> episodes;   // sorted by episode number
    };

    // get_series_info
    struct SeriesInfo {
        std::string seriesId;
        Series series;             // "info" (same fields as the list entry)
        std::vector<Season> seasons;   // only seasons that have episodes, sorted by number
    };

    enum class AuthStatus {
        Ok,
        InvalidCredentials,
        Expired,
        Banned,
        Disabled,
        Refused,          // HTTP 403 from the API
        ServerError,      // HTTP 5xx / unexpected HTTP status
        Malformed,        // not an Xtream response
        Network           // could not reach the server (set by the caller)
    };

    struct AccountInfo {
        std::string status;          // as reported: Active / Expired / Banned / Disabled / ...
        std::string message;
        int64_t expiresAt = 0;       // unix time, 0 = never / unknown
        bool trial = false;
        int maxConnections = 0;
        int activeConnections = 0;
        std::vector<std::string> outputFormats;
        // server_info
        std::string serverUrl;
        std::string serverPort;
        std::string serverHttpsPort;
        std::string serverProtocol;
        std::string timezone;
        int64_t serverTime = 0;
    };

    struct AuthResult {
        AuthStatus status = AuthStatus::Malformed;
        AccountInfo account;
        std::string detail;          // diagnostic detail for the log (never contains credentials)
    };

    const char *contentTypeName(ContentType type);
}

#endif // PS4IPTV_IPTV_MODELS_H
