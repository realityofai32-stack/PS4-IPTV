// Xtream Codes API: URL generation and response parsing (no networking here).

#ifndef PS4IPTV_IPTV_XTREAM_H
#define PS4IPTV_IPTV_XTREAM_H

#include <string>
#include <vector>

#include "models.h"

namespace xtream {

    // /player_api.php?username=..&password=..[&action=..][&extra]
    std::string apiUrl(const iptv::Profile &profile, const std::string &action = "", const std::string &extra = "");

    // /live/<user>/<pass>/<id>.<ext>   ext: "ts" or "m3u8"
    std::string liveUrl(const iptv::Profile &profile, const std::string &streamId, const std::string &ext);

    // /movie/<user>/<pass>/<id>.<container_extension>
    std::string movieUrl(const iptv::Profile &profile, const std::string &streamId, const std::string &ext);

    // /series/<user>/<pass>/<episode_id>.<container_extension>
    std::string seriesUrl(const iptv::Profile &profile, const std::string &episodeId, const std::string &ext);

    // Parses the player_api.php response. httpStatus: HTTP status code of the response.
    // `now` (unix time) is used to detect an expired exp_date the panel still reports as Active.
    iptv::AuthResult parseAuth(long httpStatus, const std::string &body, int64_t now);

    // get_live_categories / get_vod_categories / get_series_categories
    bool parseCategories(const std::string &body, std::vector<iptv::Category> &out, std::string &error);

    // get_live_streams (streamed: large lists never build a DOM). Entries without a stream_id, and repeats of
    // an id already seen, are skipped; everything else is kept, whatever optional metadata is missing.
    // stats (optional) receives the audit counts (raw / kept / rejected by reason / missing fields).
    bool parseLiveStreams(const std::string &body, std::vector<iptv::LiveChannel> &out, std::string &error,
                          iptv::ParseStats *stats = nullptr);

    // get_vod_streams / get_series (streamed). Identity is stream_id / series_id, never the title: entries
    // with the same name and different ids are all kept.
    bool parseVodStreams(const std::string &body, std::vector<iptv::Movie> &out, std::string &error,
                         iptv::ParseStats *stats = nullptr);

    bool parseSeriesList(const std::string &body, std::vector<iptv::Series> &out, std::string &error,
                         iptv::ParseStats *stats = nullptr);

    // get_vod_info / get_series_info (small DOM responses). Missing fields stay empty.
    bool parseVodInfo(const std::string &body, iptv::MovieInfo &out, std::string &error);

    bool parseSeriesInfo(const std::string &body, iptv::SeriesInfo &out, std::string &error);

    // "Orumcek Adam 7 2026" -> ("Orumcek Adam 7", 2026); "(2019)" / "- 2019" suffixes too. A name that is
    // only a year keeps it as the title.
    void splitTitleYear(const std::string &name, std::string &title, int &year);

    // channel name without a provider prefix: "TR: TRT 1" -> "TRT 1", "|DE| ZDF" -> "ZDF" (search alias;
    // the displayed name keeps the prefix). Returns "" when there is no such prefix.
    std::string channelNameWithoutPrefix(const std::string &name);

    // provider placeholder values ("-", "N/A", "null") become ""
    std::string cleanText(const std::string &s);

    // "American Hostage S01-E01" (series "American Hostage") -> ""; "Show S01E02 - Pilot" -> "Pilot"
    std::string cleanEpisodeTitle(const std::string &title, const std::string &seriesName);

    // User-facing text for an auth status.
    std::string authStatusText(iptv::AuthStatus status);
}

#endif // PS4IPTV_IPTV_XTREAM_H
