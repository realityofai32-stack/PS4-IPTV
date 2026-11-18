// Programme guide (EPG) of the live player's guide panel (host-testable; no networking here).
//
// The app keeps no guide of its own. When the guide panel opens on a channel, the source is asked once for that
// channel's short guide (Xtream get_short_epg, Stalker / MAG itv get_short_epg: the programme on air and the next
// few), and the answer is kept in the session (Cache) until the programme on air ends. Nothing is fetched for a
// panel nobody opened, and nothing is made up:
//   - playlist (M3U) sources: the XMLTV guide their header names (url-tvg / x-tvg-url), downloaded once when a
//     guide panel first needs it and kept as a compact saved guide (iptv/xmltv.h, app/xmltv_service.h); a playlist
//     without one has no guide;
//   - Xtream channels without an epg_channel_id: no guide (the provider has none for them);
//   - entries without a valid start / end or without a title are dropped; an answer with nothing left (or only
//     programmes that already ended) is "no program information".

#ifndef PS4IPTV_IPTV_EPG_H
#define PS4IPTV_IPTV_EPG_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "models.h"

namespace epg {

    struct Programme {
        int64_t start = 0;          // unix time (UTC)
        int64_t stop = 0;
        std::string title;
        std::string description;
    };

    constexpr int REQUEST_LIMIT = 4;           // entries asked from the provider: on air + 3
    constexpr size_t MAX_PROGRAMMES = 8;       // kept per channel
    constexpr size_t MAX_TITLE = 160;          // bytes (UTF-8, cut at a character boundary)
    constexpr size_t MAX_DESCRIPTION = 700;
    constexpr int64_t MAX_DURATION = 24 * 3600;

    enum class Source {
        None,       // no guide from this source / channel: "no program information" without a request
        Xtream,
        Stalker,
        Xmltv       // a playlist channel whose playlist names an XMLTV guide
    };

    // playlistGuide: the playlist (M3U) source names an XMLTV guide (LiveCatalog::guideUrls)
    Source sourceFor(iptv::SourceType type, const iptv::LiveChannel &channel, bool playlistGuide = false);

    // one clean line of provider text: control characters, newlines and runs of spaces become one space,
    // placeholders ("-", "N/A", "null") become "", at most `cap` bytes (cut at a character boundary, an ellipsis
    // added)
    std::string cleanLine(const std::string &raw, size_t cap);

    // Provider answers -> programmes sorted by start time, cleaned (control characters, placeholder values) and
    // validated (see above; overlapping entries keep the earlier one). false: not a guide answer at all (an error
    // page, an auth failure); an empty list is a valid answer.
    bool parseXtream(const std::string &body, std::vector<Programme> &out, std::string &error);

    bool parseStalker(const std::string &body, std::vector<Programme> &out, std::string &error);

    // Xtream sends titles and descriptions base64-encoded: the decoded text when `s` is base64 of valid UTF-8 text
    // (no control characters), else `s` unchanged (panels that send plain text)
    std::string decodeText(const std::string &s);

    // the programme on air at `now` (start <= now < stop) and the first one starting after it (or, with nothing on
    // air, the first one still to come): indices into `list` (sorted by start), -1 = none
    struct NowNext {
        int now = -1;
        int next = -1;
    };

    NowNext nowNext(const std::vector<Programme> &list, int64_t now);

    // how far `p` is at `now`: 0..1
    double progress(const Programme &p, int64_t now);

    // whole minutes until `p` ends (rounded up, 0 once it ended)
    int minutesLeft(const Programme &p, int64_t now);

    // The guide answers of this session by channel id: bounded (the least recently used answer goes first), main
    // thread only.
    class Cache {

    public:

        static constexpr size_t MAX_CHANNELS = 48;
        static constexpr int64_t READY_FOR = 30 * 60;     // a guide is asked again after at most this long ...
        static constexpr int64_t MIN_READY_FOR = 60;      // ... or when its programme on air ends (at least a minute)
        static constexpr int64_t EMPTY_FOR = 15 * 60;     // "no program information" is asked again after
        static constexpr int64_t FAILED_FOR = 60;         // a failed request is tried again after
        static constexpr int64_t LOADING_FOR = 45;        // a request without an answer counts as lost after

        enum class State {
            Missing,    // never asked
            Loading,    // a request is running
            Ready,      // programmes to show
            Empty,      // the provider has no programme information for the channel
            Failed      // the request failed (network, an unreadable answer)
        };

        struct Entry {
            State state = State::Missing;
            std::vector<Programme> programmes;   // Ready: still running or to come at `until`'s computation
            int64_t until = 0;                   // the answer is asked again after this (Loading: lost after)
            uint64_t used = 0;
        };

        // the entry of a channel (state Missing when none); counts as a use
        const Entry &get(const std::string &channelId);

        // a request is due: never asked, or the answer expired, and no request is running
        bool due(const std::string &channelId, int64_t now) const;

        void loading(const std::string &channelId, int64_t now);

        // a provider answer; nothing running or to come (an empty list, only programmes that ended): Empty
        void store(const std::string &channelId, std::vector<Programme> programmes, int64_t now);

        void failed(const std::string &channelId, int64_t now);

        size_t size() const { return entries.size(); }

    private:

        Entry &touch(const std::string &channelId);

        void trim();

        std::unordered_map<std::string, Entry> entries;
        uint64_t clock = 0;
        Entry missing;
    };
}

#endif // PS4IPTV_IPTV_EPG_H
