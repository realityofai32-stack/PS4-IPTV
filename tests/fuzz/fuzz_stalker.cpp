// libFuzzer target: every Stalker / MAG response parser, the saved-copy decoders and the address helpers, with the
// JSON parser underneath. Invariants: no crash / UB (sanitizers), results within their documented shape.
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "../../src/iptv/stalker.h"

#define REQUIRE(c) do { if (!(c)) { __builtin_trap(); } } while (0)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::string in((const char *) data, size);
    std::string s, err;
    json::Value js;
    stalker::envelope(in, js);
    stalker::parseToken(in, s, err);
    stalker::parseProfile(in);
    iptv::AccountInfo account;
    stalker::parseAccountInfo(in, account);
    std::vector<iptv::Category> cats;
    if (stalker::parseCategories(in, cats, err)) {
        for (const auto &c: cats) {
            REQUIRE(!c.id.empty() && c.id != "*" && !c.name.empty());
        }
    }
    std::vector<iptv::LiveChannel> ch;
    stalker::Page page;
    if (stalker::parseChannels(in, "http://p.example.com", ch, &page, err)) {
        for (const auto &c: ch) {
            REQUIRE(!c.id.empty() && !c.cmd.empty() && c.url.empty());
        }
    }
    std::vector<stalker::VodEntry> entries;
    if (stalker::parseVodPage(in, "http://p.example.com", entries, page, err)) {
        for (const auto &e: entries) {
            REQUIRE(!e.id.empty());
            stalker::toMovie(e);
            stalker::toMovieInfo(e);
            iptv::SeriesInfo si = stalker::seriesFromEntry(e, "v:" + e.id);
            for (const auto &season: si.seasons) {
                for (size_t i = 1; i < season.episodes.size(); i++) {
                    REQUIRE(season.episodes[i - 1].number < season.episodes[i].number);   // sorted, no duplicates
                }
            }
        }
        std::vector<stalker::VodEntry> back;
        REQUIRE(stalker::decodeEntries(stalker::encodeEntries(entries), back, err) && back.size() == entries.size());
    }
    std::vector<stalker::SeasonEntry> seasons;
    if (stalker::parseSeasons(in, seasons, page, err)) {
        for (const auto &x: seasons) {
            REQUIRE(x.number > 0);
        }
    }
    std::vector<iptv::Episode> eps;
    stalker::parseEpisodes(in, "", 1, eps, page, err);
    std::string url;
    if (stalker::parseLink(in, url, err)) {
        REQUIRE(!url.empty() && url.find("://") != std::string::npos && url.find(' ') == std::string::npos);
    }
    stalker::decodeEntries(in, entries, err);
    stalker::decodeChannels(in, ch, err);
    stalker::decodeCategories(in, cats, err);
    // address helpers on the raw input
    stalker::Portal p = stalker::portalFor(in);
    if (p.ok) {
        REQUIRE(!p.endpoints.empty() && p.origin.find("://") != std::string::npos && p.origin.find('@') == std::string::npos);
    }
    std::string mac;
    if (stalker::normalizeMac(in, mac)) {
        REQUIRE(mac.size() == 17 && mac[2] == ':' && mac[14] == ':');
        REQUIRE(stalker::maskedMac(mac).find(mac.substr(9, 2)) == std::string::npos || mac.substr(9, 2) == mac.substr(0, 2)
                || mac.substr(9, 2) == mac.substr(3, 2) || mac.substr(9, 2) == mac.substr(6, 2) || mac.substr(9, 2) == mac.substr(15, 2));
    }
    std::string u = stalker::urlOfCmd(in);
    REQUIRE(u.empty() || u.find(' ') == std::string::npos);
    stalker::cmdHasUrl(in);
    stalker::resolveImage("http://p.example.com", in);
    int64_t t = stalker::parseDateTime(in);
    REQUIRE(t >= 0);
    stalker::linkErrorKey(in);
    return 0;
}
