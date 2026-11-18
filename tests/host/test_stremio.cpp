// Stremio add-ons (iptv/stremio.h): add-on addresses, manifests (catalogs that list by themselves, resources,
// configuration), catalog pages, metas (details, episodes), streams (only direct http(s) addresses; torrents, YouTube,
// external links and streams needing other headers are skipped), runtimes and years; the "stremio" source type.

#include <string>
#include <vector>

#include "check.h"
#include "../../src/iptv/stremio.h"
#include "../../src/iptv/xtream.h"

TEST(stremio_addresses_and_manifest) {
    std::string err;
    CHECK_EQ(stremio::manifestUrl("https://addon.example.com/manifest.json", err),
             std::string("https://addon.example.com/manifest.json"));
    CHECK_EQ(stremio::manifestUrl("stremio://addon.example.com/cfg123/manifest.json", err),
             std::string("https://addon.example.com/cfg123/manifest.json"));
    CHECK_EQ(stremio::manifestUrl("  https://addon.example.com/path/  ", err),
             std::string("https://addon.example.com/path/manifest.json"));
    CHECK_EQ(stremio::manifestUrl("http://192.168.1.5:7000", err), std::string("http://192.168.1.5:7000/manifest.json"));
    CHECK_EQ(stremio::manifestUrl("https://addon.example.com/configure#x", err),
             std::string("https://addon.example.com/configure/manifest.json"));
    CHECK(stremio::manifestUrl("addon.example.com", err).empty() && !err.empty());
    CHECK(stremio::manifestUrl("ftp://addon.example.com/manifest.json", err).empty());
    CHECK(stremio::manifestUrl("https:///manifest.json", err).empty());
    CHECK_EQ(stremio::baseOf("https://addon.example.com/cfg/manifest.json"), std::string("https://addon.example.com/cfg"));

    stremio::Manifest m;
    CHECK(stremio::parseManifest(
            "{\"id\":\"org.example.addon\",\"version\":\"1.2.0\",\"name\":\"Example\","
            "\"resources\":[\"catalog\",{\"name\":\"meta\",\"types\":[\"movie\",\"series\"]},{\"name\":\"stream\","
            "\"types\":[\"movie\"],\"idPrefixes\":[\"tt\"]}],\"types\":[\"movie\",\"series\"],"
            "\"catalogs\":[{\"type\":\"movie\",\"id\":\"top\",\"name\":\"Top\",\"extra\":[{\"name\":\"skip\"},"
            "{\"name\":\"genre\",\"options\":[\"Drama\"]}]},"
            "{\"type\":\"movie\",\"id\":\"search\",\"name\":\"Search\",\"extra\":[{\"name\":\"search\",\"isRequired\":true}]},"
            "{\"type\":\"series\",\"id\":\"shows\",\"extraSupported\":[\"skip\"]},"
            "{\"type\":\"series\",\"id\":\"old\",\"extraRequired\":[\"genre\"]},"
            "{\"type\":\"tv\",\"id\":\"channels\",\"name\":\"TV\"}]}", m));
    CHECK(m.id == "org.example.addon" && m.name == "Example" && m.version == "1.2.0" && m.meta && m.stream);
    CHECK(!m.configurationRequired);
    CHECK_EQ(m.catalogs.size(), (size_t) 2);   // top, shows (search / required genre / tv left out)
    if (m.catalogs.size() == 2) {
        CHECK(m.catalogs[0].id == "top" && m.catalogs[0].pages && m.catalogs[0].type == "movie");
        CHECK(m.catalogs[1].id == "shows" && m.catalogs[1].pages && m.catalogs[1].name == "shows");
        CHECK_EQ(stremio::catalogUrl("https://a.example.com", m.catalogs[0], 0),
                 std::string("https://a.example.com/catalog/movie/top.json"));
        CHECK_EQ(stremio::catalogUrl("https://a.example.com", m.catalogs[0], 100),
                 std::string("https://a.example.com/catalog/movie/top/skip=100.json"));
        CHECK_EQ(stremio::categoryId(m.catalogs[1]), std::string("series/shows"));
    }
    CHECK_EQ(stremio::streamUrl("https://a.example.com", "series", "tt0944947:1:2"),
             std::string("https://a.example.com/stream/series/tt0944947%3A1%3A2.json"));
    CHECK_EQ(stremio::metaUrl("https://a.example.com", "movie", "tt0111161"),
             std::string("https://a.example.com/meta/movie/tt0111161.json"));
    stremio::Manifest c;
    CHECK(stremio::parseManifest("{\"id\":\"x\",\"resources\":[\"stream\"],\"catalogs\":[],"
                                 "\"behaviorHints\":{\"configurable\":true,\"configurationRequired\":true}}", c));
    CHECK(c.configurationRequired && c.catalogs.empty() && c.stream && !c.meta);
    CHECK(!stremio::parseManifest("<html>not an add-on</html>", c));
    CHECK(!stremio::parseManifest("{\"name\":\"no id\",\"resources\":[]}", c));

    iptv::SourceType t;
    CHECK(iptv::sourceTypeFromKey("stremio", t) && t == iptv::SourceType::Stremio);
    iptv::Profile p;
    p.type = iptv::SourceType::Stremio;
    CHECK(p.isAddon() && p.isNewerType() && p.hasVod() && !p.hasLive() && !p.supportsDownloads());
}

TEST(stremio_catalogs_metas_streams) {
    std::vector<iptv::Movie> movies;
    int n = 0;
    CHECK(stremio::parseMovies("{\"metas\":[{\"id\":\"tt0111161\",\"type\":\"movie\",\"name\":\"The Film\","
                               "\"poster\":\"https://img.example.com/p.jpg\",\"imdbRating\":\"9.3\",\"releaseInfo\":\"1994\"},"
                               "{\"id\":\"tt2\",\"type\":\"series\",\"name\":\"Not a movie\"},{\"name\":\"no id\"},"
                               "{\"id\":\"tt3\",\"name\":\"Typeless\",\"year\":2001,\"imdbRating\":7}]}", "movie/top", movies, n));
    CHECK_EQ(n, 4);
    CHECK_EQ(movies.size(), (size_t) 2);
    if (movies.size() == 2) {
        CHECK(movies[0].streamId == "tt0111161" && movies[0].title == "The Film" && movies[0].year == 1994
              && movies[0].rating > 9.2f && movies[0].icon == "https://img.example.com/p.jpg"
              && movies[0].categoryId == "movie/top");
        CHECK(movies[1].year == 2001 && movies[1].rating == 7.0f);
    }
    CHECK(!stremio::parseMovies("{\"error\":\"nope\"}", "c", movies, n));
    std::vector<iptv::Series> series;
    CHECK(stremio::parseSeries("{\"metas\":[{\"id\":\"tt0944947\",\"type\":\"series\",\"name\":\"Show\","
                               "\"releaseInfo\":\"2011–2019\",\"description\":\"Kings.\"}]}", "series/shows", series, n));
    CHECK(series.size() == 1 && series[0].year == 2011 && series[0].plot == "Kings.");

    iptv::MovieInfo mi;
    CHECK(stremio::parseMovieMeta("{\"meta\":{\"id\":\"tt0111161\",\"type\":\"movie\",\"name\":\"The Film\","
                                  "\"description\":\"Hope.\",\"genres\":[\"Drama\",\"Crime\"],\"director\":[\"D\"],"
                                  "\"cast\":[\"A\",\"B\"],\"released\":\"1994-09-23T00:00:00.000Z\",\"runtime\":\"142 min\","
                                  "\"background\":\"https://img.example.com/b.jpg\",\"imdbRating\":\"9.3\"}}", mi));
    CHECK(mi.plot == "Hope." && mi.genre == "Drama, Crime" && mi.director == "D" && mi.cast == "A, B"
          && mi.releaseDate == "1994-09-23" && mi.durationSeconds == 8520 && !mi.backdrop.empty());
    CHECK(!stremio::parseMovieMeta("{\"meta\":null}", mi));

    iptv::SeriesInfo si;
    CHECK(stremio::parseSeriesMeta(
            "{\"meta\":{\"id\":\"tt0944947\",\"type\":\"series\",\"name\":\"Show\",\"videos\":["
            "{\"id\":\"tt0944947:1:2\",\"title\":\"Two\",\"season\":1,\"episode\":2,\"thumbnail\":\"https://img.example.com/2.jpg\"},"
            "{\"id\":\"tt0944947:1:1\",\"title\":\"One\",\"season\":1,\"episode\":1,\"overview\":\"Begins.\"},"
            "{\"id\":\"tt0944947:0:1\",\"title\":\"Special\",\"season\":0,\"episode\":1},"
            "{\"id\":\"kitsu:1:5\",\"title\":\"No season\",\"episode\":5},{\"title\":\"no id\"}]}}", si));
    CHECK_EQ(si.seasons.size(), (size_t) 2);   // 0 (specials) and 1 (incl. the video without a season)
    if (si.seasons.size() == 2) {
        CHECK(si.seasons[0].number == 0 && si.seasons[0].episodes.size() == 1);
        const iptv::Season &s1 = si.seasons[1];
        CHECK(s1.number == 1 && s1.episodes.size() == 3);
        if (s1.episodes.size() == 3) {
            CHECK(s1.episodes[0].id == "tt0944947:1:1" && s1.episodes[0].plot == "Begins.");
            CHECK(s1.episodes[1].number == 2 && !s1.episodes[1].image.empty() && s1.episodes[2].number == 5);
        }
    }
    CHECK(si.seriesId == "tt0944947" && si.series.name == "Show");

    stremio::Streams s;
    CHECK(stremio::parseStreams(
            "{\"streams\":[{\"infoHash\":\"0123456789abcdef0123456789abcdef01234567\",\"fileIdx\":0,\"name\":\"Torrent\"},"
            "{\"ytId\":\"dQw4w9WgXcQ\"},{\"externalUrl\":\"https://watch.example.com/x\"},"
            "{\"url\":\"https://cdn.example.com/a.m3u8\",\"name\":\"Needs referer\",\"behaviorHints\":{\"proxyHeaders\":"
            "{\"request\":{\"Referer\":\"https://site.example.com\"}}}},"
            "{\"url\":\"https://cdn.example.com/b.mp4\",\"name\":\"Direct\",\"behaviorHints\":{\"notWebReady\":true,"
            "\"proxyHeaders\":{\"request\":{\"User-Agent\":\"ExamplePlayer/1.0\"}}}},"
            "{\"url\":\"http://cdn.example.com/c.mkv\",\"title\":\"Plain\"},{\"url\":\"magnet:?xt=urn:btih:abc\"}]}", s));
    CHECK_EQ(s.playable.size(), (size_t) 2);
    CHECK_EQ(s.skipped, 5);
    if (s.playable.size() == 2) {
        CHECK(s.playable[0].url == "https://cdn.example.com/b.mp4" && s.playable[0].userAgent == "ExamplePlayer/1.0");
        CHECK(s.playable[1].url == "http://cdn.example.com/c.mkv" && s.playable[1].userAgent.empty()
              && s.playable[1].label == "Plain");
    }
    CHECK(stremio::parseStreams("{\"streams\":[]}", s) && s.playable.empty());
    CHECK(!stremio::parseStreams("not json", s));

    CHECK_EQ(stremio::runtimeSeconds("142 min"), 8520);
    CHECK_EQ(stremio::runtimeSeconds("1h 30min"), 5400);
    CHECK_EQ(stremio::runtimeSeconds("2h"), 7200);
    CHECK_EQ(stremio::runtimeSeconds("45m"), 2700);
    CHECK_EQ(stremio::runtimeSeconds("N/A"), 0);
    CHECK_EQ(stremio::yearOf("2011–2019"), 2011);
    CHECK_EQ(stremio::yearOf("1994"), 1994);
    CHECK_EQ(stremio::yearOf("20190"), 0);
    CHECK_EQ(stremio::yearOf(""), 0);
}
