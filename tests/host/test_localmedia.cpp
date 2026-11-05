// Local Media: path validation (no traversal outside the roots), the playable formats derived from FFmpeg's demuxers,
// folder listing (folders first, natural order, hidden / unsupported / symbolic links left out), and local files
// never entering the progress store.

#include <cstdlib>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "check.h"
#include "../../src/app/vod_progress.h"
#include "../../src/media/local_media.h"
#include "../../src/platform/fs.h"

using localmedia::Entry;
using localmedia::Formats;
using localmedia::Root;

namespace {
    Formats sampleFormats() {
        // a subset of FFmpeg 5.0's demuxer table: name, extensions
        Formats f;
        f.addDemuxer("matroska,webm", "mkv,mk3d,mka,mks,webm");
        f.addDemuxer("mov,mp4,m4a,3gp,3g2,mj2", "mov,mp4,m4a,3gp,3g2,mj2,psp,m4b,ism,ismv,isma,f4v");
        f.addDemuxer("avi", "avi");
        f.addDemuxer("mpegts", "");
        f.addDemuxer("mpeg", "");
        f.addDemuxer("mp3", "mp2,mp3,m2a,mpa");
        f.addDemuxer("flac", "flac");
        f.addDemuxer("ogg", "ogg");
        // not playable on their own
        f.addDemuxer("srt", "srt");
        f.addDemuxer("ass", "ass,ssa");
        f.addDemuxer("tty", "ans,art,asc,diz,ice,nfo,txt,vt");
        f.addDemuxer("image2", "bmp,dpx,jpeg,jpg,png,tga,tif,tiff");
        f.addDemuxer("png_pipe", "png");
        f.addDemuxer("hls", "m3u8");
        f.addDemuxer("concat", "ffconcat");
        return f;
    }

    // absolute: Local Media only accepts absolute paths
    std::string tempDir(const char *name) {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string dir = fs::join(base ? base : ".", name);
#ifndef _WIN32
        if (!dir.empty() && dir[0] != '/') {
            char cwd[4096];
            if (getcwd(cwd, sizeof(cwd)) != nullptr) {
                dir = std::string(cwd) + "/" + dir;
            }
        }
#endif
        return dir;
    }
}

TEST(localmedia_paths_stay_inside_their_roots) {
    std::string out;
    CHECK(localmedia::normalize("/data//Movies/./Action/", out) && out == "/data/Movies/Action");
    CHECK(localmedia::normalize("/", out) && out == "/");
    CHECK(!localmedia::normalize("/data/../etc", out));
    CHECK(!localmedia::normalize("/data/Movies/..", out));
    CHECK(!localmedia::normalize("data/Movies", out));
    CHECK(!localmedia::normalize("", out));
    Root data = {"/data/", false, 0};
    Root usb = {"/mnt/usb0/", true, 1};
    CHECK(localmedia::within(data, "/data"));
    CHECK(localmedia::within(data, "/data/Movies/a.mkv"));
    CHECK(!localmedia::within(data, "/database/a.mkv"));        // a prefix is not a parent
    CHECK(!localmedia::within(data, "/data/../system/x"));
    CHECK(!localmedia::within(data, "/mnt/usb0/a.mkv"));
    CHECK(!localmedia::within(usb, "/mnt/usb1/a.mkv"));
    CHECK(localmedia::within(usb, "/mnt/usb0/Films/b.mp4"));
    std::vector<Root> roots = localmedia::candidateRoots();
    CHECK_EQ(roots.size(), (size_t) 9);
    CHECK(roots[0].path == "/data/" && !roots[0].usb);
    CHECK(roots[1].path == "/mnt/usb0/" && roots[1].usb && roots[1].number == 1);
    CHECK(localmedia::rootOf(roots, "/mnt/usb3/x/y.mkv") == &roots[4]);
    CHECK(localmedia::rootOf(roots, "/system/common/lib") == nullptr);
    CHECK(localmedia::safeName("Movie (2020).mkv"));
    CHECK(!localmedia::safeName(".."));
    CHECK(!localmedia::safeName("."));
    CHECK(!localmedia::safeName("a/b"));
    CHECK(!localmedia::safeName(""));
}

TEST(localmedia_formats_come_from_the_demuxers) {
    Formats f = sampleFormats();
    for (const char *e: {"mkv", "MKV", "webm", "mp4", "mov", "avi", "ts", "m2ts", "mts", "mpg", "vob", "mp3", "flac",
                         "ogg", "opus", "m4a"}) {
        CHECK(f.supported(e));
    }
    for (const char *e: {"srt", "ass", "txt", "nfo", "jpg", "png", "m3u8", "ffconcat", "exe", ""}) {
        CHECK(!f.supported(e));
    }
    // a probed format is offered only when its demuxer is in the build
    Formats noTs;
    noTs.addDemuxer("matroska,webm", "mkv");
    CHECK(!noTs.supported("ts"));
    CHECK_EQ(localmedia::extensionOf("Show.S01E02.MKV"), std::string("mkv"));
    CHECK_EQ(localmedia::extensionOf(".hidden"), std::string(""));
    CHECK_EQ(localmedia::extensionOf("noext"), std::string(""));
}

TEST(localmedia_folder_order_and_filtering) {
    std::vector<Entry> raw = {{"Episode 10.mkv", "/d/Episode 10.mkv", false, 10}, {"episode 2.mkv", "/d/episode 2.mkv", false, 2},
                              {"notes.txt", "/d/notes.txt", false, 1}, {"Season 2", "/d/Season 2", true, -1},
                              {"season 10", "/d/season 10", true, -1}, {".DS_Store", "/d/.DS_Store", false, 1},
                              {".hidden", "/d/.hidden", true, -1}, {"Film.mp4", "/d/Film.mp4", false, 5},
                              {"cover.jpg", "/d/cover.jpg", false, 3}, {"movie.srt", "/d/movie.srt", false, 1}};
    std::vector<Entry> out = localmedia::prepare(raw, sampleFormats());
    std::vector<std::string> names;
    for (const auto &e: out) {
        names.push_back(e.name);
    }
    CHECK((names == std::vector<std::string>{"Season 2", "season 10", "episode 2.mkv", "Episode 10.mkv", "Film.mp4"}));
    CHECK(localmedia::naturalLess("a2", "a10"));
    CHECK(!localmedia::naturalLess("a10", "a2"));
    CHECK(localmedia::naturalLess("a02", "a3"));
    CHECK(localmedia::naturalLess("Alpha", "beta"));
}

TEST(localmedia_listing_a_real_folder) {
    std::string base = tempDir("localmedia");
    fs::ensureDir(base);
    fs::ensureDir(fs::join(base, "Series"));
    fs::writeFileAtomic(fs::join(base, "b.mkv"), "x");
    fs::writeFileAtomic(fs::join(base, "a.mp4"), "xy");
    fs::writeFileAtomic(fs::join(base, "readme.txt"), "x");
    fs::writeFileAtomic(fs::join(base, ".secret.mkv"), "x");
#ifndef _WIN32
    std::string link = fs::join(base, "escape");
    unlink(link.c_str());
    CHECK(symlink("/", link.c_str()) == 0);   // a link out of the root is never offered
    std::string linkFile = fs::join(base, "linked.mkv");
    unlink(linkFile.c_str());
    CHECK(symlink(fs::join(base, "b.mkv").c_str(), linkFile.c_str()) == 0);
#endif

    Root root = {base + "/", false, 0};
    CHECK(localmedia::available(root));
    localmedia::Listing l = localmedia::list(root, base, sampleFormats());
    CHECK(l.ok);
    std::vector<std::string> names;
    for (const auto &e: l.entries) {
        names.push_back(e.name);
    }
    CHECK((names == std::vector<std::string>{"Series", "a.mp4", "b.mkv"}));
    CHECK(l.entries.size() == 3 && l.entries[0].directory && l.entries[1].size == 2);
    std::string normalBase;
    CHECK(localmedia::normalize(base, normalBase));
    CHECK(l.entries[1].path == normalBase + "/a.mp4");
    // outside the root, or climbing out of it: refused without reading anything
    CHECK(!localmedia::list(root, "/", sampleFormats()).ok);
    CHECK(!localmedia::list(root, base + "/../", sampleFormats()).ok);
    CHECK(!localmedia::list(root, base + "/Series/../..", sampleFormats()).ok);
    CHECK(localmedia::list(root, base + "/Series", sampleFormats()).ok);
    CHECK(!localmedia::list(root, base + "/missing", sampleFormats()).ok);
    CHECK(!localmedia::available({"/definitely/not/here/", true, 1}));
}

TEST(localmedia_files_never_enter_progress_or_history) {
    std::string dir = tempDir("localmedia_progress");
    fs::ensureDir(dir);
    LibraryStore store(dir);
    store.setProfile("p1");
    VodProgress tracker(store);
    screens::VodItem it;
    it.type = iptv::ContentType::Movie;
    it.id = "local:/data/Movies/a.mkv";
    it.localPath = "/data/Movies/a.mkv";
    it.localMedia = true;
    CHECK_EQ(tracker.begin(it, true, 0), 0.0);
    tracker.observe(120, 3600, true, 1);
    tracker.observe(300, 3600, true, 200);
    CHECK(tracker.due(1000) == VodProgress::Due::None);
    CHECK(!tracker.record(1000, 300));
    CHECK(!tracker.record(1001, -1, true));
    CHECK(store.progressOf(iptv::ContentType::Movie, it.id) == nullptr);
    CHECK(store.continueWatching(20).empty());
    // the same file as a provider movie is remembered as before
    screens::VodItem movie = it;
    movie.localMedia = false;
    movie.id = "123";
    tracker.begin(movie, true, 0);
    tracker.observe(400, 3600, true, 1);
    CHECK(tracker.record(10, 400));
    CHECK(store.progressOf(iptv::ContentType::Movie, "123") != nullptr);
}

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>

namespace {
    // a fresh directory tree under the test temp dir
    std::string tree(const char *name) {
        std::string d = tempDir(name);
        std::string cmd = "rm -rf '" + d + "'";
        if (std::system(cmd.c_str()) != 0) {
            std::printf("  could not clean %s\n", d.c_str());
        }
        fs::ensureDir(d);
        return d;
    }

    void touch(const std::string &path, const std::string &content = "x") {
        CHECK(fs::writeFileAtomic(path, content));
    }

    std::vector<std::string> names(const localmedia::Listing &l) {
        std::vector<std::string> out;
        for (const auto &e: l.entries) {
            out.push_back(e.name);
        }
        return out;
    }

    bool hasName(const localmedia::Listing &l, const std::string &n) {
        for (const auto &e: l.entries) {
            if (e.name == n) {
                return true;
            }
        }
        return false;
    }
}

TEST(localmedia_root_detection_with_mocked_storage) {
    std::string base = tree("lm-roots");
    // nothing mounted: no roots
    CHECK(localmedia::detectRoots(base).empty());
    fs::ensureDir(base + "/data");
    fs::ensureDir(base + "/mnt");
    fs::ensureDir(base + "/mnt/usb0");
    fs::ensureDir(base + "/mnt/usb3");
    fs::ensureDir(base + "/mnt/usb7");
    fs::ensureDir(base + "/mnt/usb5");
    chmod((base + "/mnt/usb5").c_str(), 0);   // mounted but not accessible to the app
    touch(base + "/mnt/usb6");                // a file, not a drive
    std::vector<localmedia::Root> roots = localmedia::detectRoots(base);
    std::vector<int> usb;
    bool data = false;
    for (const auto &r: roots) {
        if (r.usb) {
            usb.push_back(r.number);
        } else {
            data = r.path == base + "/data/";
        }
    }
    bool root = geteuid() == 0;   // root can open a 000 directory
    CHECK(data);
    CHECK((usb == (root ? std::vector<int>{1, 4, 6, 8} : std::vector<int>{1, 4, 8})));
    CHECK(roots.front().usb == false);   // internal storage first, then USB 1..8 in order
    chmod((base + "/mnt/usb5").c_str(), 0755);
    // a drive unplugged between two looks: gone at the next detection
    CHECK(std::system(("rm -rf '" + base + "/mnt/usb3'").c_str()) == 0);
    CHECK_EQ(localmedia::detectRoots(base).size(), (size_t) 4);
    // the console's own candidates are /data/ and /mnt/usb0..7/
    auto real = localmedia::candidateRoots();
    CHECK(real[0].path == "/data/" && real[8].path == "/mnt/usb7/" && real[8].number == 8);
}

TEST(localmedia_names_cases_and_folders) {
    std::string base = tree("lm-names");
    Root root = {base + "/", false, 0};
    std::string longName(240, 'L');
    longName += ".mkv";   // 244 bytes: within the 255-byte limit
    for (const std::string &n: {std::string("Film With Spaces (2024).MP4"), std::string("\xC3\x87ocuk Filmi \xC4\xB0yi.mkv"),
                                std::string("\xE6\x98\xA0\xE7\x94\xBB.Ts"), std::string("concert.M2TS"),
                                std::string("clip.webm"), std::string("song.FLAC"), std::string("audio.mp3"),
                                std::string("old.AVI"), std::string("home.mov"), std::string("radio.ogg"), longName,
                                std::string("noext"), std::string("readme.TXT"), std::string("poster.JPG"),
                                std::string("movie.srt"), std::string("movie.ass"), std::string("list.m3u8"),
                                std::string(".hidden.mkv")}) {
        touch(base + "/" + n);
    }
    fs::ensureDir(base + "/Empty Folder");
    fs::ensureDir(base + "/\xC3\x96zel Klas\xC3\xB6r");
    fs::ensureDir(base + "/.config");
    localmedia::Listing l = localmedia::list(root, base, sampleFormats());
    CHECK(l.ok && !l.truncated);
    std::vector<std::string> n = names(l);
    CHECK(n.size() == 2 + 11);
    CHECK(n[0] == "Empty Folder" && n[1] == "\xC3\x96zel Klas\xC3\xB6r");   // folders first
    for (const char *want: {"Film With Spaces (2024).MP4", "concert.M2TS", "clip.webm", "song.FLAC", "audio.mp3",
                            "old.AVI", "home.mov", "radio.ogg"}) {
        CHECK(hasName(l, want));
    }
    CHECK(hasName(l, "\xC3\x87ocuk Filmi \xC4\xB0yi.mkv") && hasName(l, "\xE6\x98\xA0\xE7\x94\xBB.Ts") && hasName(l, longName));
    for (const char *not_: {"noext", "readme.TXT", "poster.JPG", "movie.srt", "movie.ass", "list.m3u8", ".hidden.mkv",
                            ".config"}) {
        CHECK(!hasName(l, not_));
    }
    CHECK(l.skipped == 8);
    // an empty folder lists fine (the screen shows "No playable files here")
    localmedia::Listing e = localmedia::list(root, base + "/Empty Folder", sampleFormats());
    CHECK(e.ok && e.entries.empty());
    // paths with spaces / Unicode reach the player as local files
    for (const auto &entry: l.entries) {
        CHECK(localmedia::within(root, entry.path));
        CHECK(entry.path.find(base) == 0);
    }
}

TEST(localmedia_large_folders) {
    std::string base = tree("lm-large");
    Root root = {base + "/", false, 0};
    // thousands of pictures / subtitles never hide the videos (only shown entries count)
    for (int i = 0; i < 6000; i++) {
        touch(base + "/img" + std::to_string(i) + ".jpg");
    }
    for (int i = 0; i < 3; i++) {
        touch(base + "/video" + std::to_string(i) + ".mkv");
    }
    localmedia::Listing l = localmedia::list(root, base, sampleFormats());
    CHECK(l.ok && l.entries.size() == 3 && !l.truncated && l.skipped == 6000);
    // more playable files than one folder shows: capped and flagged
    std::string big = tree("lm-large2");
    for (size_t i = 0; i < localmedia::MAX_ENTRIES + 20; i++) {
        touch(big + "/ep" + std::to_string(i) + ".mp4");
    }
    localmedia::Listing b = localmedia::list({big + "/", false, 0}, big, sampleFormats());
    CHECK(b.ok && b.entries.size() == localmedia::MAX_ENTRIES && b.truncated);
    for (size_t i = 1; i < b.entries.size(); i++) {
        CHECK(!localmedia::naturalLess(b.entries[i].name, b.entries[i - 1].name));
    }
}

TEST(localmedia_symlinks_permissions_and_special_files) {
    std::string base = tree("lm-links");
    std::string outside = tree("lm-outside");
    touch(outside + "/secret.mkv");
    Root root = {base + "/", false, 0};
    fs::ensureDir(base + "/Movies");
    touch(base + "/Movies/a.mkv");
    // links inside the root, out of it, to a file outside, and dangling: none is followed or listed
    CHECK(symlink((base + "/Movies").c_str(), (base + "/LinkToMovies").c_str()) == 0);
    CHECK(symlink((base + "/Movies/a.mkv").c_str(), (base + "/link.mkv").c_str()) == 0);
    CHECK(symlink(outside.c_str(), (base + "/Escape").c_str()) == 0);
    CHECK(symlink((outside + "/secret.mkv").c_str(), (base + "/secret.mkv").c_str()) == 0);
    CHECK(symlink((base + "/missing").c_str(), (base + "/broken.mkv").c_str()) == 0);
    CHECK(symlink("/", (base + "/root").c_str()) == 0);
    mkfifo((base + "/pipe.mkv").c_str(), 0644);   // a FIFO named like a video: never opened
    fs::ensureDir(base + "/Locked");
    touch(base + "/Locked/inside.mkv");
    touch(base + "/unreadable.mkv");
    chmod((base + "/unreadable.mkv").c_str(), 0);
    chmod((base + "/Locked").c_str(), 0);
    localmedia::Listing l = localmedia::list(root, base, sampleFormats());
    CHECK(l.ok);
    std::vector<std::string> n = names(l);
    CHECK((n == std::vector<std::string>{"Locked", "Movies", "unreadable.mkv"}));
    // a listed name can never be a way out: every entry is a real directory / file inside the root
    for (const auto &e: l.entries) {
        CHECK(localmedia::within(root, e.path));
    }
    // a folder the app cannot read: refused (the screen says it cannot be opened), nothing leaks
    localmedia::Listing locked = localmedia::list(root, base + "/Locked", sampleFormats());
    if (geteuid() != 0) {
        CHECK(!locked.ok && locked.entries.empty());
    }
    // the link names themselves cannot be opened as folders (the screen only opens listed folders, and list()
    // refuses anything outside the root even when asked directly)
    CHECK(!localmedia::list(root, outside, sampleFormats()).ok);
    CHECK(!localmedia::list(root, base + "/Escape/..", sampleFormats()).ok);
    CHECK(!localmedia::list(root, base + "/../lm-outside", sampleFormats()).ok);
    chmod((base + "/Locked").c_str(), 0755);
    chmod((base + "/unreadable.mkv").c_str(), 0644);
}

TEST(localmedia_traversal_attempts_never_escape) {
    std::string base = tree("lm-escape");
    Root root = {base + "/", false, 0};
    fs::ensureDir(base + "/a");
    for (const std::string &p: {std::string("/etc"), std::string("/"), base + "/..", base + "/../", base + "/a/../..",
                                base + "/./../", base + "/a/./../../etc", base + "/%2e%2e", base + "/a/..%2f..",
                                base + "\\..\\..", std::string("..") , std::string("a/b"), std::string(""),
                                base + std::string("/a\0/../..", 9), base + "x", base + "/../" + base.substr(1)}) {
        localmedia::Listing l = localmedia::list(root, p, sampleFormats());
        bool inside = localmedia::within(root, p);
        if (l.ok) {
            CHECK(inside);   // anything that lists is inside the root
        }
        if (!inside) {
            CHECK(!l.ok && l.entries.empty());
        }
    }
    // percent-encoded dots are ordinary characters (no decoding): a folder literally named "%2e%2e" is inside
    CHECK(localmedia::within(root, base + "/%2e%2e"));
    CHECK(!localmedia::within(root, base + "/a/../../etc"));
    // the console's roots: /data never reaches /system, /mnt/usb0 never another drive
    Root data = {"/data/", false, 0};
    for (const char *p: {"/data/../system", "/data/./../../", "/data2", "/dat", "/mnt/usb0/../usb1", "//etc"}) {
        CHECK(!localmedia::within(data, p));
    }
    CHECK(localmedia::within(data, "//data//PS4IPTV/./media"));
}

TEST(localmedia_never_touches_the_iptv_library) {
    std::string dir = tree("lm-library");
    LibraryStore store(dir);
    store.setProfile("p1");
    VodProgress tracker(store);
    screens::VodItem it;
    it.type = iptv::ContentType::Movie;
    it.id = "local:/data/Movies/b.mkv";
    it.localPath = "/data/Movies/b.mkv";
    it.localMedia = true;
    it.title = "b";
    tracker.begin(it, true, 0);
    for (double t = 1; t < 400; t += 5) {
        tracker.observe(t, 3600, true, t);
        if (tracker.due(t) != VodProgress::Due::None) {
            tracker.record(t);
        }
    }
    tracker.record(500, 395);
    tracker.record(501, -1, true);
    CHECK(store.history().empty());
    CHECK(store.continueWatching(50).empty());
    CHECK(store.recentlyWatched(50).empty());
    CHECK(store.favorites(iptv::ContentType::Movie).empty());
    CHECK(store.progressOf(iptv::ContentType::Movie, it.id) == nullptr);
    CHECK(store.lastActivity(iptv::ContentType::Movie).empty());
    CHECK(store.save());
    std::string history;
    fs::readFile(fs::join(dir, "history.json"), history, 1 << 20);
    CHECK(history.find("local:") == std::string::npos && history.find("b.mkv") == std::string::npos);
}
#endif
