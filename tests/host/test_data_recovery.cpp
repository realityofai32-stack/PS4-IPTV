// Existing user data at startup (V7 Player): data written by v0.4.0, by this build, and every data file missing,
// empty, truncated, malformed, binary, from a newer version, oversized or replaced by a directory. The rule checked
// here: the app starts with what it can read, and a file it cannot read is never destroyed - its exact bytes stay on
// disk (<file>.damaged, fs::keepDamagedCopy) through later saves; nothing a damaged file did not contain is lost
// (Stalker / media server sources come back from their side files, partial downloads stay).

#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "check.h"
#include "data_v040_fixture.h"
#include "../../src/downloads/download_manager.h"
#include "../../src/media/user_playlists.h"
#include "../../src/platform/fs.h"
#include "../../src/platform/log.h"
#include "../../src/storage/library_store.h"
#include "../../src/storage/profile_store.h"
#include "../../src/storage/settings_store.h"

namespace {
    std::string freshDir(const std::string &name) {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string dir = fs::join(base ? base : ".", "recovery-" + name);
        CHECK(std::system(("rm -rf '" + dir + "'").c_str()) == 0);
        fs::ensureDir(dir);
        return dir + "/";
    }

    // the v0.4.0 data folder, with its downloaded movie and the paused movie's partial file
    void writeV040(const std::string &dir) {
        for (const auto &f: fixture040::FILES) {
            std::string path = dir + f.path;
            fs::ensureDir(path.substr(0, path.find_last_of('/')));
            CHECK(fs::writeFileAtomic(path, f.text));
            fs::removeFile(path + ".bak");
        }
        fs::ensureDir(dir + "downloads/movies");
        fs::ensureDir(dir + "downloads/temp");
        CHECK(fs::writeFileAtomic(dir + "downloads/movies/movie_501.mkv", std::string(4096, 'm')));
        CHECK(fs::writeFileAtomic(dir + "downloads/temp/movie_502.mp4.part", std::string(2048, 'p')));
    }

    dl::ManagerConfig downloadConfig(const std::string &dir) {
        dl::ManagerConfig cfg;
        cfg.root = dir + "downloads/";
        cfg.clock = [] { return 0.0; };
        cfg.wallClock = [] { return (int64_t) 1800000000; };
        cfg.freeSpace = [](const std::string &) { return (int64_t) -1; };
        return cfg;
    }

    std::string read(const std::string &path) {
        std::string text;
        fs::readFile(path, text, 64u * 1024 * 1024);
        return text;
    }

    struct Damage {
        const char *name;
        std::function<std::string(const std::string &original)> make;
    };

    const std::vector<Damage> &damages() {
        static const std::vector<Damage> d = {
                {"empty", [](const std::string &) { return std::string(); }},
                {"truncated", [](const std::string &o) { return o.substr(0, o.size() / 2); }},
                {"malformed", [](const std::string &o) { return "{\"version\": 2, " + o.substr(o.size() / 3); }},
                {"binary", [](const std::string &) { return std::string("\x89\x00\xff\xfe garbage \x01\x02", 16); }},
                {"newer version", [](const std::string &) { return std::string("{\"version\": 99, \"profiles\": []}"); }},
                {"oversized", [](const std::string &o) { return o + std::string(9u * 1024 * 1024, ' '); }},
        };
        return d;
    }
}

// v0.4.0's data (profiles of all three types it knew, the Stalker side file, settings, favorites, history, downloads)
// loads completely with this build, and saving it again keeps everything
TEST(data_v040_upgrade_keeps_everything) {
    std::string dir = freshDir("v040");
    writeV040(dir);
    std::string w;
    ProfileStore profiles(dir);
    CHECK(profiles.load(&w));
    CHECK_EQ(profiles.profiles().size(), (size_t) 3);
    CHECK_EQ(profiles.activeId(), std::string("p1"));
    CHECK(profiles.find("p1") && profiles.find("p1")->password == "examplepass");
    CHECK(profiles.find("p2") && profiles.find("p2")->isPlaylist() && profiles.find("p2")->playlistChannels == 3);
    CHECK(profiles.find("p3") && profiles.find("p3")->isStalker() && profiles.find("p3")->mac == "00:1A:79:00:00:01");
    SettingsStore settings(dir);
    CHECK(settings.load(&w));
    CHECK_EQ(settings.get().accentColor, std::string("green"));   // the next launch's loading screen: green
    CHECK_EQ(settings.get().language, std::string("tr"));
    CHECK(settings.get().playlistOrder.empty() && settings.get().hiddenPlaylists.empty());
    LibraryStore library(dir);
    CHECK(library.load(&w));
    library.setProfile("p1");
    CHECK(library.isFavorite(iptv::ContentType::Live, "101") && library.isFavorite(iptv::ContentType::Movie, "501"));
    const HistoryEntry *progress = library.progressOf(iptv::ContentType::Movie, "501");
    CHECK(progress != nullptr && progress->position == 1200);
    CHECK(progress != nullptr && progress::continueEligible(*progress));   // still on Continue Watching
    // v0.4.0 read local playlist files from <data>/playlists/ as M3U sources: the same files are My Playlists now
    fs::ensureDir(dir + "playlists");
    CHECK(fs::writeFileAtomic(dir + "playlists/Family.m3u",
                              "#EXTM3U\n#EXTINF:-1 tvg-id=\"a.example\",Channel A\nhttp://stream.example.com/a.ts\n"
                              "#EXTINF:-1,Channel B\nhttp://stream.example.com/b.ts\n"));
    std::vector<userpl::Summary> mine = userpl::list(dir + "playlists");
    CHECK(mine.size() == 1 && mine[0].file == "Family.m3u" && mine[0].editable && mine[0].entries == 2);
    userpl::Playlist family;
    std::string perr;
    CHECK(userpl::load(dir + "playlists/Family.m3u", family, perr) && family.entries.size() == 2);
    dl::DownloadManager downloads(downloadConfig(dir));
    w.clear();
    CHECK(downloads.load(false, &w));
    CHECK(w.empty());
    std::vector<dl::Item> items = downloads.items();
    CHECK_EQ(items.size(), (size_t) 2);
    CHECK(fs::exists(dir + "downloads/temp/movie_502.mp4.part"));
    CHECK(fs::exists(dir + "downloads/movies/movie_501.mkv"));

    // saved by this build, read again: the same data
    CHECK(profiles.save());
    CHECK(settings.save());
    CHECK(library.save());
    ProfileStore again(dir);
    CHECK(again.load());
    CHECK_EQ(again.profiles().size(), (size_t) 3);
    SettingsStore settingsAgain(dir);
    CHECK(settingsAgain.load());
    CHECK_EQ(settingsAgain.get().accentColor, std::string("green"));
    LibraryStore libraryAgain(dir);
    CHECK(libraryAgain.load());
    libraryAgain.setProfile("p1");
    CHECK(libraryAgain.progressOf(iptv::ContentType::Movie, "501") != nullptr);
    CHECK(!fs::exists(dir + "profiles.json.damaged") && !fs::exists(dir + "settings.json.damaged"));
    CHECK(read(dir + "playlists/Family.m3u").find("Channel B") != std::string::npos);   // never rewritten by loading
}

// data written by this build (every source type, the playlist order and visibility, an accent) reads back unchanged
TEST(data_current_build_round_trip) {
    std::string dir = freshDir("current");
    {
        ProfileStore s(dir);
        CHECK(s.load());
        iptv::Profile x;
        x.name = "Xtream";
        x.server = "http://example.com:8080";
        x.username = "exampleuser";
        x.password = "examplepass";
        s.upsert(x, 1);
        iptv::Profile j;
        j.type = iptv::SourceType::Jellyfin;
        j.name = "Jellyfin";
        j.server = "http://192.168.1.10:8096";
        j.username = "exampleuser";
        j.password = "examplepass";
        s.upsert(j, 2);
        iptv::Profile a;
        a.type = iptv::SourceType::Stremio;
        a.name = "Add-on";
        a.playlistUrl = "https://addon.example.com/manifest.json";
        s.upsert(a, 3);
        iptv::Profile k;
        k.type = iptv::SourceType::Stalker;
        k.name = "Portal";
        k.server = "http://portal.example.com/c/";
        k.mac = "00:1A:79:00:00:02";
        s.upsert(k, 4);
        CHECK(s.save());
        SettingsStore st(dir);
        st.get().accentColor = "magenta";
        st.get().playlistOrder = {"b.m3u", "a.m3u"};
        st.get().hiddenPlaylists = {"a.m3u"};
        CHECK(st.save());
    }
    ProfileStore s(dir);
    CHECK(s.load());
    CHECK_EQ(s.profiles().size(), (size_t) 4);
    int jellyfin = 0, stremio = 0, stalker = 0;
    for (const auto &p: s.profiles()) {
        jellyfin += p.type == iptv::SourceType::Jellyfin;
        stremio += p.type == iptv::SourceType::Stremio;
        stalker += p.isStalker();
    }
    CHECK(jellyfin == 1 && stremio == 1 && stalker == 1);
    SettingsStore st(dir);
    CHECK(st.load());
    CHECK_EQ(st.get().accentColor, std::string("magenta"));
    CHECK(st.get().playlistOrder.size() == 2 && st.get().hiddenPlaylists.count("a.m3u") == 1);
}

// every data file, damaged every way: the store starts with defaults (or the other copy), the damaged bytes are kept
// as <file>.damaged, and two later saves do not touch that copy
TEST(data_damaged_files_are_kept_through_saves) {
    const std::vector<std::string> files = {"profiles.json", "settings.json", "favorites.json", "history.json",
                                            "downloads/metadata/downloads.json"};
    int round = 0;
    for (const auto &file: files) {
        for (const auto &d: damages()) {
            std::string dir = freshDir("damage-" + std::to_string(round++));
            writeV040(dir);
            std::string original;
            for (const auto &f: fixture040::FILES) {
                if (file == f.path) {
                    original = f.text;
                }
            }
            const std::string bad = d.make(original);
            const std::string path = dir + file;
            CHECK(fs::writeFileAtomic(path, bad));
            fs::removeFile(path + ".bak");   // no older copy to fall back on: the worst case

            std::string w;
            ProfileStore profiles(dir);
            profiles.load(&w);
            SettingsStore settings(dir);
            settings.load(&w);
            LibraryStore library(dir);
            library.load(&w);
            dl::DownloadManager downloads(downloadConfig(dir));
            downloads.load(false, &w);
            dl::Credentials creds;
            creds.profileId = "p1";
            creds.name = "Example Xtream";
            creds.server = "http://example.com:8080";
            creds.username = "exampleuser";
            creds.password = "examplepass";
            downloads.setProfiles({creds});

            // an empty file holds nothing to keep; everything else is kept byte for byte. Settings read a newer
            // version's file leniently (the keys they know), like every version did: not damaged.
            const bool lenient = std::string(d.name) == "newer version" && file == "settings.json";
            const bool keeps = !bad.empty() && !lenient;
            if (keeps != (read(path + ".damaged") == bad && fs::exists(path + ".damaged"))) {
                std::printf("  FAIL detail: %s, %s: damaged copy %s\n", file.c_str(), d.name,
                            fs::exists(path + ".damaged") ? "differs" : "missing");
            }
            CHECK(keeps == (fs::exists(path + ".damaged") && read(path + ".damaged") == bad));
            // the partial download is never removed because a list could not be read
            CHECK(fs::exists(dir + "downloads/temp/movie_502.mp4.part"));
            CHECK(fs::exists(dir + "downloads/movies/movie_501.mkv"));

            // two saves of everything (the user changes things): the kept copy stays as it was
            for (int k = 0; k < 2; k++) {
                iptv::Profile p;
                p.name = "New " + std::to_string(k);
                p.server = "http://example.com";
                profiles.upsert(p, 10 + k);
                CHECK(profiles.save());
                settings.get().accentColor = k == 0 ? "red" : "teal";
                CHECK(settings.save());
                library.setProfile("p1");
                library.toggleFavorite(iptv::ContentType::Live, "9" + std::to_string(k));
                CHECK(library.save());
                dl::Item it;   // a new download: the manager writes its list again
                it.kind = dl::Kind::Movie;
                it.profileId = "p1";
                it.contentId = "60" + std::to_string(k);
                it.title = "Queued";
                it.extension = "mkv";
                downloads.enqueue(it);
            }
            if (keeps) {
                CHECK(read(path + ".damaged") == bad);
            }
            // the rest of the folder still loads: one damaged file never costs another file's data
            if (file != "profiles.json") {
                ProfileStore check(dir);
                CHECK(check.load() && check.find("p1") != nullptr && check.find("p3") != nullptr);
            }
            if (file != "settings.json") {
                SettingsStore check(dir);
                CHECK(check.load() && check.get().accentColor == "teal");
            }
        }
    }
}

// profiles.json and its .bak unreadable: the Stalker and media server sources come back from the side files this
// build writes, nothing signs in by itself, and the side files survive the next saves
TEST(data_unreadable_profiles_restore_side_files) {
    std::string dir = freshDir("sidecars");
    {
        ProfileStore s(dir);
        iptv::Profile j;
        j.type = iptv::SourceType::Jellyfin;
        j.name = "Jellyfin";
        j.server = "http://192.168.1.10:8096";
        j.username = "exampleuser";
        s.upsert(j, 1);
        iptv::Profile k;
        k.type = iptv::SourceType::Stalker;
        k.name = "Portal";
        k.server = "http://portal.example.com/c/";
        k.mac = "00:1A:79:00:00:03";
        s.upsert(k, 2);
        iptv::Profile x;
        x.name = "Xtream";
        x.server = "http://example.com";
        s.upsert(x, 3);
        CHECK(s.save());
        CHECK(s.save());
    }
    CHECK(fs::writeFileAtomic(dir + "profiles.json", "{\"version\":2,\"profiles\":[{\"id\":\"p1\""));
    CHECK(fs::writeFileAtomic(dir + "profiles.json.bak", "\x01\x02 not json"));
    ProfileStore s(dir);
    std::string w;
    CHECK(!s.load(&w));
    CHECK(w.find("2 source(s) restored") != std::string::npos);
    CHECK(w.find("kept as profiles.json.damaged") != std::string::npos);
    CHECK_EQ(s.profiles().size(), (size_t) 2);
    CHECK(s.activeId().empty() && s.active() == nullptr);
    CHECK(fs::exists(dir + "profiles.json.damaged") && fs::exists(dir + "profiles.json.bak.damaged"));
    iptv::Profile x;
    x.name = "Again";
    x.server = "http://example.com";
    s.upsert(x, 4);
    CHECK(s.save());
    CHECK(s.save());
    ProfileStore t(dir);
    CHECK(t.load());
    CHECK_EQ(t.profiles().size(), (size_t) 3);
    CHECK(fs::exists(dir + "profiles.stalker.json") && fs::exists(dir + "profiles.mediaservers.json"));
}

// a directory where a data file belongs (an FTP mistake): defaults, saves fail cleanly, the directory and what is in
// it are never moved or removed
TEST(data_directory_in_place_of_a_file) {
    std::string dir = freshDir("dirs");
    writeV040(dir);
    for (const char *f: {"settings.json", "favorites.json"}) {
        fs::removeFile(dir + f);
        fs::ensureDir(dir + f);
        CHECK(fs::writeFileAtomic(dir + f + "/note.txt", "user file"));
    }
    std::string w;
    SettingsStore settings(dir);
    settings.load(&w);
    CHECK_EQ(settings.get().accentColor, std::string("blue"));   // defaults: the loading screen is V7 Blue
    std::string err;
    CHECK(!settings.save(&err));
    CHECK(err.find("is a directory") != std::string::npos);
    LibraryStore library(dir);
    library.load(&w);
    library.setProfile("p1");
    library.toggleFavorite(iptv::ContentType::Live, "7");
    CHECK(!library.save());
    CHECK(read(dir + "settings.json/note.txt") == "user file" && read(dir + "favorites.json/note.txt") == "user file");
    CHECK(!fs::exists(dir + "settings.json.damaged"));
    // the other files are untouched
    ProfileStore profiles(dir);
    CHECK(profiles.load() && profiles.profiles().size() == 3);
}

// fs::keepDamagedCopy: identical content is kept once, at most three different versions, never a directory
TEST(data_keep_damaged_copy_rules) {
    std::string dir = freshDir("keep");
    const std::string p = dir + "x.json";
    CHECK(fs::keepDamagedCopy(p).empty());   // no file
    CHECK(fs::writeFileAtomic(p, "one"));
    CHECK_EQ(fs::keepDamagedCopy(p), p + ".damaged");
    CHECK_EQ(fs::keepDamagedCopy(p), p + ".damaged");   // the same bytes: no second copy
    CHECK(fs::writeFileAtomic(p, "two"));
    CHECK_EQ(fs::keepDamagedCopy(p), p + ".damaged-2");
    CHECK(fs::writeFileAtomic(p, "three"));
    CHECK_EQ(fs::keepDamagedCopy(p), p + ".damaged-3");
    CHECK(fs::writeFileAtomic(p, "four"));
    CHECK(fs::keepDamagedCopy(p).empty());   // the three oldest stay
    CHECK(read(p + ".damaged") == "one" && read(p + ".damaged-2") == "two" && read(p + ".damaged-3") == "three");
    fs::ensureDir(dir + "d.json");
    CHECK(fs::keepDamagedCopy(dir + "d.json").empty());
    // a large file is copied by streaming, exactly
    std::string big(3u * 1024 * 1024 + 17, 'z');
    big[12345] = '\0';
    CHECK(fs::writeFileAtomic(dir + "big.json", big));
    CHECK_EQ(fs::keepDamagedCopy(dir + "big.json"), dir + "big.json.damaged");
    CHECK(read(dir + "big.json.damaged") == big);
}

// the log folder cannot be written (a file in its place, a full or read-only drive): startup goes on, every log call
// still works (the recent lines stay in memory for Settings > Diagnostics), and a later start with a writable folder
// logs to the file again
TEST(data_log_folder_unwritable) {
    std::string dir = freshDir("log");
    CHECK(fs::writeFileAtomic(dir + "logs", "a file where the log folder belongs"));
    diag::init(dir + "logs");
    diag::FileStatus st = diag::fileStatus();
    CHECK(!st.ok && !st.detail.empty());
    LOG_I("test", "log-unwritable-marker");
    LOG_W("test", "%s", std::string(5000, 'x').c_str());   // long lines: cut, never a crash
    bool seen = false;
    for (const auto &l: diag::recent(50)) {
        seen |= l.find("log-unwritable-marker") != std::string::npos;
    }
    CHECK(seen);
    CHECK(read(dir + "logs") == "a file where the log folder belongs");   // never replaced
    diag::init(dir + "logs2");
    CHECK(diag::fileStatus().ok);
    LOG_I("test", "log-writable-again");
    diag::shutdown();
    CHECK(read(dir + "logs2/log.txt").find("log-writable-again") != std::string::npos);
}
