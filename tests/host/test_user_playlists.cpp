// My Playlists (media/user_playlists.h): names / addresses / file names, M3U text both ways (a playlist written here
// opens as a playlist source unchanged), listing a folder (titles, sizes, files that are not editable), saving and
// loading, moving entries, paths that stay inside the playlist folder.

#include <string>
#include <vector>

#include "check.h"
#include "../../src/iptv/catalog.h"
#include "../../src/iptv/m3u.h"
#include "../../src/media/user_playlists.h"
#include "../../src/platform/fs.h"

namespace {
    std::string folder(const std::string &name) {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string d = fs::join(fs::join(base ? base : ".", "user-playlists"), name) + "/";
        for (const auto &e: fs::listDir(d)) {
            fs::removeFile(fs::join(d, e.name));   // a clean folder per run
        }
        fs::ensureDir(d);
        return d;
    }

    userpl::Entry entry(const std::string &name, const std::string &url) {
        userpl::Entry e;
        e.name = name;
        e.url = url;
        return e;
    }
}

TEST(userpl_names_and_addresses) {
    CHECK_EQ(userpl::cleanName("  My \t Sports\n "), std::string("My Sports"));
    CHECK_EQ(userpl::cleanName(std::string(300, 'a')).size(), userpl::MAX_NAME);
    std::string turkish(100, 'a');
    turkish += "şşşşşşşşşşşşşş";   // 2-byte characters across the limit: never cut inside one
    std::string cut = userpl::cleanName(turkish);
    CHECK(cut.size() <= userpl::MAX_NAME && ((unsigned char) cut.back() & 0xC0) != 0xC0);
    CHECK(userpl::validUrl("http://stream.example.com/live/1.ts"));
    CHECK(userpl::validUrl("https://stream.example.com:8443/a.m3u8?token=exampletoken"));
    CHECK(userpl::validUrl("rtmp://stream.example.com/app/key"));
    CHECK(userpl::validUrl("udp://239.0.0.1:1234"));
    CHECK(userpl::validUrl("rtsp://cam.example.com/stream"));
    CHECK(!userpl::validUrl("ftp://files.example.com/a.ts"));
    CHECK(!userpl::validUrl("file:///data/a.ts"));
    CHECK(!userpl::validUrl("/data/PS4IPTV/a.ts"));
    CHECK(!userpl::validUrl("http://"));
    CHECK(!userpl::validUrl("http:///path"));
    CHECK(!userpl::validUrl("http://host/a b.ts"));
    CHECK(!userpl::validUrl("http://host/a\n.ts"));
    CHECK(!userpl::validUrl(""));
    CHECK(!userpl::validUrl("http://host/" + std::string(userpl::MAX_URL, 'a')));
    CHECK_EQ(userpl::slug("My Sports!"), std::string("my-sports"));
    CHECK_EQ(userpl::slug("  Haberler / TR  "), std::string("haberler-tr"));
    CHECK_EQ(userpl::slug("şşş"), std::string("playlist"));
    CHECK_EQ(userpl::slug("../../etc"), std::string("etc"));
    CHECK(userpl::slug(std::string(100, 'x')).size() <= 40);
}

TEST(userpl_m3u_both_ways) {
    userpl::Playlist p;
    p.title = "My \"Best\" Channels";
    userpl::Entry a = entry("TRT 1, HD", "http://stream.example.com/1.ts");
    a.logo = "http://img.example.com/1.png";
    a.group = "Ulusal";
    a.tvgId = "trt1.tr";
    userpl::Entry b = entry("Radio \"X\"", "http://radio.example.com/x.mp3");
    b.userAgent = "ExamplePlayer/1.0";
    b.group = "Group \"quoted\"";
    p.entries = {a, b, entry("Third", "rtmp://stream.example.com/app/3")};
    std::string text = userpl::toM3u(p);
    userpl::Playlist back;
    std::string err;
    CHECK(userpl::fromM3u(text, "fallback", back, err));
    CHECK_EQ(back.title, std::string("My \"Best\" Channels"));
    CHECK_EQ(back.entries.size(), (size_t) 3);
    if (back.entries.size() == 3) {
        CHECK_EQ(back.entries[0].name, std::string("TRT 1, HD"));   // a comma in a name survives
        CHECK_EQ(back.entries[0].logo, a.logo);
        CHECK_EQ(back.entries[0].group, std::string("Ulusal"));
        CHECK_EQ(back.entries[0].tvgId, std::string("trt1.tr"));
        CHECK_EQ(back.entries[1].userAgent, std::string("ExamplePlayer/1.0"));
        CHECK_EQ(back.entries[1].group, std::string("Group 'quoted'"));   // quotes never break an attribute
        CHECK_EQ(back.entries[2].url, std::string("rtmp://stream.example.com/app/3"));
    }
    // it is a playlist source as it is: the same channels, in the same order
    iptv::LiveCatalog cat;
    m3u::Info info;
    CHECK(m3u::build("p1", text, cat, info) == m3u::Error::None);
    CHECK_EQ(cat.channels().size(), (size_t) 3);
    if (cat.channels().size() == 3) {
        CHECK_EQ(cat.channels()[0].name, std::string("TRT 1, HD"));
        CHECK_EQ(cat.channels()[1].userAgent, std::string("ExamplePlayer/1.0"));
    }
    // an empty playlist is a playlist; no #PLAYLIST: the fallback title
    userpl::Playlist empty;
    CHECK(userpl::fromM3u("#EXTM3U\n", "Fallback", empty, err) && empty.entries.empty() && empty.title == "Fallback");
    CHECK(userpl::fromM3u(userpl::toM3u(empty), "x", empty, err) && empty.entries.empty() && empty.title == "Fallback");
    // not playlists
    userpl::Playlist no;
    CHECK(!userpl::fromM3u("<!DOCTYPE html><html><body>403</body></html>", "x", no, err));
    CHECK(!userpl::fromM3u("", "x", no, err));
    CHECK(!userpl::fromM3u("#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:6,\nseg1.ts\n", "x", no, err));
    std::string big = "#EXTM3U\n";
    for (size_t i = 0; i <= userpl::MAX_ENTRIES; i++) {
        big += "#EXTINF:-1,C" + std::to_string(i) + "\nhttp://stream.example.com/" + std::to_string(i) + ".ts\n";
    }
    CHECK(!userpl::fromM3u(big, "x", no, err));
}

TEST(userpl_files) {
    std::string dir = folder("files");
    userpl::Playlist p;
    p.title = "Zeta";
    p.path = userpl::newPath(dir, p.title);
    CHECK_EQ(p.path, dir + "zeta.m3u");
    p.entries = {entry("A", "http://stream.example.com/a.ts"), entry("B", "http://stream.example.com/b.ts")};
    std::string err;
    CHECK(userpl::save(p, err));
    CHECK_EQ(userpl::newPath(dir, "Zeta"), dir + "zeta-2.m3u");   // never an existing file
    userpl::Playlist alpha;
    alpha.title = "alpha";
    alpha.path = userpl::newPath(dir, alpha.title);
    CHECK(userpl::save(alpha, err));
    CHECK(userpl::save(alpha, err));   // saved twice: a backup copy appears, never listed
    fs::writeFileReplace(dir + "notes.txt", "#EXTM3U\n");
    fs::writeFileReplace(dir + ".hidden.m3u", "#EXTM3U\n");
    fs::writeFileReplace(dir + "page.m3u8", "<html>not a playlist</html>");
    fs::writeFileReplace(dir + "huge.m3u", "#EXTM3U\n" + std::string(userpl::MAX_FILE_BYTES, '#'));
    std::vector<userpl::Summary> l = userpl::list(dir);
    CHECK_EQ(l.size(), (size_t) 4);   // alpha, huge, page, Zeta (by title)
    if (l.size() == 4) {
        CHECK(l[0].title == "alpha" && l[0].editable && l[0].entries == 0);
        CHECK(l[1].title == "huge" && !l[1].editable);
        CHECK(l[2].title == "page" && !l[2].editable);
        CHECK(l[3].title == "Zeta" && l[3].editable && l[3].entries == 2 && l[3].file == "zeta.m3u");
    }
    userpl::Playlist back;
    CHECK(userpl::load(p.path, back, err));
    CHECK(back.path == p.path && back.title == "Zeta" && back.entries.size() == 2);
    CHECK(!userpl::load(dir + "huge.m3u", back, err));
    CHECK(!userpl::load(dir + "missing.m3u", back, err));
    CHECK(userpl::remove(alpha.path));
    CHECK(!fs::exists(alpha.path) && !fs::exists(alpha.path + ".bak"));
    // a playlist written here is a local playlist a source may open
    CHECK(!m3u::localPlaylistPath(std::string(m3u::LOCAL_PLAYLIST_DIR) + "zeta.m3u").empty());
    // paths inside the folder only
    CHECK(userpl::inside(dir, dir + "zeta.m3u"));
    CHECK(userpl::inside(dir, dir + "a.M3U8"));
    CHECK(!userpl::inside(dir, dir + "../zeta.m3u"));
    CHECK(!userpl::inside(dir, dir + "sub/zeta.m3u"));
    CHECK(!userpl::inside(dir, dir + ".hidden.m3u"));
    CHECK(!userpl::inside(dir, dir + "zeta.txt"));
    CHECK(!userpl::inside(dir, "/data/other/zeta.m3u"));
    CHECK(!userpl::inside(dir, dir));
}

TEST(userpl_edits) {
    userpl::Playlist p;
    for (const char *n: {"A", "B", "C", "D"}) {
        p.entries.push_back(entry(n, std::string("http://stream.example.com/") + n + ".ts"));
    }
    auto order = [&p]() {
        std::string s;
        for (const auto &e: p.entries) {
            s += e.name;
        }
        return s;
    };
    CHECK_EQ(userpl::move(p, 0, 1), 1);
    CHECK_EQ(order(), std::string("BACD"));
    CHECK_EQ(userpl::move(p, 1, 2), 3);
    CHECK_EQ(order(), std::string("BCDA"));
    CHECK_EQ(userpl::move(p, 3, -10), 0);   // clamped
    CHECK_EQ(order(), std::string("ABCD"));
    CHECK_EQ(userpl::move(p, 2, 0), 2);
    CHECK_EQ(userpl::move(p, 3, 5), 3);
    CHECK_EQ(order(), std::string("ABCD"));
    CHECK_EQ(userpl::move(p, 9, 1), -1);
    CHECK_EQ(userpl::move(p, -1, 1), -1);
    CHECK_EQ(userpl::find(p, "http://stream.example.com/C.ts"), 2);
    CHECK_EQ(userpl::find(p, "http://stream.example.com/Z.ts"), -1);
}
