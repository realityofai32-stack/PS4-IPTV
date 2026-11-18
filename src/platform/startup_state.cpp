#include <cctype>

#include "startup_state.h"
#include "fs.h"
#include "log.h"

namespace startup {

    namespace {
        const size_t MAX_FILE = 4096;
        const size_t MAX_NAME = 32;
        const size_t MAX_BUILD = 64;
        const int MAX_COUNT = 1000;

        bool validPhase(const std::string &p) {
            return p == "starting" || p == "running" || p == "exited";
        }

        std::string cleanBuild(const std::string &b) {
            std::string out;
            for (char c: b) {
                if (std::isalnum((unsigned char) c) || c == '.' || c == '-' || c == '+' || c == '_') {
                    out += c;
                }
                if (out.size() >= MAX_BUILD) {
                    break;
                }
            }
            return out;
        }

        int parseCount(const std::string &value, int max) {
            int n = 0;
            for (char c: value) {
                if (!std::isdigit((unsigned char) c)) {
                    return 0;
                }
                n = n * 10 + (c - '0');
                if (n > max) {
                    return max;
                }
            }
            return n;
        }

        int clampCount(int n, int max) {
            return n < 0 ? 0 : n > max ? max : n;
        }

        std::string joinSet(const std::set<std::string> &s) {
            std::string out;
            for (const auto &n: s) {
                out += (out.empty() ? "" : ",") + n;
            }
            return out;
        }
    }

    const std::set<std::string> &optionalStages() {
        static const std::set<std::string> stages = {KEYBOARD, PLAYER, SUBTITLE_FONT};
        return stages;
    }

    bool knownStage(const std::string &name) {
        static const std::set<std::string> names = {
                NETWORK_MODULE, RENDERER, APP_MEMBERS, RENDERER_FAILED, FONTS, NETWORK, JOBS, STORAGE, LIBRARY,
                DOWNLOADS, SCREENS, MAIN_LOOP, SHUTDOWN, KEYBOARD, PLAYER, SUBTITLE_FONT, BEGIN, FIRST_FRAME};
        return names.count(name) > 0;
    }

    std::string cleanName(const std::string &name) {
        std::string out;
        for (char c: name) {
            char l = (char) std::tolower((unsigned char) c);
            if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '-') {
                out += l;
            }
            if (out.size() >= MAX_NAME) {
                break;
            }
        }
        return out;
    }

    bool parse(const std::string &text, State &out) {
        out = State();
        if (text.empty() || text.size() > MAX_FILE || text.find('\0') != std::string::npos) {
            return false;
        }
        size_t start = 0;
        while (start < text.size()) {
            size_t nl = text.find('\n', start);
            std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            start = nl == std::string::npos ? text.size() : nl + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            if (line.empty() || line[0] == '#') {
                continue;
            }
            size_t eq = line.find('=');
            if (eq == std::string::npos) {
                continue;   // not ours: ignored
            }
            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            if (key == "build") {
                out.build = cleanBuild(value);
            } else if (key == "phase") {
                out.phase = value;
            } else if (key == "stage") {
                std::string n = cleanName(value);
                out.stage = knownStage(n) ? n : UNKNOWN;   // only the app's own names ever reach the log
            } else if (key == "skip") {
                size_t s = 0;
                while (s <= value.size()) {
                    size_t comma = value.find(',', s);
                    std::string n = cleanName(value.substr(s, comma == std::string::npos ? std::string::npos : comma - s));
                    if (optionalStages().count(n)) {
                        out.skip.insert(n);   // only optional stages can ever be skipped
                    }
                    if (comma == std::string::npos) {
                        break;
                    }
                    s = comma + 1;
                }
            } else if (key == "skip-starts") {
                out.skipStarts = parseCount(value, SKIP_STARTS);
            } else if (key == "failed") {
                out.failedStarts = parseCount(value, MAX_COUNT);
            }
        }
        if (!validPhase(out.phase)) {
            out = State();
            return false;
        }
        return true;
    }

    std::string serialize(const State &s) {
        return "# V7 Player startup state (no user data): the last startup stage begun\n"
               "build=" + cleanBuild(s.build) + "\n"
               "phase=" + (validPhase(s.phase) ? s.phase : std::string("starting")) + "\n"
               "stage=" + cleanName(s.stage) + "\n"
               "skip=" + joinSet(s.skip) + "\n"
               "skip-starts=" + std::to_string(s.skip.empty() ? 0 : clampCount(s.skipStarts, SKIP_STARTS)) + "\n"
               "failed=" + std::to_string(clampCount(s.failedStarts, MAX_COUNT)) + "\n";
    }

    const char *previousName(Previous p) {
        switch (p) {
            case Previous::None:
                return "none";
            case Previous::Unreadable:
                return "unreadable";
            case Previous::Exited:
                return "exited";
            case Previous::Closed:
                return "closed";
            default:
                return "died-starting";
        }
    }

    Verdict evaluate(const State *previous, bool unreadable, const std::string &build) {
        Verdict v;
        if (previous == nullptr) {
            v.previous = unreadable ? Previous::Unreadable : Previous::None;
            return v;
        }
        v.sameBuild = !build.empty() && cleanBuild(previous->build) == cleanBuild(build);
        bool exited = previous->phase == "exited";
        // skips carried over: same build, never past a normal exit, and only while their starts last
        if (v.sameBuild && !exited && previous->skipStarts > 1) {
            for (const auto &s: previous->skip) {
                if (optionalStages().count(s)) {
                    v.skip.insert(s);   // only optional stages can ever be skipped
                }
            }
            v.skipStarts = v.skip.empty() ? 0 : previous->skipStarts - 1;
        }
        if (exited) {
            v.previous = Previous::Exited;
        } else if (previous->phase == "running") {
            v.previous = Previous::Closed;
        } else {
            v.previous = Previous::DiedStarting;
            v.stage = knownStage(previous->stage) ? previous->stage : UNKNOWN;
            int before = v.sameBuild ? previous->failedStarts : 0;
            v.failedStarts = before >= MAX_COUNT ? MAX_COUNT : before + 1;
            if (v.sameBuild && optionalStages().count(v.stage) && !v.skip.count(v.stage)) {
                v.skip.insert(v.stage);
                v.newlySkipped = v.stage;
                v.skipStarts = SKIP_STARTS;   // a new crash: the whole set is skipped for SKIP_STARTS starts again
            }
        }
        return v;
    }

    void Tracker::begin(const std::string &dir, const std::string &build) {
        file = fs::join(dir, "startup.txt");
        reachedFirstFrame = false;
        std::string text;
        State prev;
        bool have = false;
        bool unreadable = false;
        if (fs::exists(file)) {
            std::string err;
            if (fs::readFile(file, text, MAX_FILE, &err) && parse(text, prev)) {
                have = true;
            } else {
                unreadable = true;
            }
        }
        v = evaluate(have ? &prev : nullptr, unreadable, build);
        state = State();
        state.build = build;
        state.phase = "starting";
        state.stage = BEGIN;
        state.skip = v.skip;
        state.skipStarts = v.skipStarts;
        state.failedStarts = v.failedStarts;
        write();
        if (file.empty()) {
            // this start's outcome cannot be recorded: the guard would act on a file that does not describe it
            v.guardOff = true;
            v.skip.clear();
            v.newlySkipped.clear();
            v.skipStarts = 0;
            LOG_W("startup", "crash guard off for this start: the startup state cannot be written");
        }
        switch (v.previous) {
            case Previous::DiedStarting:
                LOG_W("startup", "the previous start (%s build) stopped before its first frame, during stage '%s' "
                                 "(%d failed start(s) in a row)", v.sameBuild ? "same" : "another",
                      v.stage.empty() ? "?" : v.stage.c_str(), v.failedStarts);
                break;
            case Previous::Closed:
                LOG_I("startup", "the previous session reached its first frame and was closed from the system "
                                 "(or ended without the app's own exit)");
                break;
            case Previous::Unreadable:
                LOG_W("startup", "startup state file unreadable: ignored");
                break;
            default:
                LOG_I("startup", "previous start: %s", previousName(v.previous));
                break;
        }
        if (!v.newlySkipped.empty()) {
            LOG_W("startup", "crash guard: optional stage '%s' is skipped until the app is closed with its Exit, for "
                             "%d starts at most (Settings > Diagnostics > Startup check tries it again)",
                  v.newlySkipped.c_str(), SKIP_STARTS);
        }
        for (const auto &s: v.skip) {
            LOG_W("startup", "optional stage skipped by the crash guard: %s (%d start(s) left, this one included)",
                  s.c_str(), v.skipStarts);
        }
    }

    void Tracker::stage(const char *name) {
        state.stage = cleanName(name ? name : "");
        LOG_I("startup", "stage: %s%s", state.stage.c_str(), skipped(name) ? " (skipped by the crash guard)" : "");
        write();
    }

    bool Tracker::skipped(const char *name) const {
        return name != nullptr && v.skip.count(cleanName(name)) > 0;
    }

    void Tracker::firstFrame() {
        if (reachedFirstFrame) {
            return;
        }
        reachedFirstFrame = true;
        state.phase = "running";
        state.stage = FIRST_FRAME;
        state.failedStarts = 0;
        LOG_I("startup", "first frame drawn: startup complete");
        write();
    }

    void Tracker::exited() {
        state.phase = "exited";
        state.skip.clear();   // a normal clean exit ends every skip: the next start runs every stage
        state.skipStarts = 0;
        write();
    }

    void Tracker::clearSkips() {
        state.skip.clear();
        state.skipStarts = 0;
        v.skip.clear();
        v.newlySkipped.clear();
        v.skipStarts = 0;
        LOG_I("startup", "crash guard cleared: every optional stage runs at the next start");
        write();
    }

    void Tracker::write() {
        if (file.empty()) {
            return;
        }
        // no fsync: a crashed process keeps what it wrote (the kernel holds it); only a console power loss could not.
        // Each stage costs one small write + rename.
        std::string err;
        if (!fs::writeFileReplace(file, serialize(state), &err)) {
            LOG_W("startup", "startup state not written: %s", err.c_str());
            // an older state left behind would describe another start: removed (best effort), and no write attempt
            // per stage after a failure (the log still has every stage)
            fs::removeFile(file);
            file.clear();
        }
    }

    Tracker &tracker() {
        static Tracker t;
        return t;
    }
}
