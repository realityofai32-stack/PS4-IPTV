#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <sys/stat.h>
#include <sys/time.h>

#include "log.h"
#include "redact.h"

#ifdef __PS4__
#include <orbis/libkernel.h>
#endif

namespace {

    // Verbose mpv output is capped so a long session cannot fill the disk; errors and warnings
    // are always written.
    const long MAX_LOG_BYTES = 16L * 1024 * 1024;
    const size_t MAX_RECENT_LINES = 400;
    const size_t MAX_LINE_LENGTH = 1500;

    std::mutex g_mutex;
    FILE *g_file = nullptr;
    long g_bytes = 0;
    bool g_capNoticeWritten = false;
    diag::FileStatus g_status;
    std::deque<std::string> g_recent;
    double g_startTime = 0;
    bool g_verbose = false;

    double now() {
        struct timeval tv{};
        gettimeofday(&tv, nullptr);
        return (double) tv.tv_sec + (double) tv.tv_usec / 1000000.0;
    }

    const char *levelTag(diag::Level level) {
        switch (level) {
            case diag::Level::Error:
                return "E";
            case diag::Level::Warn:
                return "W";
            case diag::Level::Info:
                return "I";
            default:
                return "V";
        }
    }

    std::string errnoText(const char *operation, const std::string &path) {
        int e = errno;
        return diag::format("%s(%s) failed: errno %d (%s)", operation, path.c_str(), e, strerror(e));
    }

    bool ensureDir(const std::string &dir, std::string &error) {
        struct stat st{};
        if (stat(dir.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                return true;
            }
            error = dir + " exists but is not a directory";
            return false;
        }
        if (mkdir(dir.c_str(), 0777) != 0 && errno != EEXIST) {
            error = errnoText("mkdir", dir);
            return false;
        }
        return true;
    }
}

namespace diag {

    std::string format(const char *fmt, ...) {
        char buf[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        return buf;
    }

    void init(const std::string &dir) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_startTime = now();

        std::string base = dir;  // log directory
        while (!base.empty() && base.back() == '/') {
            base.pop_back();
        }
        g_status.path = base + "/log.txt";

        std::string error;
        if (!ensureDir(base, error)) {
            g_status.ok = false;
            g_status.detail = error;
            return;
        }

        // keep the previous session
        std::string prev = base + "/log.prev.txt";
        remove(prev.c_str());
        rename(g_status.path.c_str(), prev.c_str());

        g_file = fopen(g_status.path.c_str(), "w");
        if (g_file == nullptr) {
            g_status.ok = false;
            g_status.detail = errnoText("fopen", g_status.path);
            return;
        }

        // prove the file is really writable before relying on it
        const char *probe = "# PS4 IPTV log\n";
        size_t len = strlen(probe);
        if (fwrite(probe, 1, len, g_file) != len || fflush(g_file) != 0) {
            g_status.ok = false;
            g_status.detail = errnoText("fwrite", g_status.path);
            fclose(g_file);
            g_file = nullptr;
            return;
        }
        g_bytes = (long) len;
        g_status.ok = true;
        g_status.detail = "writable";
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file != nullptr) {
            fflush(g_file);
            fclose(g_file);
            g_file = nullptr;
        }
    }

    FileStatus fileStatus() {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_status;
    }

    double uptime() {
        return now() - g_startTime;
    }

    void setVerbose(bool enabled) {
        g_verbose = enabled;
    }

    bool verbose() {
        return g_verbose;
    }

    void write(Level level, const std::string &component, const std::string &message) {
        if (level == Level::Verbose && !g_verbose) {
            return;
        }
        std::string msg = redact::apply(message);
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
            msg.pop_back();
        }
        if (msg.size() > MAX_LINE_LENGTH) {
            msg = msg.substr(0, MAX_LINE_LENGTH) + "...(truncated)";
        }

        std::string line = format("[%9.3f] %s %-10s ", uptime(), levelTag(level), component.c_str()) + msg;

        std::lock_guard<std::mutex> lock(g_mutex);

        if (level != Level::Verbose) {
            g_recent.push_back(line);
            while (g_recent.size() > MAX_RECENT_LINES) {
                g_recent.pop_front();
            }
        }

        if (g_file != nullptr) {
            bool capped = g_bytes > MAX_LOG_BYTES;
            if (capped && !g_capNoticeWritten) {
                fputs("# log size cap reached: verbose/info lines are no longer written\n", g_file);
                g_capNoticeWritten = true;
            }
            if (!capped || level <= Level::Warn) {
                fputs(line.c_str(), g_file);
                fputc('\n', g_file);
                fflush(g_file);
                g_bytes += (long) line.size() + 1;
            }
        }

#ifdef __PS4__
        // mirror to the kernel debug output (same call libcross2d's PS4Sys::print uses)
        if (level != Level::Verbose) {
            std::string kline = "[PS4IPTV] " + line + "\n";
            sceKernelDebugOutText(0, kline.c_str());
        }
#endif
    }

    void writef(Level level, const char *component, const char *fmt, ...) {
        char buf[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        write(level, component, buf);
    }

    bool copyLogTo(const std::string &dest, std::string &error) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file == nullptr) {
            error = "no log file open (" + g_status.detail + ")";
            return false;
        }
        fflush(g_file);
        FILE *in = fopen(g_status.path.c_str(), "rb");
        if (in == nullptr) {
            error = errnoText("fopen", g_status.path);
            return false;
        }
        FILE *out = fopen(dest.c_str(), "wb");
        if (out == nullptr) {
            error = errnoText("fopen", dest);
            fclose(in);
            return false;
        }
        static char buf[64 * 1024];
        size_t n;
        bool ok = true;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
            if (fwrite(buf, 1, n, out) != n) {
                error = errnoText("fwrite", dest);
                ok = false;
                break;
            }
        }
        fclose(in);
        if (fclose(out) != 0 && ok) {
            error = errnoText("fclose", dest);
            ok = false;
        }
        return ok;
    }

    std::vector<std::string> recent(size_t maxLines) {
        std::lock_guard<std::mutex> lock(g_mutex);
        size_t n = std::min(maxLines, g_recent.size());
        return {g_recent.end() - (long) n, g_recent.end()};
    }
}
