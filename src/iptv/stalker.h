// Stalker / MAG middleware portal protocol (Ministra and compatible "MAC portals"): addresses, identity,
// request building and response parsing. No networking here (app/stalker_service does the requests): host-testable.
//
// Flow of a compatible portal (JsHttpRequest API on .../portal.php or .../stalker_portal/server/load.php):
//   handshake       type=stb&action=handshake                    -> js.token (Bearer token of the session)
//   get_profile     type=stb&action=get_profile (+ device ids)    -> the box is known / blocked / needs a login
//   account info    type=account_info&action=get_main_info        -> expiry date (optional, many portals omit it)
//   Live TV         type=itv&action=get_genres, get_all_channels
//   Movies          type=vod&action=get_categories, get_ordered_list&category=..&p=..   (paged)
//   Series          type=series&action=get_categories, get_ordered_list (paged); seasons / episodes with movie_id,
//                   or older portals: VOD entries with is_series=1 and a "series" list of episode numbers
//   playback        type=itv|vod&action=create_link&cmd=..[&series=N] -> js.cmd "ffmpeg http://..." (single-use)
// Every request carries the MAC as a cookie and the MAG 250 identity (User-Agent, X-User-Agent); serial number and
// device ids are derived from the MAC like the common MAG emulators do. Portals answer {"js": ...}; anything else
// (HTML, "Authorization failed.") is reported, never guessed.

#ifndef PS4IPTV_IPTV_STALKER_H
#define PS4IPTV_IPTV_STALKER_H

#include <cstdint>
#include <string>
#include <vector>

#include "models.h"
#include "../core/json.h"

namespace stalker {

    // MAG 250 identity sent with every portal request (and as the stream User-Agent)
    extern const char *const USER_AGENT;
    extern const char *const X_USER_AGENT;

    // "00:1a:79:ab:cd:ef", "00-1A-79-AB-CD-EF", "001A79ABCDEF" -> "00:1A:79:AB:CD:EF"; false when not 12 hex digits
    bool normalizeMac(const std::string &input, std::string &out);

    // "00:1A:79:••:••:EF": what the UI may show of a MAC address
    std::string maskedMac(const std::string &mac);

    struct Identity {
        std::string mac;         // normalized
        std::string serial;      // MD5(mac), 13 upper-case hex digits
        std::string deviceId;    // SHA-256(serial)
        std::string deviceId2;   // SHA-256(mac)
        std::string signature;   // SHA-256(serial + mac)
    };

    Identity identity(const std::string &normalizedMac);

    struct Portal {
        bool ok = false;
        std::string error;                     // user-facing reason when !ok
        std::string origin;                    // scheme://host[:port]
        std::string referer;                   // the portal page a MAG box would show: <origin><path>/c/
        std::vector<std::string> endpoints;    // API URLs to try, most likely first
    };

    // Accepts what users paste: "host:port", "http://host:port/c/", ".../stalker_portal/c/", ".../portal.php",
    // ".../server/load.php". Defaults to http.
    Portal portalFor(const std::string &input);

    // <endpoint>?type=..&action=..<params>&JsHttpRequest=1-xml (params: "&key=value" pairs, already encoded)
    std::string apiUrl(const std::string &endpoint, const std::string &type, const std::string &action,
                       const std::string &params = "");

    // request headers ("Name: value"); token "" before the handshake
    std::vector<std::string> headers(const Identity &id, const std::string &referer, const std::string &token);

    // get_profile parameters (device identity, MAG 250 firmware strings); timestamp = unix time
    std::string profileParams(const Identity &id, int64_t timestamp);

    // ------------------------------------------------------------------ responses
    enum class Reply {
        Ok,
        NotJson,         // HTML page, empty body...: not a portal API (wrong address)
        AuthFailed,      // "Authorization failed" / token rejected: handshake again
        NoJs             // JSON without "js"
    };

    // checks the {"js": ...} envelope; `js` gets the js value
    Reply envelope(const std::string &body, json::Value &js);

    bool parseToken(const std::string &body, std::string &token, std::string &error);

    struct ProfileReply {
        bool ok = false;           // the box is known and allowed
        bool blocked = false;      // the portal refuses this MAC (not registered, blocked, expired)
        bool needsLogin = false;   // the portal wants a login / password for this box (not supported)
        std::string message;       // the portal's own text, if any (block_msg / msg)
    };

    ProfileReply parseProfile(const std::string &body);

    // get_main_info: expiry date (end_date / expire_billing_date / tariff_expired_date) and the account status
    void parseAccountInfo(const std::string &body, iptv::AccountInfo &account);

    // get_genres / get_categories: [{id, title|name}], the "*" (All) entry is skipped
    bool parseCategories(const std::string &body, std::vector<iptv::Category> &out, std::string &error);

    struct Page {
        int totalItems = -1;     // -1 when the portal does not say
        int perPage = 0;         // max_page_items
        int count = 0;           // entries in this page
    };

    // get_all_channels / get_ordered_list (itv): {data: [...]} or [...]
    bool parseChannels(const std::string &body, const std::string &origin, std::vector<iptv::LiveChannel> &out,
                       Page *page, std::string &error);

    // a VOD / series list entry, before it becomes a Movie or a Series
    struct VodEntry {
        std::string id;
        std::string name;
        std::string categoryId;
        std::string poster;
        std::string cmd;
        std::string plot;
        std::string director;
        std::string cast;
        std::string genre;
        std::string year;          // as sent ("2019" / "2019-05-12")
        float rating = 0;
        int minutes = 0;
        int64_t added = 0;
        bool isSeries = false;     // is_series = 1 (old style: episodes in `episodes`)
        std::vector<int> episodes; // "series": [1, 2, 3...]
    };

    bool parseVodPage(const std::string &body, const std::string &origin, std::vector<VodEntry> &out, Page &page,
                      std::string &error);

    iptv::Movie toMovie(const VodEntry &e);

    // seriesId: the id the Series catalog uses (series module: the entry id; VOD-module series: "v:<id>")
    iptv::Series toSeries(const VodEntry &e, const std::string &seriesId);

    iptv::MovieInfo toMovieInfo(const VodEntry &e);

    // old style (VOD entry with is_series): one season with the listed episode numbers, all played with the
    // entry's cmd and series=<number>
    iptv::SeriesInfo seriesFromEntry(const VodEntry &e, const std::string &seriesId);

    // series module, step 1: the seasons of a series (movie_id=<id>&season_id=0&episode_id=0)
    struct SeasonEntry {
        std::string id;
        int number = 0;
        std::string name;
        std::string cmd;
        std::vector<int> episodes;   // when the season lists its episode numbers itself
    };

    bool parseSeasons(const std::string &body, std::vector<SeasonEntry> &out, Page &page, std::string &error);

    // series module, step 2: the episodes of a season
    bool parseEpisodes(const std::string &body, const std::string &origin, int season, std::vector<iptv::Episode> &out,
                       Page &page, std::string &error);

    // create_link: the playable URL in js.cmd ("ffmpeg http://..."), or the portal's error
    bool parseLink(const std::string &body, std::string &url, std::string &error);

    // "ffmpeg http://host/..." / "ffrt ..." / "auto ..." / plain URL -> the URL ("" when none)
    std::string urlOfCmd(const std::string &cmd);

    // a command a portal resolves itself (localhost / relative / base64 commands): create_link is needed. Commands
    // that already hold a complete stream URL still go through create_link first (it adds the play token).
    bool cmdHasUrl(const std::string &cmd);

    // logo / poster: absolute URLs as they are, "/path" against the portal origin, anything else is dropped
    std::string resolveImage(const std::string &origin, const std::string &path);

    // "2021-05-01 12:30:00" -> unix time (UTC), 0 when unreadable
    int64_t parseDateTime(const std::string &text);

    // localization key for the portal's create_link error words ("nothing_to_play", "limit", "link_fault")
    std::string linkErrorKey(const std::string &portalError);

    // ------------------------------------------------------------------ saved copies (<dataDir>/cache/<profile>/)
    // A portal has no single list call for Movies / Series (they are paged), so the saved copy is the app's own
    // compact JSON array of what was mapped, read back with the same streaming parser as an Xtream list.
    std::string encodeCategories(const std::vector<iptv::Category> &categories);

    bool decodeCategories(const std::string &text, std::vector<iptv::Category> &out, std::string &error);

    std::string encodeChannels(const std::vector<iptv::LiveChannel> &channels);

    bool decodeChannels(const std::string &text, std::vector<iptv::LiveChannel> &out, std::string &error);

    std::string encodeEntries(const std::vector<VodEntry> &entries);

    bool decodeEntries(const std::string &text, std::vector<VodEntry> &out, std::string &error);
}

#endif // PS4IPTV_IPTV_STALKER_H
