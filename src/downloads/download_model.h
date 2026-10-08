// Offline downloads: data model and the pure decisions of the download engine (host-testable).
//
// A download is the provider's original movie / episode file, byte for byte (no transcoding: resolution,
// bitrate, every audio and subtitle track and the container metadata are kept). Layout under the downloads
// root (APP_DATA_DIR "downloads/"):
//
//   movies/movie_<id>.<ext>        completed movies
//   episodes/episode_<id>.<ext>    completed episodes
//   temp/<file>.part               transfers in progress (renamed into place only when complete)
//   metadata/downloads.json        the manifest (written atomically: tmp + fsync + rename)
//
// The manifest is the source of truth - the app never infers downloads from file names. It holds everything
// needed to list and play a download offline (titles, poster URL, series / season / episode, sizes, dates)
// but never a credential or an authenticated URL: the URL is rebuilt from the profile when a transfer runs.
// All byte counts are int64_t (files are often larger than 4 GB).

#ifndef PS4IPTV_DOWNLOADS_DOWNLOAD_MODEL_H
#define PS4IPTV_DOWNLOADS_DOWNLOAD_MODEL_H

#include <cstdint>
#include <string>
#include <vector>

namespace dl {

    enum class Kind {
        Movie,
        Episode
    };

    enum class State {
        Queued,
        Downloading,
        Paused,
        Completed,
        Failed,
        WaitingForNetwork,
        Cancelled
    };

    // why a download is Failed / Paused / Waiting (shown through a localization key)
    enum class Problem {
        None,
        Network,            // DNS / connect / timeout / connection lost
        HttpAuth,           // 401
        HttpForbidden,      // 403 (often temporary with IPTV providers)
        HttpNotFound,       // 404 / 410
        HttpServer,         // 5xx and other statuses
        NoSpace,            // not enough free space (before or during the transfer)
        Storage,            // the file could not be written
        FileTooLarge,       // the file system refused a file this large (EFBIG)
        SizeMismatch,       // the finished file does not have the announced size
        RestartNeeded,      // the server cannot resume: restarting from 0 needs the user's confirmation
        ProfileMissing,     // the profile of a queued download was deleted / changed
        FileMissing         // a completed file is no longer on the disk
    };

    struct Item {
        std::string key;              // "<profileId>|movie|<id>" / "<profileId>|episode|<id>"
        Kind kind = Kind::Movie;
        std::string profileId;
        std::string profileName;      // shown when downloads of several profiles exist
        std::string profileTag;       // fingerprint of server + username (detects a reused profile id)
        std::string contentId;        // stream id / episode id
        std::string seriesId;
        std::string seriesName;
        int season = 0;
        int episode = 0;
        std::string title;            // movie title / episode title (provider text, never translated)
        int year = 0;
        std::string extension;        // container extension (sanitized)
        std::string poster;           // poster / series cover URL (image cache key, not a credential)
        double durationHint = 0;      // seconds, from the provider metadata
        std::string fileName;         // "movie_123.mkv" (sanitized, unique in the manifest)
        State state = State::Queued;
        Problem problem = Problem::None;
        long httpStatus = 0;
        int64_t expectedBytes = -1;   // total size announced by the server (-1 unknown)
        int64_t downloadedBytes = 0;  // bytes in the .part file (the final size once completed)
        int64_t durableBytes = 0;     // bytes known to be flushed to the disk (crash recovery point)
        int64_t neededBytes = 0;      // NoSpace: bytes required (incl. the safety margin)
        int64_t availableBytes = -1;  // NoSpace: bytes that were free
        std::string etag;             // validators for If-Range
        std::string lastModified;
        int64_t createdAt = 0;        // unix time
        int64_t completedAt = 0;
        int64_t order = 0;            // queue order (lower first)
        int attempts = 0;             // consecutive failed attempts (retry backoff)
        double retryAt = 0;           // monotonic time of the next automatic attempt (not saved)
    };

    // ------------------------------------------------------------------ identity / file names
    std::string makeKey(const std::string &profileId, Kind kind, const std::string &contentId);

    // letters, digits, '-' and '_' only, at most 48 characters ("" stays "")
    std::string sanitizeComponent(const std::string &s);

    // lowercase letters/digits, 1..5 characters; anything else -> "mkv"
    std::string sanitizeExtension(const std::string &ext);

    // "movie_<id>.<ext>" / "episode_<id>.<ext>"; when taken by another download, "_<profile>" is added and then
    // a counter. Never contains a path separator or "..".
    std::string makeFileName(Kind kind, const std::string &contentId, const std::string &extension,
                             const std::string &profileId, const std::vector<std::string> &taken);

    const char *folderOf(Kind kind);   // "movies" / "episodes"

    // non-reversible fingerprint (FNV-1a 64, hex) of a profile's server and username
    std::string profileTag(const std::string &server, const std::string &username);

    // ------------------------------------------------------------------ manifest
    std::string serializeManifest(const std::vector<Item> &items);

    // Unknown fields are ignored, broken entries skipped (warning). Cancelled entries are dropped.
    bool parseManifest(const std::string &text, std::vector<Item> &out, std::string *error);

    const char *stateKey(State s);           // "queued", "downloading" ...

    State stateFromKey(const std::string &key);

    const char *problemKey(Problem p);       // "network", "no_space" ...

    Problem problemFromKey(const std::string &key);

    // what an interrupted download becomes when the app starts again
    State recoveredState(State saved, bool autoResume);

    // ------------------------------------------------------------------ HTTP
    struct ContentRange {
        bool valid = false;
        int64_t start = 0;
        int64_t end = 0;      // inclusive
        int64_t total = -1;   // -1 for "*"
    };

    // "bytes 100-999/1000", "bytes 100-999/*"
    ContentRange parseContentRange(const std::string &value);

    struct ResponseHead {
        long status = 0;
        int64_t contentLength = -1;
        ContentRange range;
        std::string etag;
        std::string lastModified;
        std::string acceptRanges;   // "bytes", "none", "" (absent); diagnostics only
    };

    // What to do with a response to a request for `offset` bytes onwards (offset 0 = no Range header).
    struct Plan {
        enum class Action {
            Write,          // write the body at writeOffset (truncating the file there)
            RestartFromZero,// the body cannot be used: truncate to 0 and request again without Range
            AskRestart,     // the server ignored Range and the partial is large: pause until the user confirms
            AlreadyComplete,// 416 for a file that is already complete
            Fail            // HTTP error (see Problem)
        };
        Action action = Action::Fail;
        int64_t writeOffset = 0;
        int64_t total = -1;           // expected final size (-1 unknown)
        Problem problem = Problem::None;
    };

    // A partial larger than this is never discarded without asking the user.
    const int64_t RESTART_CONFIRM_BYTES = 64ll * 1024 * 1024;

    // expectedTotal: the size known from an earlier response (-1 unknown)
    Plan planResponse(int64_t offset, int64_t expectedTotal, const ResponseHead &head);

    Problem problemForStatus(long status);

    // ------------------------------------------------------------------ retry policy
    struct RetryPolicy {
        int maxNetworkAttempts = 20;   // Waiting for network: about 17 minutes with the backoff below
        int maxServerAttempts = 6;     // HTTP 403 / 5xx
        double firstDelay = 5;
        double maxDelay = 60;
    };

    // seconds before attempt number `attempt` (1 = the first retry)
    double backoffDelay(const RetryPolicy &policy, int attempt);

    bool isRetryable(Problem p);

    // ------------------------------------------------------------------ space
    // free space that must remain after a download: 512 MB, or 2 % of the remaining bytes when larger
    int64_t safetyMargin(int64_t remainingBytes);

    // true when `available` (bytes free, -1 unknown) can take `remaining` more bytes plus the margin
    bool hasRoomFor(int64_t remaining, int64_t available);

    // ------------------------------------------------------------------ speed / ETA
    // Exponentially smoothed transfer speed from (time, total bytes) samples; samples closer than
    // minInterval seconds are merged so the ETA does not jump every callback.
    class SpeedMeter {
    public:
        void reset(double now, int64_t bytes);

        void sample(double now, int64_t bytes);

        double bytesPerSecond() const { return speed; }

        // seconds left for `remaining` bytes (-1 when unknown)
        double eta(int64_t remaining) const;

    private:
        double lastTime = 0;
        int64_t lastBytes = 0;
        double speed = 0;
        bool primed = false;
    };

    // ------------------------------------------------------------------ transfer diagnostics
    // Measurements of one transfer session (request -> stop), for Settings > Diagnostics > Downloads and the
    // log. Never contains a URL or credentials. Times are monotonic seconds (the manager's clock).
    struct TransferStats {
        bool active = false;
        std::string title;
        long httpStatus = 0;
        bool lengthKnown = false;
        int rangeSupport = -1;          // -1 unknown, 0 no, 1 yes (206 for a resume, or Accept-Ranges)
        int64_t startOffset = 0;        // bytes already on the disk when the request was sent
        int64_t bytes = 0;              // received and written in this session
        int64_t total = -1;             // the whole file (-1 unknown)
        double started = 0;             // when the request was sent
        double elapsed = 0;             // since then
        double firstByte = -1;          // seconds until the first body byte (-1 none yet)
        int64_t callbacks = 0;          // body callbacks of the transport (one write each)
        int64_t minCallback = 0;
        int64_t maxCallback = 0;
        double writeSeconds = 0;        // spent inside write() of the .part file
        double syncSeconds = 0;         // spent flushing it to the disk
        int syncs = 0;
        int manifestWrites = 0;         // downloads.json writes during the session
        double smoothed = 0;            // the speed the Downloads screen shows
        double peak = 0;                // best 1-second window
        int receiveBufferDefault = -1;  // the socket's SO_RCVBUF before the app's request (bytes, -1 unknown)
        int receiveBuffer = -1;         // after it
        long transferBuffer = 0;        // libcurl CURLOPT_BUFFERSIZE

        // bytes / elapsed: what the user gets
        double average() const;

        // bytes / time the transfer spent waiting for and receiving data (elapsed minus the wait for the
        // first byte, minus disk writes and flushes): the network side
        double networkSpeed() const;

        // bytes / time in write(): the disk side (0 unknown)
        double diskSpeed() const;

        double averageCallback() const;

        double callbacksPerSecond() const;
    };

    // Collects TransferStats from the transfer thread (cheap per callback; 1-second windows for the peak)
    class StatsMeter {
    public:
        void begin(double now, int64_t offset, long transferBuffer);

        void head(double now, long status, int64_t total, int rangeSupport);

        // one body callback of n bytes that took writeSeconds to write
        void data(double now, size_t n, double writeSeconds);

        void sync(double seconds);

        void finish(double now);

        TransferStats &stats() { return s; }

        const TransferStats &stats() const { return s; }

    private:
        TransferStats s;
        double windowStart = 0;
        int64_t windowBytes = 0;
    };

    // ------------------------------------------------------------------ display helpers
    // "512 KB", "3.4 MB", "7.82 GB" (1024-based units, as consoles show storage)
    std::string formatBytes(int64_t bytes);

    // whole percent 0..100 (-1 when the total is unknown)
    int percent(int64_t done, int64_t total);
}

#endif // PS4IPTV_DOWNLOADS_DOWNLOAD_MODEL_H
