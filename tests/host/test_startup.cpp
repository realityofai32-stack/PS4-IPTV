// Startup breadcrumbs and the crash guard (platform/startup_state.h), and the firmware version format
// (platform/system_info.h): v0.4.1 compatibility round.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "check.h"
#include "../../src/downloads/download_manager.h"
#include "../../src/platform/fs.h"
#include "../../src/platform/log.h"
#include "../../src/storage/library_store.h"
#include "../../src/storage/profile_store.h"
#include "../../src/storage/settings_store.h"
#include "../../src/platform/startup_state.h"
#include "../../src/platform/system_info.h"

using startup::Previous;
using startup::State;

namespace {
    std::string freshDir(const char *name) {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string d = fs::join(base ? base : ".", name);
        fs::removeFile(fs::join(d, "startup.txt"));
        fs::removeFile(fs::join(d, "startup.txt.tmp"));
        fs::ensureDir(d);
        return d;
    }

    State died(const std::string &build, const std::string &stage, int failed = 0) {
        State s;
        s.build = build;
        s.phase = "starting";
        s.stage = stage;
        s.failedStarts = failed;
        return s;
    }
}

TEST(startup_state_round_trip_and_damage) {
    State s;
    s.build = "0.4.1-abc1234";
    s.phase = "running";
    s.stage = "first-frame";
    s.skip = {"keyboard", "player"};
    s.skipStarts = 3;
    s.failedStarts = 3;
    State back;
    CHECK(startup::parse(startup::serialize(s), back));
    CHECK_EQ(back.build, s.build);
    CHECK_EQ(back.phase, s.phase);
    CHECK_EQ(back.stage, s.stage);
    CHECK(back.skip == s.skip);
    CHECK_EQ(back.skipStarts, 3);
    CHECK_EQ(back.failedStarts, 3);
    // CRLF, comments, unknown keys: tolerated
    CHECK(startup::parse("# x\r\nphase=starting\r\nstage=Keyboard\r\nwhat=ever\r\n", back));
    CHECK_EQ(back.stage, std::string("keyboard"));
    // damaged / foreign / hostile content: rejected or cleaned, never trusted
    CHECK(!startup::parse("", back));
    CHECK(!startup::parse("phase=sideways\n", back));
    CHECK(!startup::parse("stage=keyboard\n", back));                       // no phase
    CHECK(!startup::parse(std::string("phase=starting\n\0", 16), back));    // binary
    CHECK(!startup::parse(std::string(5000, 'a'), back));                   // oversized
    CHECK(startup::parse("phase=starting\nstage=../../etc/x\nskip=a b,,PLAYER,$(rm),renderer\n"
                         "skip-starts=99\nfailed=99999999999\n", back));
    CHECK_EQ(back.stage, std::string("unknown"));                           // only the app's own stage names
    CHECK((back.skip == std::set<std::string>{"player"}));                  // only optional stages
    CHECK_EQ(back.skipStarts, startup::SKIP_STARTS);
    CHECK_EQ(back.failedStarts, 1000);
    CHECK(startup::parse("phase=exited\nfailed=-4\nskip-starts=x\n", back));
    CHECK_EQ(back.failedStarts, 0);
    CHECK_EQ(back.skipStarts, 0);
    CHECK_EQ(startup::cleanName(std::string(80, 'k')).size(), (size_t) 32);
    // every name the app writes is known; nothing else is
    for (const char *n: {startup::NETWORK_MODULE, startup::RENDERER, startup::APP_MEMBERS, startup::RENDERER_FAILED,
                         startup::FONTS, startup::NETWORK, startup::JOBS, startup::STORAGE, startup::LIBRARY,
                         startup::DOWNLOADS, startup::SCREENS, startup::MAIN_LOOP, startup::SHUTDOWN,
                         startup::KEYBOARD, startup::PLAYER, startup::SUBTITLE_FONT, startup::BEGIN,
                         startup::FIRST_FRAME}) {
        CHECK(startup::knownStage(n) && startup::cleanName(n) == n);
    }
    CHECK(!startup::knownStage("unknown") && !startup::knownStage("") && !startup::knownStage("keyboardx"));
    // no skip: no starts are written for it
    s.skip.clear();
    CHECK(startup::serialize(s).find("skip-starts=0\n") != std::string::npos);
}

TEST(startup_crash_guard_rules) {
    const std::string build = "0.4.1-abc1234";
    const int N = startup::SKIP_STARTS;
    // first start / damaged file: nothing skipped
    startup::Verdict v = startup::evaluate(nullptr, false, build);
    CHECK(v.previous == Previous::None && v.skip.empty() && v.skipStarts == 0);
    v = startup::evaluate(nullptr, true, build);
    CHECK(v.previous == Previous::Unreadable && v.skip.empty());

    // died inside an optional stage of this build: skipped for N starts (this one included)
    State s = died(build, "keyboard");
    v = startup::evaluate(&s, false, build);
    CHECK(v.previous == Previous::DiedStarting && v.stage == "keyboard" && v.sameBuild);
    CHECK(v.skip.count("keyboard") == 1 && v.newlySkipped == "keyboard" && v.failedStarts == 1 && v.skipStarts == N);
    // another optional stage dies while one is skipped: both, for N starts again
    s = died(build, "player", 1);
    s.skip = {"keyboard"};
    s.skipStarts = 2;
    v = startup::evaluate(&s, false, build);
    CHECK((v.skip == std::set<std::string>{"keyboard", "player"}) && v.failedStarts == 2 && v.skipStarts == N);

    // a required stage is never skipped, only localized; skips already running count down
    s = died(build, "renderer", 4);
    s.skip = {"keyboard"};
    s.skipStarts = 3;
    v = startup::evaluate(&s, false, build);
    CHECK(v.previous == Previous::DiedStarting && v.stage == "renderer" && v.newlySkipped.empty());
    CHECK((v.skip == std::set<std::string>{"keyboard"}) && v.skipStarts == 2 && v.failedStarts == 5);
    s = died(build, "renderer-failed");
    v = startup::evaluate(&s, false, build);
    CHECK(v.skip.empty() && v.newlySkipped.empty());
    s = died(build, "unknown");
    v = startup::evaluate(&s, false, build);
    CHECK(v.stage == "unknown" && v.skip.empty());

    // another build (an update): every stage is tried again, the counter restarts
    s = died("0.4.0-71d0d87", "keyboard", 7);
    s.skip = {"player"};
    s.skipStarts = N;
    v = startup::evaluate(&s, false, build);
    CHECK(!v.sameBuild && v.skip.empty() && v.failedStarts == 1 && v.newlySkipped.empty());

    // closed from the system after the first frame: skips count down, nothing is added
    s = State();
    s.build = build;
    s.phase = "running";
    s.stage = "first-frame";
    s.skip = {"keyboard", "renderer"};   // a required name in the file is never honoured
    s.skipStarts = 4;
    v = startup::evaluate(&s, false, build);
    CHECK(v.previous == Previous::Closed && (v.skip == std::set<std::string>{"keyboard"}) && v.skipStarts == 3);
    CHECK(v.failedStarts == 0 && v.newlySkipped.empty());
    // ... and end when their starts are used up: the stage is tried again
    s.skipStarts = 1;
    v = startup::evaluate(&s, false, build);
    CHECK(v.previous == Previous::Closed && v.skip.empty() && v.skipStarts == 0);
    // a file without starts for its skips (or a hand-made one): nothing skipped
    s.skipStarts = 0;
    v = startup::evaluate(&s, false, build);
    CHECK(v.skip.empty());

    // a normal clean exit ends every skip, whatever starts were left
    s.phase = "exited";
    s.skipStarts = N;
    v = startup::evaluate(&s, false, build);
    CHECK(v.previous == Previous::Exited && v.skip.empty() && v.skipStarts == 0 && v.newlySkipped.empty());
    CHECK_EQ(std::string(startup::previousName(Previous::DiedStarting)), std::string("died-starting"));
}

// The ways a session ends, through the tracker as the app drives it (main.cpp, App): only a death inside an optional
// stage skips it, and no skip outlives a normal clean exit or its starts
TEST(startup_tracker_across_starts) {
    std::string dir = freshDir("startup-tracker");
    const std::string build = "0.4.1-abc1234";
    std::string text;
    State s;
    auto fileState = [&](const startup::Tracker &t) {
        State st;
        CHECK(fs::readFile(t.path(), text, 4096) && startup::parse(text, st));
        return st;
    };

    // start 1: dies in the keyboard stage (the process ends without firstFrame)
    {
        startup::Tracker t;
        t.begin(dir, build);
        CHECK(t.verdict().previous == Previous::None && !t.verdict().guardOff);
        t.stage(startup::RENDERER);
        t.stage(startup::KEYBOARD);
        CHECK(!t.skipped(startup::KEYBOARD));
        s = fileState(t);
        CHECK(s.phase == "starting" && s.stage == "keyboard" && s.build == build);
    }
    // start 2: the keyboard is skipped, startup completes; closed from the PS4 menu (no exit of its own)
    {
        startup::Tracker t;
        t.begin(dir, build);
        CHECK(t.verdict().previous == Previous::DiedStarting && t.verdict().stage == "keyboard");
        CHECK(t.skipped(startup::KEYBOARD) && t.skipped("Keyboard") && !t.skipped(startup::PLAYER));
        CHECK_EQ(t.verdict().skipStarts, startup::SKIP_STARTS);
        t.stage(startup::KEYBOARD);   // the stage marker still runs (and says it is skipped)
        t.stage(startup::PLAYER);
        t.firstFrame();
        CHECK(t.started());
        s = fileState(t);
        CHECK(s.phase == "running" && s.failedStarts == 0 && s.skip.count("keyboard") == 1);
    }
    // start 3: still skipped after a close from the system; this time the user leaves with the app's Exit
    {
        startup::Tracker t;
        t.begin(dir, build);
        CHECK(t.verdict().previous == Previous::Closed && t.skipped(startup::KEYBOARD));
        t.firstFrame();
        t.stage(startup::SHUTDOWN);
        t.exited();
        s = fileState(t);
        CHECK(s.phase == "exited" && s.skip.empty() && s.skipStarts == 0);
    }
    // start 4: after a normal clean exit nothing is skipped - the keyboard runs again (here it crashes again)
    {
        startup::Tracker t;
        t.begin(dir, build);
        CHECK(t.verdict().previous == Previous::Exited && t.verdict().skip.empty());
        CHECK(!t.skipped(startup::KEYBOARD));
        t.stage(startup::KEYBOARD);
    }
    // starts 5..: skipped again, but only for SKIP_STARTS starts when the app is only ever closed from the system
    for (int i = 0; i < startup::SKIP_STARTS; i++) {
        startup::Tracker t;
        t.begin(dir, build);
        CHECK(t.skipped(startup::KEYBOARD));
        CHECK_EQ(t.verdict().skipStarts, startup::SKIP_STARTS - i);
        t.stage(startup::KEYBOARD);
        t.firstFrame();
    }
    {
        startup::Tracker t;
        t.begin(dir, build);
        CHECK(t.verdict().previous == Previous::Closed && !t.skipped(startup::KEYBOARD) && t.verdict().skip.empty());
        t.firstFrame();
    }
    // the app's Exit before its first frame (a quit event in the first frame's logic): a clean exit, nothing skipped
    // although the last stage marker was an optional one
    {
        startup::Tracker t;
        t.begin(dir, build);
        t.stage(startup::KEYBOARD);
        t.stage(startup::MAIN_LOOP);
        t.stage(startup::SHUTDOWN);
        t.exited();
    }
    {
        startup::Tracker t;
        t.begin(dir, build);
        CHECK(t.verdict().previous == Previous::Exited && t.verdict().skip.empty());
        // closed from the system right after the first frame, during the first keyboard reads of the main loop:
        // the first frame was drawn, so no skip
        t.stage(startup::KEYBOARD);
        t.firstFrame();
    }
    {
        startup::Tracker t;
        t.begin(dir, build);
        CHECK(t.verdict().previous == Previous::Closed && t.verdict().skip.empty());
        // the renderer failed (the app exits with a notification, no first frame): localized, never skipped
        t.stage(startup::RENDERER);
        t.stage(startup::RENDERER_FAILED);
    }
    {
        startup::Tracker t;
        t.begin(dir, build);
        CHECK(t.verdict().previous == Previous::DiedStarting && t.verdict().stage == "renderer-failed");
        CHECK(t.verdict().skip.empty());
        // Settings > Diagnostics > Startup check clears a skip right away
        t.stage(startup::PLAYER);
    }
    {
        startup::Tracker t;
        t.begin(dir, build);
        CHECK(t.skipped(startup::PLAYER));
        t.clearSkips();
        CHECK(!t.skipped(startup::PLAYER) && t.verdict().skip.empty());
        s = fileState(t);
        CHECK(s.skip.empty() && s.skipStarts == 0);
    }
    {
        startup::Tracker t;
        t.begin(dir, build);
        CHECK(t.verdict().skip.empty());
        // a new build installed: nothing of the old build's skips survives
        t.stage(startup::PLAYER);
    }
    {
        startup::Tracker t;
        t.begin(dir, "0.4.2-def5678");
        CHECK(!t.verdict().sameBuild && t.verdict().skip.empty() && t.verdict().previous == Previous::DiedStarting);
    }

    // a damaged file: ignored, rewritten
    CHECK(fs::writeFileAtomic(fs::join(dir, "startup.txt"), std::string("\x01\x02garbage", 9)));
    {
        startup::Tracker t;
        t.begin(dir, build);
        CHECK(t.verdict().previous == Previous::Unreadable && t.verdict().skip.empty());
        s = fileState(t);
        CHECK(s.phase == "starting");
    }
    // an unwritable location: no crash, nothing skipped, the stages still run
    {
        startup::Tracker t;
        t.begin(fs::join(dir, "missing/dir/that/does/not/exist"), build);
        CHECK(t.verdict().guardOff);
        t.stage(startup::KEYBOARD);
        t.firstFrame();
        CHECK(!t.skipped(startup::KEYBOARD) && t.path().empty());
    }
#ifndef _WIN32
    // a stale file that can no longer be replaced (read-only folder): it says "died in the player stage", but this start
    // cannot record its own outcome - the guard is off, the player runs, start after start
    if (geteuid() != 0) {   // root writes anyway
        std::string ro = freshDir("startup-readonly");
        CHECK(fs::writeFileAtomic(fs::join(ro, "startup.txt"), startup::serialize(died(build, "player"))));
        chmod(ro.c_str(), 0555);
        for (int i = 0; i < 3; i++) {
            startup::Tracker t;
            t.begin(ro, build);
            CHECK(t.verdict().previous == Previous::DiedStarting && t.verdict().guardOff);
            CHECK(!t.skipped(startup::PLAYER) && t.verdict().skip.empty());
        }
        chmod(ro.c_str(), 0755);
    }
    // a file that can be replaced but whose write fails later: removed, so it never describes another start
    {
        std::string later = freshDir("startup-later");
        startup::Tracker t;
        t.begin(later, build);
        CHECK(fs::exists(t.path()));
        std::string path = t.path();
        CHECK(std::system(("mkdir '" + path + ".tmp'").c_str()) == 0);   // the next write's temporary file cannot be made
        t.stage(startup::PLAYER);
        CHECK(t.path().empty() && !fs::exists(path));   // the older state ("begin") is gone, not left to mislead
        CHECK(std::system(("rmdir '" + path + ".tmp'").c_str()) == 0);
    }
#endif
}

// What the startup diagnostics write to the log comes from a fixed vocabulary: whatever the state file holds (a
// hand-edited or foreign file with addresses, logins, MACs, paths), none of it reaches log.txt
TEST(startup_log_never_carries_file_content) {
    std::string dir = freshDir("startup-privacy");
    const std::string hostile =
            "build=http://exampleuser:examplepass@example.com:8080/get.php?username=exampleuser&password=examplepass\n"
            "phase=starting\n"
            "stage=http://example.com/live/exampleuser/examplepass/1.ts\n"
            "skip=player,00:1A:79:12:34:56,/usb0/Family Holiday/Kids.mkv,token=exampletoken\n"
            "skip-starts=3\n"
            "failed=2\n";
    CHECK(fs::writeFileAtomic(fs::join(dir, "startup.txt"), hostile));
    LOG_I("test", "startup-privacy-marker");
    startup::Tracker t;
    t.begin(dir, "0.4.1-abc1234");
    t.stage(startup::RENDERER);
    t.stage(startup::KEYBOARD);
    t.firstFrame();
    std::vector<std::string> lines = diag::recent(400);
    size_t from = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].find("startup-privacy-marker") != std::string::npos) {
            from = i + 1;
        }
    }
    CHECK(from > 0 && from < lines.size());
    bool sawPrevious = false;
    for (size_t i = from; i < lines.size(); i++) {
        const std::string &l = lines[i];
        for (const char *secret: {"exampleuser", "examplepass", "example.com", "exampletoken", "http", "://", "@",
                                  "1a79", "1A:79", "/usb0", "Family", "Kids", "get.php", "8080"}) {
            if (l.find(secret) != std::string::npos) {
                std::printf("  leaked '%s': %s\n", secret, l.c_str());
            }
            CHECK(l.find(secret) == std::string::npos);
        }
        sawPrevious = sawPrevious || l.find("during stage 'unknown'") != std::string::npos;
    }
    CHECK(sawPrevious);
    // the file this start writes holds the same closed vocabulary only
    std::string text;
    CHECK(fs::readFile(t.path(), text, 4096));
    CHECK(text.find("example") == std::string::npos && text.find("http") == std::string::npos);
    CHECK(text.find("usb0") == std::string::npos && text.find("1a79") == std::string::npos);
}

TEST(sysinfo_firmware_version_format) {
    CHECK_EQ(sysinfo::formatFirmware(0x09000001u), std::string("9.00"));
    CHECK_EQ(sysinfo::formatFirmware(0x11000000u), std::string("11.00"));
    CHECK_EQ(sysinfo::formatFirmware(0x12520001u), std::string("12.52"));
    CHECK_EQ(sysinfo::formatFirmware(0x13520001u), std::string("13.52"));
    CHECK_EQ(sysinfo::formatFirmware(0x14000000u), std::string("14.00"));
    CHECK_EQ(sysinfo::formatFirmware(0x06720001u), std::string("6.72"));
    CHECK_EQ(sysinfo::formatFirmware(0), std::string(""));
    CHECK_EQ(sysinfo::formatFirmware(0xFF000000u), std::string(""));
    CHECK_EQ(sysinfo::firmwareVersion(), std::string("host"));
}

// Every file the app reads while it starts (profiles, Stalker sidecar, settings, favorites, history, the download
// manifest) and its .bak, damaged in the ways a power loss / a full disk / an FTP mistake leaves them: the stores load
// defaults (or the other copy) and never crash. A crash here would happen before the first frame on any firmware.
TEST(startup_survives_damaged_data_files) {
    const std::vector<std::string> damage = {
            "", "{", "}", "null", "[]", "0", "\"text\"", std::string("\xff\xfe\x00\x01binary", 10),
            "{\"version\":1,\"profiles\":5,\"active\":[]}", "{\"version\":99999999999999999999}",
            "{\"version\":2,\"profiles\":[null,1,\"x\",{\"id\":5,\"name\":{}}]}",
            "{\"items\":[{\"key\":null,\"state\":\"bogus\",\"bytes\":-5,\"expected\":1e999}]}",
            std::string(200000, '['), std::string(100000, '{') + "\"a\":", "{\"a\":\"\\ud800\\udc00\\u0000\"}",
            "{\"language\":\"\\u0000xx\",\"accentColor\":12,\"keyboardLayout\":[],\"loadImages\":\"yes\"}"};
    const std::vector<std::string> files = {"profiles.json", "profiles.stalker.json", "settings.json",
                                            "favorites.json", "history.json"};
    int round = 0;
    for (const auto &bad: damage) {
        std::string dir = fs::join(freshDir("startup-damage"), std::to_string(round++));
        CHECK(std::system(("rm -rf '" + dir + "'").c_str()) == 0);
        fs::ensureDir(dir);
        fs::ensureDir(fs::join(dir, "downloads/metadata"));
        for (const auto &f: files) {
            CHECK(fs::writeFileAtomic(fs::join(dir, f), bad));
            CHECK(fs::writeFileAtomic(fs::join(dir, f + ".bak"), bad));
        }
        CHECK(fs::writeFileAtomic(fs::join(dir, "downloads/metadata/downloads.json"), bad));
        std::string w;
        ProfileStore profiles(dir + "/");
        profiles.load(&w);
        SettingsStore settings(dir + "/");
        settings.load(&w);
        CHECK(!settings.get().language.empty() || settings.get().language.empty());   // loaded without crashing
        LibraryStore library(dir + "/");
        library.load(&w);
        dl::ManagerConfig cfg;
        cfg.root = fs::join(dir, "downloads/");
        cfg.clock = [] { return 0.0; };
        cfg.wallClock = [] { return (int64_t) 0; };
        cfg.freeSpace = [](const std::string &) { return (int64_t) -1; };
        dl::DownloadManager manager(cfg);
        manager.load(false, &w);
        CHECK(profiles.profiles().size() <= 1);   // nothing invented from garbage
    }
    // a directory where a file should be (an FTP client creating "settings.json/")
    std::string dir = fs::join(freshDir("startup-damage"), "dirs");
    CHECK(std::system(("rm -rf '" + dir + "'").c_str()) == 0);
    for (const auto &f: files) {
        fs::ensureDir(fs::join(dir, f));
    }
    fs::ensureDir(fs::join(dir, "downloads/metadata/downloads.json"));
    std::string w;
    ProfileStore profiles(dir + "/");
    profiles.load(&w);
    SettingsStore settings(dir + "/");
    settings.load(&w);
    LibraryStore library(dir + "/");
    library.load(&w);
    dl::ManagerConfig cfg;
    cfg.root = fs::join(dir, "downloads/");
    cfg.clock = [] { return 0.0; };
    cfg.wallClock = [] { return (int64_t) 0; };
    cfg.freeSpace = [](const std::string &) { return (int64_t) -1; };
    dl::DownloadManager manager(cfg);
    manager.load(false, &w);
    CHECK(profiles.profiles().empty());
}
