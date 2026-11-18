// Startup breadcrumbs and the crash guard for optional startup stages (host-testable).
//
// Every startup stage (renderer, fonts, storage, keyboard, player...) is written to the log and to a small state file,
// <data>/logs/startup.txt, before it runs. A start that dies before its first frame leaves that file at the stage it
// died in, so the next start (and anyone reading the file over FTP) knows where it stopped, even when the log itself
// was not flushed far enough to tell.
//
// Crash guard: when the previous start of the SAME build died inside an OPTIONAL stage (the physical keyboard
// library, the playback backend, the subtitle font copy), the next starts skip that stage: the app opens without it
// instead of crashing on every launch. A skip is never permanent. It ends at the first of:
//   - a normal clean exit (the app's own Exit): the next start runs every stage again;
//   - SKIP_STARTS starts (the stage is then tried again; if it crashes again, it is skipped again);
//   - a new build;
//   - Settings > Diagnostics > Startup check.
// Nothing else ever adds a skip: the app's Exit (even before its first frame), a close from the system menu after the
// first frame, a renderer failure and a death in a required stage (renderer, storage...: only localized) do not. When
// this start cannot write the state file, the guard is off for it (a stale file must not decide anything).
//
// The file holds no user data: build id, phase, stage names from a fixed list, two counters. Unreadable / damaged
// files are ignored; a stage name the app does not use is read back as "unknown".

#ifndef PS4IPTV_PLATFORM_STARTUP_STATE_H
#define PS4IPTV_PLATFORM_STARTUP_STATE_H

#include <set>
#include <string>

namespace startup {

    // the stages, in startup order (main.cpp, App::App, App::run). Names are written to the log and the state file.
    constexpr const char *NETWORK_MODULE = "network-module";   // libSceNet system module
    constexpr const char *RENDERER = "renderer";               // libcross2d / SDL2: pads, Piglet GL ES, window, shaders
    constexpr const char *APP_MEMBERS = "app-members";         // App's members (stores, services, image loader)
    constexpr const char *RENDERER_FAILED = "renderer-failed"; // no GL context: the app exits with a notification
    constexpr const char *STORAGE = "storage";                 // profiles, settings (then the loading screen)
    constexpr const char *FONTS = "fonts";
    constexpr const char *NETWORK = "network";                 // libcurl / mbedTLS
    constexpr const char *JOBS = "jobs";                       // worker threads
    constexpr const char *LIBRARY = "library";                 // favorites, history, progress
    constexpr const char *DOWNLOADS = "downloads";
    constexpr const char *SCREENS = "screens";
    constexpr const char *MAIN_LOOP = "main-loop";
    constexpr const char *SHUTDOWN = "shutdown";
    // optional stages: their failure must not keep the app from opening
    constexpr const char *KEYBOARD = "keyboard";               // libSceKeyboard (loaded at run time)
    constexpr const char *PLAYER = "player";                   // mpv + FFmpeg + mpv's GL render context
    constexpr const char *SUBTITLE_FONT = "subtitle-font";     // copy of the subtitle font for libass
    // markers of the state file around the stages
    constexpr const char *BEGIN = "begin";
    constexpr const char *FIRST_FRAME = "first-frame";
    constexpr const char *UNKNOWN = "unknown";                 // a name read back that the app never writes

    // starts that skip a stage after it crashed, at most (the next normal exit ends it sooner)
    constexpr int SKIP_STARTS = 5;

    const std::set<std::string> &optionalStages();

    // one of the names above
    bool knownStage(const std::string &name);

    struct State {
        std::string build;            // the build that wrote the file
        std::string phase;            // "starting" (no first frame yet), "running", "exited"
        std::string stage;            // the last stage begun
        std::set<std::string> skip;   // optional stages skipped by this build
        int skipStarts = 0;           // starts that skip `skip`, the one that wrote the file included (1..SKIP_STARTS)
        int failedStarts = 0;         // consecutive starts that did not reach their first frame
    };

    // key=value lines; false when the text is not a state file (empty, binary, oversized, no phase). Values are cleaned:
    // stage names outside the fixed list become "unknown", skips other than the optional stages are dropped.
    bool parse(const std::string &text, State &out);

    std::string serialize(const State &s);

    enum class Previous {
        None,          // no state file: first start (or the file was removed)
        Unreadable,    // damaged file: ignored
        Exited,        // left through the app's own exit
        Closed,        // reached its first frame, ended without the app's exit (closed from the PS4 menu, or a crash)
        DiedStarting,  // never reached its first frame: stopped in `stage`
    };

    const char *previousName(Previous p);

    struct Verdict {
        Previous previous = Previous::None;
        std::string stage;              // DiedStarting: the stage it stopped in
        bool sameBuild = false;         // the previous start was this build
        std::set<std::string> skip;     // optional stages this start skips
        std::string newlySkipped;       // the stage added to `skip` by this start ("" when none)
        int skipStarts = 0;             // starts that skip `skip`, this one included (0 when nothing is skipped)
        int failedStarts = 0;           // including the previous one when it DiedStarting
        bool guardOff = false;          // the state file could not be written: nothing is skipped by this start
    };

    // what this start does, from the previous start's state (nullptr: no file; unreadable: a damaged file)
    Verdict evaluate(const State *previous, bool unreadable, const std::string &build);

    // stage names written to the file: lower-case letters, digits and '-' only (anything else is dropped)
    std::string cleanName(const std::string &name);

    class Tracker {

    public:

        // reads the previous state (logs what it says) and records this start as "starting". If that record cannot be
        // written, the crash guard is off for this start.
        void begin(const std::string &dir, const std::string &build);

        // a stage begins: logged, written to the state file
        void stage(const char *name);

        // an optional stage this start must skip (the crash guard)
        bool skipped(const char *name) const;

        // the first frame was drawn: startup succeeded
        void firstFrame();

        // the app leaves through its own exit path: a normal clean exit. Every skip ends here (the next start runs every
        // stage again).
        void exited();

        // Settings > Diagnostics: every optional stage is tried again at the next start
        void clearSkips();

        const Verdict &verdict() const { return v; }

        bool started() const { return reachedFirstFrame; }

        const std::string &currentStage() const { return state.stage; }

        const std::string &path() const { return file; }

    private:

        void write();

        std::string file;
        State state;
        Verdict v;
        bool reachedFirstFrame = false;
    };

    // the app's tracker
    Tracker &tracker();
}

#endif // PS4IPTV_PLATFORM_STARTUP_STATE_H
