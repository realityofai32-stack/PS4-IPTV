// libFuzzer target: provider / playlist / log input the app parses, with invariants:
//   - JSON (core/json), M3U / M3U8 playlists (iptv/m3u), every Xtream answer parser (iptv/xtream)
//   - the mpv log filter (player/mpv_log_filter) and URL-to-origin redaction for third-party log lines
//   - the HTTP redirect / scheme policy (network/http_policy) and the playlist address shown in the UI
// No crash / UB (sanitizers); bounded outputs; no secret survives where it must not.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/core/json.h"
#include "../../src/iptv/m3u.h"
#include "../../src/iptv/xtream.h"
#include "../../src/network/http_policy.h"
#include "../../src/platform/redact.h"
#include "../../src/player/mpv_log_filter.h"

#define REQUIRE(c) do { if (!(c)) { __builtin_trap(); } } while (0)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::string in((const char *) data, size);
    std::string err;

    json::Value v;
    json::parse(in, v, &err);

    m3u::Limits limits;
    limits.maxEntries = 500;
    m3u::Result r = m3u::parse(in, limits);
    REQUIRE((int) r.entries.size() <= limits.maxEntries);
    for (const auto &e: r.entries) {
        REQUIRE(!e.url.empty());
        m3u::channelId("src", e);
    }
    std::string shown = m3u::displayUrl(in);
    REQUIRE(shown.size() <= in.size() + 16);

    xtream::parseAuth(200, in, 1700000000);
    std::vector<iptv::Category> cats;
    xtream::parseCategories(in, cats, err);
    std::vector<iptv::LiveChannel> live;
    xtream::parseLiveStreams(in, live, err);
    std::vector<iptv::Movie> movies;
    xtream::parseVodStreams(in, movies, err);
    std::vector<iptv::Series> series;
    xtream::parseSeriesList(in, series, err);
    iptv::MovieInfo mi;
    xtream::parseVodInfo(in, mi, err);
    iptv::SeriesInfo si;
    xtream::parseSeriesInfo(in, si, err);

    // log lines: the filter never writes a font-search line after its first few, never crashes on any text
    mpvlog::Filter f;
    uint32_t cp = 0;
    bool font = mpvlog::isFontSearchLine(in, &cp);
    REQUIRE(cp <= 0x10FFFF);
    int written = 0;
    for (int i = 0; i < 20; i++) {
        written += f.admit(in, true, 1.0 + i * 0.01);
    }
    REQUIRE(written <= (font ? mpvlog::Filter::FONT_LINES_KEPT : mpvlog::Filter::LINES_PER_SECOND));
    f.takeSummary(5.0);
    f.endSession();

    // a URL's path and query never survive urlsToOrigins
    // (a URL ends at white space / quotes / brackets: text after them is ordinary text and stays)
    if (in.find_first_of(" \t\r\n\v\f'\"<>)") == std::string::npos && in.find("zq7Token9x") == std::string::npos) {
        std::string line = "Playing: http://h.example.com/" + in + "/zq7Token9x?k=zq7Token9x";
        REQUIRE(redact::urlsToOrigins(line).find("zq7Token9x") == std::string::npos);
    }
    redact::urlsToOrigins(in);

    // redirect policy: only http / https are ever followed
    size_t half = in.size() / 2;
    std::string a = in.substr(0, half), b = in.substr(half);
    httppolicy::Origin o;
    httppolicy::parseOrigin(a, o);
    for (bool cred: {false, true}) {
        for (bool same: {false, true}) {
            httppolicy::Redirect d = httppolicy::checkRedirect(a, b, cred, same);
            if (d == httppolicy::Redirect::Follow) {
                REQUIRE(httppolicy::schemeAllowed(b));
            }
            httppolicy::checkRedirect("https://p.example.com/x", in, cred, same);
        }
    }
    return 0;
}
