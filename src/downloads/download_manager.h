// Download queue and engine (host-testable: the transport, clocks and free-space query are injected).
//
// - one media transfer at a time, on one dedicated worker thread (startWorker), in queue order; the UI
//   thread only changes states and reads snapshots
// - a transfer writes temp/<file>.part; resume requests "Range: bytes=<part size>-" (+ If-Range) and the
//   response is checked by dl::planResponse: 206 at the offset is appended, a 200 is never appended (small
//   partials restart from 0, large ones wait for the user's confirmation), 416 for a complete file finishes it
// - the .part file is fsynced every SYNC_EVERY bytes and that size is recorded (durableBytes): after a crash
//   the partial is cut back to it, so a resumed file never contains bytes that were not on the disk
// - complete: the size is checked against Content-Length / Content-Range, then the file is renamed into
//   movies/ or episodes/; only then is it Completed
// - network errors -> Waiting for network with bounded exponential backoff (when Retry downloads is on);
//   HTTP 403 / 5xx are retried a few times; 401 / 404 fail at once
// - free space is checked before a transfer (when the size is known), when the size becomes known, and every
//   SPACE_CHECK_EVERY bytes while writing; the safety margin is never used up
// - playback started: the active transfer is stopped and resumes (from the same byte) when playback ends
// - the manifest is written atomically after every state change; credentials only live in memory
//   (setProfiles) and the URL is built when a transfer starts

#ifndef PS4IPTV_DOWNLOADS_DOWNLOAD_MANAGER_H
#define PS4IPTV_DOWNLOADS_DOWNLOAD_MANAGER_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "download_model.h"
#include "transport.h"

namespace dl {

    // what the engine needs from a profile (in memory only, never written by the download manager)
    struct Credentials {
        std::string profileId;
        std::string name;
        std::string server;
        std::string username;
        std::string password;
    };

    struct ManagerConfig {
        std::string root;                                   // the downloads directory ("<data>/downloads/")
        Transport *transport = nullptr;
        std::function<double()> clock;                      // monotonic seconds
        std::function<int64_t()> wallClock;                 // unix time (createdAt / completedAt)
        std::function<int64_t(const std::string &)> freeSpace;   // bytes free at a path, -1 unknown
        // authenticated URL of a movie / episode (built only when its transfer starts)
        std::function<std::string(const Credentials &, Kind, const std::string &id, const std::string &ext)> buildUrl;
        RetryPolicy retry;
        int64_t syncEvery = 32ll * 1024 * 1024;
        int64_t spaceCheckEvery = 64ll * 1024 * 1024;
        bool probeLargeFiles = false;                       // PS4: check the >4 GiB support once
    };

    struct Totals {
        int completed = 0;
        int active = 0;           // queued, downloading, waiting, paused, failed
        int64_t completedBytes = 0;
        int64_t partialBytes = 0;
    };

    // live numbers of the item being transferred
    struct LiveProgress {
        std::string key;
        int64_t bytes = 0;
        int64_t total = -1;
        double speed = 0;         // bytes/s (smoothed)
        double eta = -1;          // seconds (-1 unknown)
    };

    class DownloadManager {
    public:
        explicit DownloadManager(ManagerConfig config);

        ~DownloadManager();

        // Reads the manifest, recovers interrupted transfers (autoResume: they are queued again, else
        // paused), cuts partials back to their durable size, removes orphan .part files.
        bool load(bool autoResume, std::string *warning);

        void startWorker();

        void stopWorker();

        // one scheduling iteration on the calling thread (the worker loop; unit tests call it directly).
        // Returns true when it started a transfer or changed an item.
        bool step();

        // ------------------------------------------------------------------ settings / environment
        void setAutoRetry(bool enabled);

        void setProfiles(const std::vector<Credentials> &profiles);

        // video playback started / ended: the active transfer pauses meanwhile (stability first)
        void setPlaybackActive(bool active);

        bool playbackActive() const;

        // ------------------------------------------------------------------ queries (any thread)
        std::vector<Item> items() const;

        bool get(const std::string &key, Item &out) const;

        // the completed download of a movie / episode, if any
        bool findCompleted(const std::string &profileId, Kind kind, const std::string &contentId, Item &out) const;

        // a download in any state
        bool find(const std::string &profileId, Kind kind, const std::string &contentId, Item &out) const;

        bool hasCompletedEpisodes(const std::string &profileId, const std::string &seriesId) const;

        LiveProgress live() const;

        Totals totals() const;

        int64_t freeBytes() const;

        // changes whenever an item or the live progress changes
        unsigned generation() const { return gen.load(); }

        std::string completedPath(const Item &item) const;

        std::string partPath(const Item &item) const;

        const std::string &root() const { return cfg.root; }

        // ------------------------------------------------------------------ actions (UI thread)
        enum class Enqueue {
            Added,
            Exists,         // already queued / downloading / paused (unchanged)
            Resumed,        // was failed or paused: queued again
            Completed,      // already downloaded
            NoProfile,
            NoSpace
        };

        // item: kind, profileId, contentId, title, extension, poster, series fields. The rest is filled in.
        Enqueue enqueue(Item item);

        void pause(const std::string &key);

        // Paused / Failed / Waiting -> Queued (attempts reset)
        void resume(const std::string &key);

        // the server cannot resume: discard the partial and download again from 0
        void confirmRestart(const std::string &key);

        // stops the transfer, deletes the partial file and forgets the item (never touches progress /
        // favorites / history)
        void cancel(const std::string &key);

        // deletes a download in any state: media file, partial file and manifest entry
        bool remove(const std::string &key, std::string *error = nullptr);

        int removeAllCompleted();

        int removePartials();

        // app exit: the active transfer stops where it is (saved as interrupted, resumed next start)
        void shutdown();

    private:
        enum class StopReason {
            None,
            Pause,
            Cancel,
            Suspend,
            Shutdown
        };

        struct Active;

        Item *findLocked(const std::string &key);

        const Item *findLocked(const std::string &key) const;

        void persist();

        void touch();

        void runTransfer(Item work, const Credentials &creds);

        bool pickNext(Item &work, Credentials &creds);

        void deleteFiles(const Item &item, bool media);

        static void *threadMain(void *self);

        void workerLoop();

        ManagerConfig cfg;
        mutable std::mutex m;
        std::mutex saveMutex;
        std::condition_variable cv;
        std::vector<Item> list;
        std::vector<Credentials> creds;
        bool autoRetry = true;
        bool suspended = false;
        bool stopping = false;
        int64_t nextOrder = 1;
        int largeFiles = 0;                 // 0 unknown, 1 supported, -1 not supported
        // the transfer in progress
        std::string activeKey;
        StopReason stopReason = StopReason::None;
        std::shared_ptr<std::atomic<bool>> activeCancel;
        LiveProgress liveProgress;
        std::atomic<unsigned> gen{1};
        void *thread = nullptr;
    };
}

#endif // PS4IPTV_DOWNLOADS_DOWNLOAD_MANAGER_H
