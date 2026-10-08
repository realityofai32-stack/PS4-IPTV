// Extended M3U / M3U8 channel playlists (host-testable, no networking here).
//
// A playlist source is a list of Live / Playlist channels:
//
//   #EXTM3U [url-tvg="..."]
//   #EXTINF:-1 tvg-id="trt1.tr" tvg-name="TRT 1" tvg-logo="http://.../trt1.png" group-title="Turkey",TRT 1 HD
//   #EXTGRP:Turkey                                    (category when group-title is absent)
//   #EXTVLCOPT:http-user-agent=Mozilla/5.0 ...        (User-Agent for this entry's stream)
//   http://host:8080/live/stream.ts
//
// Entries are never classified as movies or series: the playlist only lists channels (Xtream stays the
// source of Movies / Series). An entry's URL may itself be an HLS (.m3u8) media URL: it is played as given.
// A file that is an HLS media / master playlist (#EXT-X-TARGETDURATION, #EXT-X-STREAM-INF...) is not a
// channel list and is rejected with Error::HlsMedia instead of turning its segments into "channels".
//
// Robustness: UTF-8 with or without BOM, LF / CRLF / CR line ends, blank lines, surrounding white space,
// quoted / unquoted / single-quoted attributes, commas inside names, missing optional attributes. A broken
// entry is skipped on its own and counted in Stats; it never rejects the rest of the playlist. Invalid UTF-8
// in names is replaced (the text stays valid UTF-8 for the UI and the history file).
//
// Identity (channelId): favorites, Recently Watched and search refer to a channel by a stable local id
// derived from the source id + tvg-id + group + name (not the URL: providers rotate stream tokens on every
// refresh). The first entry with a given base keeps the plain id; later entries with the same base get a
// suffix from their stream URL (scheme/host/path, no query, no credentials) and, if that is also the same,
// their occurrence number. Ids are therefore unique per playlist, independent between sources, and stay the
// same across app restarts and ordinary refreshes (channels added, removed, reordered, tokens rotated).

#ifndef PS4IPTV_IPTV_M3U_H
#define PS4IPTV_IPTV_M3U_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "catalog.h"
#include "models.h"

namespace m3u {

    struct Entry {
        std::string name;        // display name (the EXTINF title; tvg-name / URL when it is missing)
        std::string tvgId;
        std::string tvgName;
        std::string logo;        // tvg-logo
        std::string group;       // group-title, else #EXTGRP, else "" (Uncategorized)
        std::string url;         // media URL, played as-is
        std::string userAgent;   // #EXTVLCOPT:http-user-agent= / user-agent="..." ("" = none)
        int number = 0;          // tvg-chno (0 = none)
    };

    // What the parser saw. Never contains URLs, names or other playlist text.
    struct Stats {
        size_t bytes = 0;
        int lines = 0;
        bool header = false;          // #EXTM3U present
        int extinf = 0;               // #EXTINF lines
        int plainUrls = 0;            // entries without #EXTINF (plain M3U): named after their URL
        int channels = 0;             // entries kept
        int groups = 0;               // distinct groups (Uncategorized not counted)
        int uncategorized = 0;        // channels without a group
        int withLogo = 0;
        int withoutLogo = 0;
        int withTvgId = 0;
        int userAgents = 0;           // entries with their own User-Agent
        int hls = 0;                  // media URLs ending in .m3u8
        int https = 0;                // https:// media URLs (the player has no TLS: shown, not playable)
        int otherProtocols = 0;       // rtmp://, udp://, rtsp://... (played if the player supports them)
        int duplicateIds = 0;         // identity collisions resolved with a suffix
        // skipped entries
        int missingUrl = 0;           // #EXTINF with no media URL before the next entry / the end
        int invalidUrl = 0;           // a media line that is not scheme://host...
        int overLimit = 0;            // entries beyond Limits::maxEntries
        // kept, with defaults
        int malformedExtinf = 0;      // no title comma / unterminated quote: kept when a URL follows
        int unnamed = 0;              // no title and no tvg-name: named after the URL
        int unknownDirectives = 0;    // #EXT... lines this parser does not use (ignored)
        int invalidUtf8 = 0;          // names / groups repaired

        int skipped() const { return missingUrl + invalidUrl + overLimit; }
    };

    enum class Error {
        None,
        Empty,          // no data / only blank lines
        NotPlaylist,    // no entries at all (an HTML page, JSON, an error message...)
        HlsMedia,       // the URL is a single HLS stream (media / master playlist), not a channel list
        Utf16           // UTF-16 text (UTF-8 is required)
    };

    struct Limits {
        int maxEntries = 200000;
        size_t maxLineBytes = 16384;     // longer lines are skipped (counted as invalid)
    };

    struct Result {
        Error error = Error::None;
        std::vector<Entry> entries;   // playlist order
        Stats stats;

        bool ok() const { return error == Error::None && !entries.empty(); }
    };

    Result parse(std::string_view text, const Limits &limits = Limits());

    // ------------------------------------------------------------------ identity / catalog
    // stable local channel id (see the header comment): "m" + 16 hex digits [+ "-" + 8 hex] [+ "-" + n]
    std::string channelId(const std::string &sourceId, const Entry &entry);

    // scheme://host[:port]/path of a media URL: lower-case scheme/host, no user info, query or fragment
    std::string stableUrlKey(const std::string &url);

    // Channels + categories for iptv::LiveCatalog: one category per group in order of first appearance,
    // then Uncategorized (iptv::UNCATEGORIZED_ID) when some entries have none. Ids are unique (see above).
    void toChannels(const std::string &sourceId, const std::vector<Entry> &entries,
                    std::vector<iptv::Category> &categories, std::vector<iptv::LiveChannel> &channels,
                    Stats *stats = nullptr);

    // ------------------------------------------------------------------ loading (worker thread)
    // Playlist Info / diagnostics of the loaded playlist: counts and timings, never URLs or names
    struct Info {
        Stats stats;
        int categories = 0;          // incl. Uncategorized
        bool fromCache = false;
        int64_t savedAt = 0;         // unix time the playlist was downloaded (0 = unknown)
        double parseMs = 0;          // parse + channel ids
        double indexMs = 0;          // catalog indices + search index
        size_t indexBytes = 0;
    };

    // parse + channels + catalog (incl. its search index). On success `out` holds the playlist; on failure it
    // is left untouched (the caller keeps the playlist it has) and the error says why.
    Error build(const std::string &sourceId, std::string_view body, iptv::LiveCatalog &out, Info &info,
                const Limits &limits = Limits());

    // ------------------------------------------------------------------ URLs
    // lower-case scheme of "scheme://..." ("" when the text is not such a URL)
    std::string schemeOf(std::string_view url);

    // http:// or https:// with a host
    bool isHttpUrl(const std::string &url);

    // Local playlists: only files in the app's own data folder, /data/PS4IPTV/playlists/<name>.m3u[8]
    // (as a path or a file:// URL). No other folder, no "..". Returns the path, "" when not allowed.
    std::string localPlaylistPath(const std::string &input);

    extern const char *const LOCAL_PLAYLIST_DIR;   // "/data/PS4IPTV/playlists/"

    // What the user may see of a playlist / media URL: "host[:port]/…/name.m3u" with no user info, query or
    // credential-like path segments. Local files: "playlists/<name>".
    std::string displayUrl(const std::string &url);
}

#endif // PS4IPTV_IPTV_M3U_H
