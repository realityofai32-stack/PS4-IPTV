// libFuzzer target: the input the V7 Player sources added since v0.4.1 parse, and the app's own data files, with
// invariants:
//   - the streaming XMLTV reader (iptv/xmltv), fed in pieces split where the input says, and the XMLTV time stamps
//   - every Jellyfin / Emby answer parser (iptv/mediabrowser) and every Stremio add-on parser (iptv/stremio)
//   - My Playlists' M3U import and export (media/user_playlists): what is exported reads back the same
//   - the data files read at startup: settings, profiles, the download list, the startup state (crash guard)
// No crash / UB (sanitizers); bounded outputs.
#include <cstdint>
#include <string>
#include <vector>

#include "../../src/downloads/download_model.h"
#include "../../src/iptv/mediabrowser.h"
#include "../../src/iptv/stremio.h"
#include "../../src/iptv/xmltv.h"
#include "../../src/media/user_playlists.h"
#include "../../src/platform/startup_state.h"
#include "../../src/storage/profile_store.h"
#include "../../src/storage/settings_store.h"

#define REQUIRE(c) do { if (!(c)) { __builtin_trap(); } } while (0)

namespace {
    struct Count : xmltv::Handler {
        int channels = 0;
        int programmes = 0;

        void channel(xmltv::Channel &c) override {
            REQUIRE(c.names.size() <= 4);
            channels++;
        }

        void programme(xmltv::Programme &p) override {
            REQUIRE(p.title.size() <= xmltv::Reader::MAX_FIELD && p.description.size() <= xmltv::Reader::MAX_FIELD);
            REQUIRE(p.start != 0);
            programmes++;
        }
    };
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::string in((const char *) data, size);
    std::string err;

    // XMLTV: two pieces, split at a place taken from the input
    {
        Count c;
        xmltv::Reader r(c, size > 2 && data[0] & 1 ? "tr" : "");
        size_t cut = size ? data[size - 1] % (size + 1) : 0;
        bool ok = r.feed(in.data(), cut);
        if (ok) {
            ok = r.feed(in.data() + cut, size - cut);
        }
        r.finish();
        REQUIRE(r.stats().bytes <= size);
        REQUIRE(c.programmes <= r.stats().programmes && c.channels <= r.stats().channels);
        int64_t t = 0;
        xmltv::parseTime(in, t);
        xmltv::nameKey(in);
    }

    // Jellyfin / Emby
    {
        mediabrowser::ServerInfo si;
        mediabrowser::parsePublicInfo(in, si);
        mediabrowser::Login login;
        mediabrowser::parseLogin(in, login);
        std::vector<mediabrowser::Library> libs;
        mediabrowser::parseViews(in, libs);
        std::vector<iptv::Movie> movies;
        mediabrowser::Page page;
        mediabrowser::parseMovies(in, "http://media.example.com:8096", "lib", movies, page);
        std::vector<iptv::Series> series;
        mediabrowser::parseSeries(in, "http://media.example.com:8096", "lib", series, page);
        iptv::MovieInfo mi;
        mediabrowser::parseMovieInfo(in, "http://media.example.com:8096", mi);
        REQUIRE(mi.durationSeconds >= 0);
        iptv::Series s;
        mediabrowser::parseSeriesDetail(in, "http://media.example.com:8096", s);
        iptv::SeriesInfo info;
        mediabrowser::parseEpisodes(in, "http://media.example.com:8096", info);
    }

    // Stremio add-ons
    {
        stremio::Manifest m;
        stremio::parseManifest(in, m);
        std::vector<iptv::Movie> movies;
        int n = 0;
        stremio::parseMovies(in, "movie/top", movies, n);
        REQUIRE((int) movies.size() <= n);
        std::vector<iptv::Series> series;
        stremio::parseSeries(in, "series/top", series, n);
        iptv::MovieInfo mi;
        stremio::parseMovieMeta(in, mi);
        iptv::SeriesInfo si;
        stremio::parseSeriesMeta(in, si);
        stremio::Streams streams;
        if (stremio::parseStreams(in, streams)) {
            for (const auto &p: streams.playable) {
                // only direct http(s) addresses are ever playable (no torrent, no YouTube page, no magnet)
                REQUIRE(p.url.compare(0, 7, "http://") == 0 || p.url.compare(0, 8, "https://") == 0);
            }
        }
    }

    // My Playlists: an import that succeeds exports to text that imports to the same entries
    {
        userpl::Playlist pl;
        if (userpl::fromM3u(in, "Fuzz", pl, err)) {
            std::string text = userpl::toM3u(pl);
            userpl::Playlist again;
            std::string err2;
            REQUIRE(userpl::fromM3u(text, "Fuzz", again, err2));
            REQUIRE(again.entries.size() == pl.entries.size());
        }
    }

    // data files read at startup
    {
        SettingsStore settings("/nonexistent-fuzz/");
        settings.deserialize(in, &err);
        REQUIRE(!settings.get().language.empty());
        ProfileStore profiles("/nonexistent-fuzz/");
        profiles.deserialize(in, &err);
        std::vector<dl::Item> items;
        dl::parseManifest(in, items, &err);
        for (const auto &it: items) {
            REQUIRE(it.fileName.find('/') == std::string::npos && it.fileName.find("..") == std::string::npos);
        }
        startup::State st;
        if (startup::parse(in, st)) {
            REQUIRE(st.stage.empty() || startup::knownStage(st.stage) || st.stage == startup::UNKNOWN
                    || st.stage == startup::BEGIN || st.stage == startup::FIRST_FRAME);
        }
    }
    return 0;
}
