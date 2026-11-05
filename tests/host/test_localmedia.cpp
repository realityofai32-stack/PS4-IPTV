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
