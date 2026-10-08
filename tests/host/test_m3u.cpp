// M3U / M3U8 playlist sources: the parser on the sanitized fixtures (tests/fixtures/m3u), stable channel
// identity, categories, profiles.json schema 2 migration, URL redaction, the playlist cache and the
// 25,000 / 50,000 entry benchmarks.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <set>

#include "check.h"
#include "../../src/core/utf8.h"
#include "../../src/iptv/catalog.h"
#include "../../src/iptv/m3u.h"
#include "../../src/platform/fs.h"
#include "../../src/platform/redact.h"
#include "../../src/storage/catalog_cache.h"
#include "../../src/storage/profile_store.h"

using namespace iptv;

namespace {
    std::string fixture(const char *name) {
        const char *src = std::getenv("PS4IPTV_SOURCE_DIR");
        std::string path = fs::join(fs::join(fs::join(src ? src : "src", ".."), "tests/fixtures/m3u"), name);
        std::string text;
        if (!fs::readFile(path, text, 1u << 20)) {
            std::printf("  missing fixture %s\n", path.c_str());
        }
        return text;
    }

    std::string tempDir(const char *name) {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string d = fs::join(base ? base : ".", name);
        fs::ensureDir(d);
        for (const auto &e: fs::listDir(d)) {
            if (!e.dir) {
                fs::removeFile(fs::join(d, e.name));
            }
        }
        return d;
    }

    double msSince(std::chrono::steady_clock::time_point t0) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }

    const m3u::Entry *byName(const m3u::Result &r, const std::string &name) {
        for (const auto &e: r.entries) {
            if (e.name == name) {
                return &e;
            }
        }
        return nullptr;
    }

    std::vector<std::string> idsOf(const std::string &source, const std::string &text) {
        m3u::Result r = m3u::parse(text);
        std::vector<Category> cats;
        std::vector<LiveChannel> ch;
        m3u::toChannels(source, r.entries, cats, ch);
        std::vector<std::string> ids;
        for (const auto &c: ch) {
            ids.push_back(c.id);
        }
        return ids;
    }

    // a realistic large playlist: country prefixes, ~40 groups, logos on 90 %, some HLS, a few user agents,
    // duplicates of names across groups, Turkish / Cyrillic names
    std::string synthetic(int n, bool rotateTokens = false) {
        static const char *const prefixes[] = {"TR: ", "UK: ", "DE: ", "|FR| ", "US: ", "RU: ", ""};
        static const char *const words[] = {"News", "Sport", "Haber", "Spor", "Kids", "Çocuk", "Film", "Música",
                                            "Документальный", "Discovery", "Cinema", "Belgesel", "Müzik", "Dizi"};
        std::string s = "#EXTM3U url-tvg=\"http://epg.example.com/guide.xml\"\n";
        s.reserve((size_t) n * 230);
        for (int i = 0; i < n; i++) {
            const char *prefix = prefixes[i % 7];
            std::string name = std::string(prefix) + words[(i * 5) % 14] + " " + std::to_string(i % 1000)
                               + ((i % 3) == 0 ? " HD" : "");
            std::string group = std::string(prefix).substr(0, 2) + " " + words[(i / 7) % 14];
            s += "#EXTINF:-1 tvg-id=\"ch" + std::to_string(i % 20000) + ".example\" tvg-name=\"" + name + "\"";
            if (i % 10 != 0) {
                s += " tvg-logo=\"http://img.example.com/logos/" + std::to_string(i) + ".png\"";
            }
            s += " group-title=\"" + group + "\"," + name + "\n";
            if (i % 500 == 0) {
                s += "#EXTVLCOPT:http-user-agent=ExamplePlayer/2.0\n";
            }
            s += "http://stream.example.com:8080/exampleuser/examplepass/" + std::to_string(100000 + i)
                 + (i % 4 == 0 ? ".m3u8" : ".ts");
            if (rotateTokens) {
                s += "?token=" + std::to_string(i * 7919 + 13);
            }
            s += "\n";
        }
        return s;
    }

    size_t heapOf(const std::string &s) {
        return s.capacity() > 22 ? s.capacity() + 1 : 0;   // beyond the small-string buffer
    }
}

TEST(m3u_basic_fixture) {
    m3u::Result r = m3u::parse(fixture("basic.m3u"));
    CHECK(r.ok());
    CHECK(r.stats.header);
    CHECK_EQ(r.entries.size(), (size_t) 8);
    CHECK_EQ(r.stats.channels, 8);
    CHECK_EQ(r.stats.extinf, 8);
    CHECK_EQ(r.stats.skipped(), 0);
    CHECK_EQ(r.stats.groups, 8);
    CHECK_EQ(r.stats.withLogo, 3);
    CHECK_EQ(r.stats.withoutLogo, 5);
    CHECK_EQ(r.stats.hls, 1);
    CHECK_EQ(r.stats.https, 0);   // an https logo is not an https stream
    const m3u::Entry &e = r.entries[0];
    CHECK(e.name == "News One HD");
    CHECK(e.tvgId == "news1.example" && e.tvgName == "News One");
    CHECK(e.logo == "http://img.example.com/logos/news1.png");
    CHECK(e.group == "News");
    CHECK(e.url == "http://stream.example.com/live/news1.ts");
    // UTF-8 names in many scripts, untouched
    CHECK(byName(r, "TR: TRT 1 \xC3\x87ocuk \xC5\x9E" "ark\xC4\xB1lar\xC4\xB1") != nullptr);
    CHECK(byName(r, "\xD0\x9F\xD0\xB5\xD1\x80\xD0\xB2\xD1\x8B\xD0\xB9 \xD0\xBA\xD0\xB0\xD0\xBD\xD0\xB0\xD0\xBB") != nullptr);
    CHECK(byName(r, "NHK \xE7\xB7\x8F\xE5\x90\x88") != nullptr);
    CHECK(r.entries[2].group == "T\xC3\xBCrkiye");
}

TEST(m3u_line_endings_and_bom_are_equivalent) {
    m3u::Result lf = m3u::parse(fixture("basic.m3u"));
    for (const char *name: {"basic_crlf.m3u", "basic_bom_crlf.m3u8", "basic_cr.m3u"}) {
        m3u::Result other = m3u::parse(fixture(name));
        CHECK(other.ok());
        CHECK_EQ(other.entries.size(), lf.entries.size());
        bool same = other.entries.size() == lf.entries.size();
        for (size_t i = 0; same && i < lf.entries.size(); i++) {
            same = other.entries[i].name == lf.entries[i].name && other.entries[i].url == lf.entries[i].url
                   && other.entries[i].group == lf.entries[i].group && other.entries[i].logo == lf.entries[i].logo;
        }
        CHECK(same);
        CHECK(idsOf("p1", fixture(name)) == idsOf("p1", fixture("basic.m3u")));
    }
}

TEST(m3u_extgrp_fallback) {
    m3u::Result r = m3u::parse(fixture("extgrp.m3u"));
    CHECK(r.ok());
    CHECK_EQ(r.entries.size(), (size_t) 5);
    CHECK(byName(r, "Channel A")->group == "Music");        // #EXTGRP after #EXTINF
    CHECK(byName(r, "Channel B")->group == "Kids");         // #EXTGRP before #EXTINF
    CHECK(byName(r, "Channel C")->group == "Movies");       // group-title wins over #EXTGRP
    CHECK(byName(r, "Channel D")->group == "Docs");         // empty group-title: #EXTGRP, trimmed
    CHECK(byName(r, "Channel E")->group.empty());           // none: Uncategorized
    CHECK_EQ(r.stats.uncategorized, 1);
}

TEST(m3u_edge_cases_skip_individually) {
    m3u::Result r = m3u::parse(fixture("edge_cases.m3u"));
    CHECK(r.ok());
    CHECK(r.stats.header);   // after blank lines and white space
    CHECK_EQ(r.stats.extinf, 20);
    CHECK_EQ(r.stats.channels, 18);
    CHECK_EQ((int) r.entries.size(), 18);
    CHECK_EQ(r.stats.missingUrl, 2);        // "Missing URL Channel", "Last Without URL"
    CHECK_EQ(r.stats.invalidUrl, 1);        // "not a url at all"
    CHECK_EQ(r.stats.skipped(), 3);
    CHECK_EQ(r.stats.malformedExtinf, 2);   // no title comma, unterminated quote
    CHECK_EQ(r.stats.unknownDirectives, 2); // #EXTIMG, #EXT-UNKNOWN-DIRECTIVE (KODIPROP / VLC options known)
    CHECK_EQ(r.stats.plainUrls, 1);
    CHECK_EQ(r.stats.unnamed, 2);
    CHECK_EQ(r.stats.groups, 6);
    CHECK_EQ(r.stats.uncategorized, 2);
    CHECK_EQ(r.stats.withLogo, 1);
    CHECK_EQ(r.stats.userAgents, 1);
    CHECK_EQ(r.stats.https, 1);
    CHECK_EQ(r.stats.otherProtocols, 1);
    CHECK_EQ(r.stats.hls, 1);               // .m3u8 before the query string
    CHECK_EQ(r.stats.withTvgId, 6);

    CHECK(byName(r, "Name, With, Commas") != nullptr);   // commas inside names, white space trimmed
    CHECK(byName(r, "Name, With, Commas")->url == "http://stream.example.com/1.ts");
    CHECK(byName(r, "No Logo Channel")->logo.empty());
    CHECK(byName(r, "No Group Channel")->group.empty());
    const m3u::Entry *noComma = byName(r, "Fallback Name");   // tvg-name when there is no title
    CHECK(noComma && noComma->group == "Group 4");
    const m3u::Entry *unterminated = byName(r, "Unterminated Quote Channel");
    CHECK(unterminated && unterminated->group == "Group 4" && unterminated->tvgId == "broken");
    const m3u::Entry *single = byName(r, "Single Quoted");
    CHECK(single && single->group == "Group 5" && single->number == 101 && single->tvgId == "unquoted.id");
    const m3u::Entry *ua = byName(r, "User Agent Channel");
    CHECK(ua && ua->userAgent == "ExamplePlayer/1.0 (Test)");
    CHECK(byName(r, "Single Quoted")->userAgent.empty());   // the option belongs to the next entry only
    CHECK(byName(r, "Invalid URL Channel") == nullptr);
    CHECK(byName(r, "Missing URL Channel") == nullptr);
    CHECK(byName(r, "Plain_Entry") && byName(r, "Plain_Entry")->group.empty());
    CHECK(byName(r, "Unnamed_Stream") && byName(r, "Unnamed_Stream")->group == "Group 6");
    CHECK(byName(r, "HTTPS Media")->url == "https://secure.example.com/live/https.m3u8?token=exampletoken");
}

TEST(m3u_rejects_what_is_not_a_channel_list) {
    CHECK(m3u::parse(fixture("hls_media.m3u8")).error == m3u::Error::HlsMedia);
    CHECK(m3u::parse(fixture("hls_master.m3u8")).error == m3u::Error::HlsMedia);
    CHECK(m3u::parse(fixture("not_playlist.html")).error == m3u::Error::NotPlaylist);
    CHECK(m3u::parse(fixture("empty.m3u")).error == m3u::Error::Empty);
    CHECK(m3u::parse("").error == m3u::Error::Empty);
    CHECK(m3u::parse(fixture("utf16.m3u")).error == m3u::Error::Utf16);
    CHECK(m3u::parse("#EXTM3U\n#EXTINF:-1,Only Header\n").error == m3u::Error::NotPlaylist);
    // plain M3U: URLs only
    m3u::Result plain = m3u::parse(fixture("plain.m3u"));
    CHECK(plain.ok() && plain.entries.size() == 2 && plain.entries[0].name == "one" && plain.stats.plainUrls == 2);
    // build() keeps the current playlist on failure
    LiveCatalog keep;
    m3u::Info info;
    CHECK(m3u::build("p1", fixture("basic.m3u"), keep, info) == m3u::Error::None);
    CHECK(m3u::build("p1", fixture("not_playlist.html"), keep, info) == m3u::Error::NotPlaylist);
    CHECK_EQ(keep.channels().size(), (size_t) 8);
}

TEST(m3u_invalid_utf8_repaired) {
    m3u::Result r = m3u::parse(fixture("invalid_utf8.m3u"));
    CHECK(r.ok() && r.entries.size() == 1);
    CHECK_EQ(r.stats.invalidUtf8, 2);
    std::u32string name = utf8::decode(r.entries[0].name);
    CHECK(std::find(name.begin(), name.end(), (char32_t) 0xFFFD) != name.end());
    // re-encoded: valid UTF-8 now (decode -> encode is stable)
    std::string again;
    for (char32_t c: name) {
        again += utf8::encode(c);
    }
    CHECK(again == r.entries[0].name);
}

TEST(m3u_limits) {
    m3u::Limits lim;
    lim.maxEntries = 3;
    m3u::Result r = m3u::parse(fixture("basic.m3u"), lim);
    CHECK_EQ(r.entries.size(), (size_t) 3);
    CHECK_EQ(r.stats.overLimit, 5);
    std::string longLine = "#EXTM3U\n#EXTINF:-1,Long\nhttp://stream.example.com/" + std::string(20000, 'a') + "\n"
                           "#EXTINF:-1,Short\nhttp://stream.example.com/s.ts\n";
    m3u::Result l = m3u::parse(longLine);
    CHECK(l.ok() && l.entries.size() == 1 && l.entries[0].name == "Short" && l.stats.invalidUrl == 1);
}

TEST(m3u_identity_is_stable_and_unique) {
    std::string text = fixture("edge_cases.m3u");
    std::vector<std::string> a = idsOf("p7", text);
    CHECK(a == idsOf("p7", text));   // deterministic (restart)
    CHECK_EQ(std::set<std::string>(a.begin(), a.end()).size(), a.size());   // unique
    // other source: independent ids for the same playlist (favorites of Playlist A and B stay apart)
    std::vector<std::string> b = idsOf("p8", text);
    for (size_t i = 0; i < a.size(); i++) {
        CHECK(a[i] != b[i]);
    }

    m3u::Entry e;
    e.name = "TRT 1";
    e.group = "Turkey";
    e.tvgId = "trt1.tr";
    e.url = "http://stream.example.com/trt1.ts?token=1";
    std::string id = m3u::channelId("p1", e);
    CHECK(id.size() == 17 && id[0] == 'm');
    m3u::Entry rotated = e;
    rotated.url = "http://other-cdn.example.com/x/trt1.ts?token=2";   // token / CDN rotation: same channel
    CHECK(m3u::channelId("p1", rotated) == id);
    m3u::Entry spaced = e;
    spaced.name = "  TRT   1 ";
    CHECK(m3u::channelId("p1", spaced) == id);
    m3u::Entry otherGroup = e;
    otherGroup.group = "Turkey HD";
    CHECK(m3u::channelId("p1", otherGroup) != id);   // same name in another group
    m3u::Entry noTvg = e;
    noTvg.tvgId = "none";                             // unusable tvg-id values are ignored
    m3u::Entry emptyTvg = e;
    emptyTvg.tvgId = "";
    CHECK(m3u::channelId("p1", noTvg) == m3u::channelId("p1", emptyTvg));
    CHECK(m3u::channelId("p2", e) != id);

    // reordering and new channels do not change existing ids
    std::string base = "#EXTM3U\n#EXTINF:-1 group-title=\"G\",One\nhttp://s.example.com/1.ts\n"
                       "#EXTINF:-1 group-title=\"G\",Two\nhttp://s.example.com/2.ts\n";
    std::string reordered = "#EXTM3U\n#EXTINF:-1 group-title=\"G\",New\nhttp://s.example.com/n.ts\n"
                            "#EXTINF:-1 group-title=\"G\",Two\nhttp://s.example.com/2.ts\n"
                            "#EXTINF:-1 group-title=\"G\",One\nhttp://s.example.com/1.ts\n";
    std::vector<std::string> before = idsOf("p1", base);
    std::vector<std::string> after = idsOf("p1", reordered);
    CHECK(after[2] == before[0] && after[1] == before[1]);

    // exact duplicates (same name, group, tvg-id): unique ids, the first one keeps the plain id
    std::string dups = "#EXTM3U\n#EXTINF:-1 group-title=\"G\",Dup\nhttp://s.example.com/a.ts\n"
                       "#EXTINF:-1 group-title=\"G\",Dup\nhttp://s.example.com/b.ts\n"
                       "#EXTINF:-1 group-title=\"G\",Dup\nhttp://s.example.com/b.ts\n"
                       "#EXTINF:-1 group-title=\"G\",Dup\nhttp://s.example.com/b.ts?token=9\n";
    std::vector<std::string> d = idsOf("p1", dups);
    CHECK_EQ(std::set<std::string>(d.begin(), d.end()).size(), (size_t) 4);
    m3u::Entry dup;
    dup.name = "Dup";
    dup.group = "G";
    CHECK(d[0] == m3u::channelId("p1", dup));
    CHECK(d[1].size() == 17 + 9 && d[1].compare(0, 17, d[0]) == 0);
    m3u::Result dr = m3u::parse(dups);
    std::vector<Category> cats;
    std::vector<LiveChannel> ch;
    m3u::Stats st;
    m3u::toChannels("p1", dr.entries, cats, ch, &st);
    CHECK_EQ(st.duplicateIds, 3);
}

TEST(m3u_channels_and_categories) {
    m3u::Result r = m3u::parse(fixture("edge_cases.m3u"));
    std::vector<Category> cats;
    std::vector<LiveChannel> ch;
    m3u::toChannels("p1", r.entries, cats, ch);
    CHECK_EQ(ch.size(), r.entries.size());
    // groups in order of first appearance, Uncategorized last
    CHECK_EQ(cats.size(), (size_t) 7);
    CHECK(cats[0].name == "Group 1" && cats[5].name == "Group 6");
    CHECK(cats.back().id == UNCATEGORIZED_ID);
    LiveCatalog live;
    live.assign(cats, ch);
    CHECK_EQ(live.countInCategory(UNCATEGORIZED_ID), 2);
    CHECK_EQ(live.countInCategory("Group 1"), 4);
    CHECK(!live.categoryName(UNCATEGORIZED_ID).empty());
    CHECK(live.categoryName("Group 2") == "Group 2");
    // playlist channels carry their media URL, never a fake stream id
    const LiveChannel &ua = live.channels()[(size_t) live.search("User Agent Channel", 1)[0]];
    CHECK(ua.isPlaylist() && ua.url == "http://stream.example.com/ua.ts" && ua.userAgent == "ExamplePlayer/1.0 (Test)");
    CHECK(ua.id[0] == 'm');
    // provider order is the catalog order
    CHECK(live.channels()[0].name == "Name, With, Commas");
    // favorites by local id
    std::set<std::string> favs = {ch[3].id, ch[4].id};
    CHECK(live.favorites(favs) == std::vector<int>({3, 4}));
    // search finds playlist channels (prefix alias, Turkish folding)
    LiveCatalog basic;
    m3u::Info info;
    CHECK(m3u::build("p1", fixture("basic.m3u"), basic, info) == m3u::Error::None);
    CHECK(!basic.search("trt 1", 5).empty());
    CHECK(!basic.search("cocuk sarkilari", 5).empty());
    CHECK(!basic.search("\xD0\xBF\xD0\xB5\xD1\x80\xD0\xB2\xD1\x8B\xD0\xB9", 5).empty());
    CHECK_EQ(info.categories, 8);
}

TEST(m3u_catalog_move_keeps_search_index) {
    // a tiny catalog (search keys shorter than std::string's small buffer) built elsewhere and moved
    LiveCatalog target;
    {
        LiveCatalog built;
        m3u::Info info;
        CHECK(m3u::build("p1", "#EXTM3U\n#EXTINF:-1,A\nhttp://s.example.com/a.ts\n", built, info) == m3u::Error::None);
        target = std::move(built);
        LiveCatalog clobber;   // reuse of the moved-from object's memory must not matter
        clobber.assign({}, {});
    }
    CHECK_EQ(target.search("a", 5).size(), (size_t) 1);
    LiveCatalog second(std::move(target));
    CHECK_EQ(second.search("a", 5).size(), (size_t) 1);
}

TEST(m3u_urls) {
    CHECK(m3u::schemeOf("HTTP://x.example.com/a") == "http");
    CHECK(m3u::schemeOf("rtmp://x/y") == "rtmp");
    CHECK(m3u::schemeOf("not a url").empty());
    CHECK(m3u::schemeOf("://x").empty());
    CHECK(m3u::isHttpUrl("https://x.example.com/list.m3u"));
    CHECK(!m3u::isHttpUrl("ftp://x.example.com/list.m3u"));
    CHECK(!m3u::isHttpUrl("http:///list.m3u"));
    CHECK(m3u::stableUrlKey("HTTP://User:Pw@Host.Example.com:80/a/B.ts?token=1#x") == "http://host.example.com:80/a/B.ts");
    CHECK(m3u::localPlaylistPath("/data/PS4IPTV/playlists/sports.m3u") == "/data/PS4IPTV/playlists/sports.m3u");
    CHECK(m3u::localPlaylistPath("file:///data/PS4IPTV/playlists/My List.M3U8") == "/data/PS4IPTV/playlists/My List.M3U8");
    CHECK(m3u::localPlaylistPath("/data/PS4IPTV/playlists/../profiles.json").empty());
    CHECK(m3u::localPlaylistPath("/data/PS4IPTV/playlists/sub/x.m3u").empty());
    CHECK(m3u::localPlaylistPath("/data/PS4IPTV/playlists/x.txt").empty());
    CHECK(m3u::localPlaylistPath("/mnt/usb0/x.m3u").empty());
    CHECK(m3u::localPlaylistPath("/data/PS4IPTV/playlists/.m3u").empty());
    // what the UI may show: no query, no user info, no inner path segments
    CHECK(m3u::displayUrl("http://exampleuser:examplepass@list.example.com:8080/get.php?username=exampleuser&password=examplepass&type=m3u_plus")
          == "list.example.com:8080/get.php");
    std::string shown = m3u::displayUrl("http://list.example.com/exampleuser/examplepass/playlist.m3u");
    CHECK(shown.find("exampleuser") == std::string::npos && shown.find("examplepass") == std::string::npos);
    CHECK(m3u::displayUrl("/data/PS4IPTV/playlists/sports.m3u") == "playlists/sports.m3u");
}

TEST(m3u_redaction_of_playlist_and_stream_urls) {
    redact::addUrl("http://list.example.com/get.php?username=m3uuser42&password=m3upass42&type=m3u_plus");
    std::string line = redact::apply("fetch failed for user m3uuser42 / m3upass42");
    CHECK(line.find("m3uuser42") == std::string::npos && line.find("m3upass42") == std::string::npos);
    // Xtream-panel M3U stream lines: /<user>/<pass>/<id> without a /live/ marker
    redact::addUrl("http://xtream.example.com:8080/m3uuser77/m3upass77/12345");
    line = redact::apply("open http://xtream.example.com:8080/m3uuser77/m3upass77/12346 user m3uuser77");
    CHECK(line.find("m3uuser77") == std::string::npos && line.find("m3upass77") == std::string::npos);
    // ordinary path words are never treated as secrets
    redact::addUrl("http://cdn.example.com/channels/hls/123.m3u8");
    CHECK(redact::apply("channels hls stream").find("channels hls") != std::string::npos);
    // tokens and signatures in query strings
    std::string signedUrl = redact::apply("https://cdn.example.com/a.m3u8?signature=abc123def&key=k9k9k9&e=1700");
    CHECK(signedUrl.find("abc123def") == std::string::npos && signedUrl.find("k9k9k9") == std::string::npos);
    CHECK(signedUrl.find("e=1700") != std::string::npos);
}

TEST(m3u_profile_schema_migration) {
    std::string dir = tempDir("profiles_m3u");
    // a version 1 file as written by Checkpoints 1 - 3.1
    std::string v1 = R"({"version":1,"activeProfileId":"p1","profiles":[{"id":"p1","name":"My Provider",)"
                     R"("server":"http://xtream.example.com:8080","username":"exampleuser","password":"examplepass",)"
                     R"("createdAt":100,"lastUsedAt":200,"lastStatus":"Connected"}]})";
    CHECK(fs::writeFileAtomic(fs::join(dir, "profiles.json"), v1));
    ProfileStore s(dir);
    CHECK(s.load());
    CHECK_EQ(s.loadedFormat(), 1);
    CHECK_EQ(s.profiles().size(), (size_t) 1);
    const Profile *p = s.find("p1");
    CHECK(p && p->type == SourceType::Xtream && p->server == "http://xtream.example.com:8080"
          && p->username == "exampleuser" && p->password == "examplepass" && p->name == "My Provider"
          && p->createdAt == 100 && p->lastUsedAt == 200 && p->lastStatus == "Connected");
    CHECK_EQ(s.activeId(), std::string("p1"));
    // the original file is kept once
    std::string kept;
    CHECK(fs::readFile(fs::join(dir, "profiles.v1.json"), kept, 1 << 20) && kept == v1);

    // add a playlist source and save: version 2
    Profile m;
    m.type = SourceType::M3u;
    m.name = "Sports Playlist";
    m.playlistUrl = "http://list.example.com/get.php?username=exampleuser&password=examplepass&type=m3u_plus";
    m.userAgent = "ExamplePlayer/1.0";
    m.playlistChannels = 1284;
    std::string mid = s.upsert(m, 300);
    CHECK(s.save());
    std::string saved;
    CHECK(fs::readFile(fs::join(dir, "profiles.json"), saved, 1 << 20));
    CHECK(saved.find("\"version\": 2") != std::string::npos || saved.find("\"version\":2") != std::string::npos);

    ProfileStore t(dir);
    CHECK(t.load());
    CHECK_EQ(t.loadedFormat(), 2);
    CHECK_EQ(t.profiles().size(), (size_t) 2);
    const Profile *x = t.find("p1");
    CHECK(x && x->type == SourceType::Xtream && x->password == "examplepass" && x->playlistUrl.empty());
    const Profile *y = t.find(mid);
    CHECK(y && y->type == SourceType::M3u && y->playlistUrl == m.playlistUrl && y->userAgent == m.userAgent
          && y->playlistChannels == 1284 && y->server.empty());
    // idempotent: load + save again gives the same file, the v1 copy is not touched
    CHECK(t.save());
    std::string again;
    CHECK(fs::readFile(fs::join(dir, "profiles.json"), again, 1 << 20));
    CHECK(again == saved);
    std::string keptAgain;
    CHECK(fs::readFile(fs::join(dir, "profiles.v1.json"), keptAgain, 1 << 20) && keptAgain == v1);

    // unknown source types (a newer schema 2 writer) are skipped, not misread as Xtream
    std::string unknown = R"({"version":2,"activeProfileId":"p1","profiles":[)"
                          R"({"id":"p1","sourceType":"xtream","name":"A","server":"http://a.example.com","username":"u","password":"p"},)"
                          R"({"id":"p2","sourceType":"stalker","name":"B","server":"http://b.example.com"},)"
                          R"({"id":"p3","sourceType":"m3u","name":"C"}]})";
    ProfileStore u(dir);
    CHECK(u.deserialize(unknown, nullptr));
    CHECK_EQ(u.profiles().size(), (size_t) 1);   // p2 unknown type, p3 has no playlist URL
    // a future version is refused as before
    CHECK(!u.deserialize(R"({"version":3,"profiles":[]})", nullptr));
}

TEST(m3u_cache_round_trip) {
    std::string dir = tempDir("m3u_cache");
    CatalogCache cache(dir);
    std::string body = fixture("edge_cases.m3u");
    LiveCatalog first;
    m3u::Info info;
    CHECK(m3u::build("p5", body, first, info) == m3u::Error::None);
    CHECK(cache.save("p5", "m3u_playlist", body, 1700000000, (int) first.channels().size()));
    std::string loaded;
    CatalogCache::Meta meta;
    CHECK(cache.load("p5", "m3u_playlist", loaded, meta));
    CHECK(meta.items == (int) first.channels().size() && meta.savedAt == 1700000000);
    LiveCatalog second;
    CHECK(m3u::build("p5", loaded, second, info) == m3u::Error::None);
    CHECK_EQ(second.channels().size(), first.channels().size());
    bool sameIds = true;
    for (size_t i = 0; i < first.channels().size(); i++) {
        sameIds = sameIds && first.channels()[i].id == second.channels()[i].id;
    }
    CHECK(sameIds);
    // damaged copy: deleted, never partially used
    std::string path = fs::join(fs::join(fs::join(dir, "cache"), "p5"), "m3u_playlist.json");
    std::string damaged = loaded;
    damaged[damaged.size() / 2] ^= 0x20;
    CHECK(fs::writeFileReplace(path, damaged));
    CHECK(!cache.load("p5", "m3u_playlist", loaded, meta));
    CHECK(!fs::exists(path));
}

TEST(m3u_benchmark_25k_50k) {
    for (int n: {25000, 50000}) {
        std::string body = synthetic(n);
        auto t0 = std::chrono::steady_clock::now();
        m3u::Result r = m3u::parse(body);
        double parseOnly = msSince(t0);
        CHECK(r.ok() && (int) r.entries.size() == n);

        LiveCatalog live;
        m3u::Info info;
        t0 = std::chrono::steady_clock::now();
        CHECK(m3u::build("p9", body, live, info) == m3u::Error::None);
        double buildMs = msSince(t0);
        CHECK_EQ((int) live.channels().size(), n);
        std::set<std::string> unique;
        for (const auto &c: live.channels()) {
            unique.insert(c.id);
        }
        CHECK_EQ((int) unique.size(), n);

        // cache: save the raw body (checksummed), load it back and rebuild
        std::string dir = tempDir("m3u_bench");
        CatalogCache cache(dir);
        t0 = std::chrono::steady_clock::now();
        CHECK(cache.save("p9", "m3u_playlist", body, 1700000000, n));
        double saveMs = msSince(t0);
        std::string loaded;
        CatalogCache::Meta meta;
        t0 = std::chrono::steady_clock::now();
        CHECK(cache.load("p9", "m3u_playlist", loaded, meta));
        double loadMs = msSince(t0);
        LiveCatalog fromCache;
        t0 = std::chrono::steady_clock::now();
        CHECK(m3u::build("p9", loaded, fromCache, info) == m3u::Error::None);
        double rebuildMs = msSince(t0);
        CHECK(fromCache.channels().back().id == live.channels().back().id);

        // search latency
        const char *queries[] = {"trt", "spor 12", "haber", "cocuk", "discovery hd", "documentalnyi", "xyzzy",
                                 "muzik 999", "kids", "belgsel"};
        double worst = 0, total = 0;
        for (const char *q: queries) {
            t0 = std::chrono::steady_clock::now();
            search::Result res = live.searchRanked(q, 200);
            double ms = msSince(t0);
            worst = std::max(worst, ms);
            total += ms;
            (void) res;
        }
        CHECK(!live.search("haber", 10).empty());

        // memory: strings beyond the small-string buffer + vectors + index
        size_t bytes = live.channels().capacity() * sizeof(LiveChannel) + info.indexBytes;
        for (const auto &c: live.channels()) {
            bytes += heapOf(c.id) + heapOf(c.name) + heapOf(c.categoryId) + heapOf(c.icon) + heapOf(c.epgId)
                     + heapOf(c.url) + heapOf(c.userAgent);
        }
        std::printf("     M3U %d entries (%.1f MB): parse %.0f ms | parse + ids + catalog + search index %.0f ms "
                    "(ids %.0f, index %.0f) | cache save %.0f ms, load %.0f ms, rebuild %.0f ms | search avg %.2f ms, "
                    "worst %.2f ms | memory ~%.1f MB (index %.1f MB) | %d groups\n",
                    n, body.size() / 1048576.0, parseOnly, buildMs, info.parseMs, info.indexMs, saveMs, loadMs,
                    rebuildMs, total / 10, worst, bytes / 1048576.0, info.indexBytes / 1048576.0, info.stats.groups);
        CHECK(buildMs < 5000);   // generous: the PS4 is slower than the host, and this runs on a worker
        CHECK(worst < 100);
    }
    // ids survive a refresh where every stream token rotated
    std::string a = synthetic(2000, false);
    std::string b = synthetic(2000, true);
    CHECK(idsOf("p9", a) == idsOf("p9", b));
}
