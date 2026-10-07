// Persistent application log (from the hardware-validated playback test logger).
//
// - one file per session (<dir>/log.txt), the previous session is kept as <dir>/log.prev.txt
// - every line is redacted (redact::apply) before it is stored anywhere
// - thread safe: mpv/SDL callbacks may log from their own threads
// - each line is flushed so the file survives a crash
// - the most recent lines are kept in memory for the on-screen diagnostics page

#ifndef PS4IPTV_PLATFORM_LOG_H
#define PS4IPTV_PLATFORM_LOG_H

#include <string>
#include <vector>

namespace diag {

    enum class Level {
        Error = 0,
        Warn = 1,
        Info = 2,
        Verbose = 3
    };

    struct FileStatus {
        bool ok = false;
        std::string path;
        std::string detail;  // "writable" or the failing operation + errno text
    };

    void init(const std::string &dir);

    // Verbose lines are dropped unless enabled (Debug builds enable them at startup).
    void setVerbose(bool enabled);

    bool verbose();

    void shutdown();

    FileStatus fileStatus();

    double uptime();

    void write(Level level, const std::string &component, const std::string &message);

    void writef(Level level, const char *component, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

    std::vector<std::string> recent(size_t maxLines);

    // Copies the current log file to dest (e.g. a USB stick). Returns false with a reason on failure.
    bool copyLogTo(const std::string &dest, std::string &error);

    std::string format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
}

#define LOG_E(component, ...) diag::writef(diag::Level::Error, component, __VA_ARGS__)
#define LOG_W(component, ...) diag::writef(diag::Level::Warn, component, __VA_ARGS__)
#define LOG_I(component, ...) diag::writef(diag::Level::Info, component, __VA_ARGS__)
#define LOG_V(component, ...) diag::writef(diag::Level::Verbose, component, __VA_ARGS__)

#endif // PS4IPTV_PLATFORM_LOG_H
