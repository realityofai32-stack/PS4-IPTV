#include <algorithm>
#include <cerrno>
#include <cstdio>

#ifdef __PS4__
#include <pthread.h>
#else
#include <thread>
#endif

#include "download_manager.h"
#include "part_file.h"
#include "../platform/fs.h"
#include "../platform/log.h"

namespace dl {

    namespace {
        const size_t WORKER_STACK_BYTES = 1024 * 1024;   // libcurl + mbedTLS handshakes
        const int64_t FOUR_GIB = 4ll << 30;
        const double UI_UPDATE_EVERY = 0.25;             // live progress at most 4 times per second
        const int MAX_RESTARTS = 2;                      // RestartFromZero per attempt

        std::string dirOf(const std::string &root, const char *sub) {
            return fs::join(root, sub);
        }

        // what a finished attempt leads to
        enum class End {
            Completed,
            Retry,        // problem is retryable
            Fail,
            AskRestart,
            Stopped       // pause / cancel / suspend / shutdown
        };
    }

    DownloadManager::DownloadManager(ManagerConfig c) : cfg(std::move(c)) {
        if (!cfg.clock) {
            cfg.clock = [] { return 0.0; };
        }
        if (!cfg.wallClock) {
            cfg.wallClock = [] { return (int64_t) 0; };
        }
        if (!cfg.freeSpace) {
            cfg.freeSpace = [](const std::string &p) { return dl::freeSpace(p); };
        }
    }

    DownloadManager::~DownloadManager() {
        shutdown();
        stopWorker();
    }

    // ------------------------------------------------------------------ paths / persistence

    std::string DownloadManager::completedPath(const Item &it) const {
        return fs::join(dirOf(cfg.root, folderOf(it.kind)), it.fileName);
    }

    std::string DownloadManager::partPath(const Item &it) const {
        return fs::join(dirOf(cfg.root, "temp"), it.fileName + ".part");
    }

    void DownloadManager::touch() {
        gen++;
    }

    void DownloadManager::persist() {
        std::lock_guard<std::mutex> save(saveMutex);   // the snapshot taken last is written last
        std::string text;
        {
            std::lock_guard<std::mutex> lock(m);
            text = serializeManifest(list);
        }
        std::string err;
        if (!fs::writeFileAtomic(fs::join(dirOf(cfg.root, "metadata"), "downloads.json"), text, &err)) {
            LOG_E("downloads", "manifest not saved: %s", err.c_str());
        } else {
            manifestWrites++;
        }
    }

    Item *DownloadManager::findLocked(const std::string &key) {
        for (auto &it: list) {
            if (it.key == key) {
                return &it;
            }
        }
        return nullptr;
    }

    const Item *DownloadManager::findLocked(const std::string &key) const {
        for (const auto &it: list) {
            if (it.key == key) {
                return &it;
            }
        }
        return nullptr;
    }

    bool DownloadManager::load(bool autoResume, std::string *warning) {
        std::string err;
        for (const char *sub: {"movies", "episodes", "temp", "metadata"}) {
            if (!fs::ensureDir(dirOf(cfg.root, sub), &err)) {
                LOG_E("downloads", "%s", err.c_str());
                if (warning) {
                    *warning = err;
                }
                return false;
            }
        }
        std::string path = fs::join(dirOf(cfg.root, "metadata"), "downloads.json");
        std::vector<Item> loaded;
        std::string text;
        bool ok = false;
        for (const std::string &p: {path, path + ".bak"}) {
            if (!fs::exists(p)) {
                continue;
            }
            std::string note;
            if (fs::readFile(p, text, 8u * 1024 * 1024, &err) && parseManifest(text, loaded, &note)) {
                if (!note.empty()) {
                    LOG_W("downloads", "%s", note.c_str());
                }
                ok = true;
                if (p != path) {
                    LOG_W("downloads", "downloads.json unreadable, restored the previous copy");
                }
                break;
            }
            LOG_E("downloads", "%s: %s", p.c_str(), err.c_str());
        }
        if (!ok && fs::exists(path) && warning) {
            *warning = "the download list could not be read";
        }

        std::vector<std::string> parts;
        for (Item &it: loaded) {
            if (it.state == State::Completed) {
                int64_t size = fs::fileSize(completedPath(it));
                if (size < 0) {
                    it.state = State::Failed;
                    it.problem = Problem::FileMissing;
                    it.downloadedBytes = 0;
                    LOG_W("downloads", "%s: completed file missing", it.fileName.c_str());
                } else {
                    it.downloadedBytes = size;
                }
                continue;
            }
            std::string part = partPath(it);
            parts.push_back(it.fileName + ".part");
            int64_t size = fs::fileSize(part);
            if (size < 0) {
                size = 0;
            }
            // only bytes that were flushed to the disk are trusted after an interruption
            int64_t keep = std::min(size, it.durableBytes);
            if (size > keep) {
                PartFile f;
                if (f.open(part, &err) && f.truncateTo(keep, &err)) {
                    LOG_I("downloads", "%s: partial cut back from %lld to %lld bytes (last flush)",
                          it.fileName.c_str(), (long long) size, (long long) keep);
                } else {
                    LOG_W("downloads", "%s: %s", it.fileName.c_str(), err.c_str());
                    keep = std::max<int64_t>(0, fs::fileSize(part));
                }
            }
            it.downloadedBytes = keep;
            it.durableBytes = keep;
            State before = it.state;
            it.state = recoveredState(it.state, autoResume);
            if (before == State::Downloading || before == State::WaitingForNetwork) {
                LOG_I("downloads", "%s: interrupted at %lld bytes -> %s", it.fileName.c_str(), (long long) keep,
                      stateKey(it.state));
            }
            it.attempts = 0;
            it.retryAt = 0;
        }
        // partial files no manifest entry refers to cannot be resumed (and are never guessed from names)
        for (const auto &e: fs::listDir(dirOf(cfg.root, "temp"))) {
            if (!e.dir && e.name.size() > 5 && e.name.compare(e.name.size() - 5, 5, ".part") == 0
                && std::find(parts.begin(), parts.end(), e.name) == parts.end()) {
                fs::removeFile(fs::join(dirOf(cfg.root, "temp"), e.name));
                LOG_I("downloads", "orphan partial file removed (%lld bytes)", (long long) e.size);
            }
        }
        {
            std::lock_guard<std::mutex> lock(m);
            list = std::move(loaded);
            for (const auto &it: list) {
                nextOrder = std::max(nextOrder, it.order + 1);
            }
        }
        LOG_I("downloads", "%d download(s) in the manifest", (int) list.size());
        touch();
        persist();
        return true;
    }

    // ------------------------------------------------------------------ worker

    void *DownloadManager::threadMain(void *self) {
        ((DownloadManager *) self)->workerLoop();
        return nullptr;
    }

    void DownloadManager::startWorker() {
        if (thread != nullptr) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m);
            stopping = false;
        }
#ifdef __PS4__
        auto *t = new pthread_t;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, WORKER_STACK_BYTES);
        int rc = pthread_create(t, &attr, &DownloadManager::threadMain, this);
        pthread_attr_destroy(&attr);
        if (rc != 0) {
            LOG_E("downloads", "pthread_create failed: %d", rc);
            delete t;
            return;
        }
        thread = t;
#else
        thread = new std::thread(&DownloadManager::threadMain, this);
#endif
        LOG_I("downloads", "download worker started");
    }

    void DownloadManager::stopWorker() {
        {
            std::lock_guard<std::mutex> lock(m);
            stopping = true;
            if (activeCancel) {
                if (stopReason == StopReason::None) {
                    stopReason = StopReason::Shutdown;
                }
                activeCancel->store(true);
            }
        }
        cv.notify_all();
        if (thread == nullptr) {
            return;
        }
#ifdef __PS4__
        pthread_join(*(pthread_t *) thread, nullptr);
        delete (pthread_t *) thread;
#else
        ((std::thread *) thread)->join();
        delete (std::thread *) thread;
#endif
        thread = nullptr;
    }

    void DownloadManager::shutdown() {
        std::lock_guard<std::mutex> lock(m);
        if (activeCancel) {
            stopReason = StopReason::Shutdown;
            activeCancel->store(true);
        }
        stopping = true;
        cv.notify_all();
    }

    void DownloadManager::workerLoop() {
        while (true) {
            {
                std::lock_guard<std::mutex> lock(m);
                if (stopping) {
                    return;
                }
            }
            if (step()) {
                continue;
            }
            std::unique_lock<std::mutex> lock(m);
            if (stopping) {
                return;
            }
            // wake up for user actions (notify) or at least every second for retry timers
            cv.wait_for(lock, std::chrono::milliseconds(1000));
        }
    }

    bool DownloadManager::pickNext(Item &work, Credentials &c) {
        if (suspended || stopping) {
            return false;
        }
        double now = cfg.clock();
        Item *best = nullptr;
        for (auto &it: list) {
            bool runnable = (it.state == State::Queued && it.retryAt <= now)
                            || (it.state == State::WaitingForNetwork && autoRetry && it.retryAt <= now);
            if (runnable && (best == nullptr || it.order < best->order)) {
                best = &it;
            }
        }
        if (best == nullptr) {
            return false;
        }
        const Credentials *found = nullptr;
        for (const auto &cr: creds) {
            if (cr.profileId == best->profileId) {
                found = &cr;
            }
        }
        if (found == nullptr || (!best->profileTag.empty() && profileTag(found->server, found->username) != best->profileTag)) {
            best->state = State::Failed;
            best->problem = Problem::ProfileMissing;
            work = *best;
            work.state = State::Failed;
            return true;
        }
        best->state = State::Downloading;
        best->problem = Problem::None;
        activeKey = best->key;
        stopReason = StopReason::None;
        activeCancel = std::make_shared<std::atomic<bool>>(false);
        liveProgress = LiveProgress();
        liveProgress.key = best->key;
        liveProgress.bytes = best->downloadedBytes;
        liveProgress.total = best->expectedBytes;
        work = *best;
        c = *found;
        return true;
    }

    bool DownloadManager::step() {
        Item work;
        Credentials c;
        {
            std::lock_guard<std::mutex> lock(m);
            if (!pickNext(work, c)) {
                return false;
            }
        }
        touch();
        persist();
        if (work.state == State::Failed) {
            LOG_W("downloads", "%s: the profile of this download no longer exists", work.fileName.c_str());
            return true;
        }
        runTransfer(work, c);
        return true;
    }

    // ------------------------------------------------------------------ one transfer

    namespace {
        class FileSink : public TransferSink {
        public:
            PartFile &file;
            int64_t requestOffset;
            int64_t expectedTotal;
            std::function<int64_t()> free;
            std::function<double()> clock;
            const ManagerConfig &cfg;
            int largeFiles;
            std::shared_ptr<std::atomic<bool>> cancel;
            // callbacks into the manager (under its lock)
            std::function<void(const ResponseHead &, int64_t total, int64_t written)> headAccepted;
            std::function<void(int64_t written, int64_t total, double speed, double eta)> progress;
            std::function<void(int64_t durable)> durable;
            std::function<void(const TransferStats &)> publish;   // diagnostics, at the UI rate and at the end

            Plan plan;
            bool headSeen = false;
            Problem problem = Problem::None;
            long httpStatus = 0;
            int64_t written = 0;
            int64_t total = -1;
            int64_t neededBytes = 0;
            int64_t availableBytes = -1;
            int64_t lastSync = 0;
            double lastSyncTime = 0;
            int64_t lastSpaceCheck = 0;
            double lastUi = 0;
            SpeedMeter meter;
            StatsMeter diag;

            FileSink(PartFile &f, int64_t offset, int64_t expected, const ManagerConfig &c, int large)
                    : file(f), requestOffset(offset), expectedTotal(expected), cfg(c), largeFiles(large) {}

            bool onHead(const ResponseHead &h) override {
                headSeen = true;
                httpStatus = h.status;
                plan = planResponse(requestOffset, expectedTotal, h);
                // Range support: a resume answered with 206, or (fresh download) the Accept-Ranges header
                int rangeSupport = requestOffset > 0 ? (h.status == 206 ? 1 : 0)
                                   : h.acceptRanges.empty() ? -1 : h.acceptRanges == "none" ? 0 : 1;
                diag.head(cfg.clock(), h.status, plan.total, rangeSupport);
                if (plan.action != Plan::Action::Write) {
                    problem = plan.problem;
                    return false;
                }
                std::string err;
                if (!file.truncateTo(plan.writeOffset, &err)) {
                    LOG_E("downloads", "partial file: %s", err.c_str());
                    problem = Problem::Storage;
                    return false;
                }
                written = plan.writeOffset;
                lastSync = written;
                lastSyncTime = cfg.clock();
                lastSpaceCheck = written;
                total = plan.total;
                if (total > 0 && total > FOUR_GIB - 1 && largeFiles < 0) {
                    problem = Problem::FileTooLarge;
                    return false;
                }
                if (total > 0) {
                    int64_t avail = cfg.freeSpace(cfg.root);
                    if (!hasRoomFor(total - written, avail)) {
                        problem = Problem::NoSpace;
                        neededBytes = (total - written) + safetyMargin(total - written);
                        availableBytes = avail;
                        return false;
                    }
                }
                meter.reset(cfg.clock(), written);
                lastUi = cfg.clock();
                headAccepted(h, total, written);
                return true;
            }

            bool onData(const char *data, size_t n) override {
                if (cancel && cancel->load()) {
                    return false;
                }
                if (total >= 0 && written + (int64_t) n > total) {
                    problem = Problem::SizeMismatch;   // more than announced
                    return false;
                }
                int e = 0;
                double before = cfg.clock();
                if (!file.write(data, n, &e)) {
                    problem = e == ENOSPC ? Problem::NoSpace : e == EFBIG ? Problem::FileTooLarge : Problem::Storage;
                    LOG_E("downloads", "write failed at %lld bytes: errno %d", (long long) written, e);
                    return false;
                }
                double now = cfg.clock();
                diag.data(now, n, now - before);
                written += (int64_t) n;
                bool syncDue = written - lastSync >= cfg.syncEvery
                               || (written > lastSync && now - lastSyncTime >= cfg.syncInterval);
                if (syncDue) {
                    bool ok = file.sync();
                    double after = cfg.clock();
                    diag.sync(after - now);
                    lastSyncTime = after;
                    if (ok) {
                        lastSync = written;
                        durable(written);
                    }
                    now = after;
                }
                if (written - lastSpaceCheck >= cfg.spaceCheckEvery) {
                    lastSpaceCheck = written;
                    int64_t avail = cfg.freeSpace(cfg.root);
                    int64_t remaining = total >= 0 ? total - written : 0;
                    // something else used the space meanwhile, or (unknown total) the margin is reached
                    if (!hasRoomFor(remaining, avail)) {
                        problem = Problem::NoSpace;
                        neededBytes = remaining + safetyMargin(remaining);
                        availableBytes = avail;
                        return false;
                    }
                }
                meter.sample(now, written);
                if (now - lastUi >= UI_UPDATE_EVERY) {
                    lastUi = now;
                    progress(written, total, meter.bytesPerSecond(), meter.eta(total >= 0 ? total - written : -1));
                    diag.stats().smoothed = meter.bytesPerSecond();
                    if (publish) {
                        publish(diag.stats());
                    }
                }
                return true;
            }

            void onSocket(int defaultReceiveBuffer, int receiveBuffer) override {
                diag.stats().receiveBufferDefault = defaultReceiveBuffer;
                diag.stats().receiveBuffer = receiveBuffer;
                LOG_I("downloads", "socket receive buffer: system default %d KB, in use %d KB",
                      defaultReceiveBuffer < 0 ? -1 : defaultReceiveBuffer / 1024,
                      receiveBuffer < 0 ? -1 : receiveBuffer / 1024);
            }
        };

        // the session's numbers in one log line (no URL, no credentials)
        void logStats(const std::string &file, const TransferStats &s) {
            LOG_I("downloads", "%s: session %.1f MB in %.1f s = %.2f MB/s (network %.2f MB/s, disk %.1f MB/s, peak "
                               "%.2f MB/s); first byte %.2f s; %lld callbacks avg %.1f KB (min %lld, max %lld B), "
                               "%.0f/s; write %.2f s, %d flushes %.2f s, %d manifest writes; HTTP %ld, length %s, "
                               "range %s; receive buffer %d -> %d KB, curl buffer %ld KB",
                  file.c_str(), s.bytes / 1e6, s.elapsed, s.average() / 1e6, s.networkSpeed() / 1e6,
                  s.diskSpeed() / 1e6, s.peak / 1e6, s.firstByte, (long long) s.callbacks, s.averageCallback() / 1024,
                  (long long) s.minCallback, (long long) s.maxCallback, s.callbacksPerSecond(), s.writeSeconds, s.syncs,
                  s.syncSeconds, s.manifestWrites, s.httpStatus, s.lengthKnown ? "known" : "unknown",
                  s.rangeSupport > 0 ? "yes" : s.rangeSupport == 0 ? "no" : "unknown",
                  s.receiveBufferDefault < 0 ? -1 : s.receiveBufferDefault / 1024,
                  s.receiveBuffer < 0 ? -1 : s.receiveBuffer / 1024, s.transferBuffer / 1024);
        }
    }

    void DownloadManager::runTransfer(Item work, const Credentials &c) {
        std::string err;
        std::string part = partPath(work);
        PartFile file;
        std::shared_ptr<std::atomic<bool>> cancelFlag;
        {
            std::lock_guard<std::mutex> lock(m);
            cancelFlag = activeCancel;
        }

        End end = End::Fail;
        Problem problem = Problem::None;
        long httpStatus = 0;
        int64_t written = 0;
        int64_t startOffset = 0;
        int64_t total = work.expectedBytes;
        int64_t needed = 0;
        int64_t available = -1;
        std::string etag = work.etag;
        std::string lastModified = work.lastModified;

        auto finishActive = [&]() {
            std::lock_guard<std::mutex> lock(m);
            activeKey.clear();
            activeCancel.reset();
            liveProgress = LiveProgress();
        };

        if (!file.open(part, &err)) {
            LOG_E("downloads", "%s: %s", work.fileName.c_str(), err.c_str());
            problem = Problem::Storage;
        } else {
            int64_t offset = file.size();
            if (offset < 0) {
                offset = 0;
            }
            if (work.expectedBytes > 0 && offset > work.expectedBytes) {
                LOG_W("downloads", "%s: partial larger than the file, starting over", work.fileName.c_str());
                file.truncateTo(0, &err);
                offset = 0;
                etag.clear();
                lastModified.clear();
            }
            startOffset = offset;
            written = offset;
            if (cfg.probeLargeFiles && largeFiles == 0) {
                std::string detail;
                largeFiles = supportsLargeFiles(dirOf(cfg.root, "temp"), &detail) ? 1 : -1;
                LOG_I("downloads", "large file check: %s", detail.c_str());
            }
            int64_t avail = cfg.freeSpace(cfg.root);
            if (work.expectedBytes > 0 && offset == work.expectedBytes) {
                end = End::Completed;   // everything is here already (interrupted right before the rename)
            } else if (work.expectedBytes > 0 && !hasRoomFor(work.expectedBytes - offset, avail)) {
                problem = Problem::NoSpace;
                needed = (work.expectedBytes - offset) + safetyMargin(work.expectedBytes - offset);
                available = avail;
            } else if (work.expectedBytes <= 0 && avail >= 0 && avail < safetyMargin(0)) {
                problem = Problem::NoSpace;
                needed = safetyMargin(0);
                available = avail;
            } else if (work.expectedBytes > FOUR_GIB - 1 && largeFiles < 0) {
                problem = Problem::FileTooLarge;
            } else {
                std::string url = cfg.buildUrl ? cfg.buildUrl(c, work.kind, work.contentId, work.extension) : "";
                for (int restarts = 0;; restarts++) {
                    TransferRequest req;
                    req.url = url;
                    req.offset = offset;
                    req.ifRange = offset > 0 ? (!etag.empty() ? etag : lastModified) : "";
                    req.cancel = cancelFlag;
                    req.receiveBuffer = cfg.receiveBuffer;
                    req.transferBuffer = cfg.transferBuffer;
                    FileSink sink(file, offset, offset > 0 ? total : -1, cfg, largeFiles);
                    sink.cancel = cancelFlag;
                    sink.diag.stats().title = work.title;
                    sink.diag.begin(cfg.clock(), offset, req.transferBuffer);
                    int manifestBase = manifestWrites.load();
                    std::string key = work.key;
                    sink.publish = [this, manifestBase](const TransferStats &st) {
                        std::lock_guard<std::mutex> lock(m);
                        lastStats = st;
                        lastStats.manifestWrites = manifestWrites.load() - manifestBase;
                    };
                    sink.publish(sink.diag.stats());
                    sink.headAccepted = [this, key, &etag, &lastModified](const ResponseHead &h, int64_t t, int64_t w) {
                        std::lock_guard<std::mutex> lock(m);
                        etag = h.etag;
                        lastModified = h.lastModified;
                        if (Item *it = findLocked(key)) {
                            it->expectedBytes = t;
                            it->downloadedBytes = w;
                            it->etag = h.etag;
                            it->lastModified = h.lastModified;
                        }
                        liveProgress.bytes = w;
                        liveProgress.total = t;
                        gen++;
                    };
                    sink.progress = [this, key](int64_t w, int64_t t, double speed, double eta) {
                        std::lock_guard<std::mutex> lock(m);
                        if (Item *it = findLocked(key)) {
                            it->downloadedBytes = w;
                        }
                        liveProgress.bytes = w;
                        liveProgress.total = t;
                        liveProgress.speed = speed;
                        liveProgress.eta = eta;
                        gen++;
                    };
                    sink.durable = [this, key](int64_t d) {
                        {
                            std::lock_guard<std::mutex> lock(m);
                            if (Item *it = findLocked(key)) {
                                it->durableBytes = d;
                                it->downloadedBytes = d;
                            }
                        }
                        persist();
                    };
                    LOG_I("downloads", "%s: request from byte %lld%s", work.fileName.c_str(), (long long) offset,
                          req.ifRange.empty() ? "" : " (If-Range)");
                    TransferResult r = cfg.transport ? cfg.transport->run(req, sink) : TransferResult();
                    if (sink.headSeen && sink.plan.action == Plan::Action::Write) {
                        written = sink.written;
                        total = sink.total;
                    }
                    httpStatus = r.status ? r.status : sink.httpStatus;
                    double syncStart = cfg.clock();
                    file.sync();   // pause / cancel / failure / completion / app exit: the written bytes are durable
                    sink.diag.sync(cfg.clock() - syncStart);
                    sink.diag.stats().smoothed = sink.meter.bytesPerSecond();
                    sink.diag.finish(cfg.clock());
                    sink.publish(sink.diag.stats());
                    if (sink.diag.stats().callbacks > 0) {
                        logStats(work.fileName, transferStats());
                    }
                    if (sink.headSeen && sink.plan.action == Plan::Action::RestartFromZero) {
                        LOG_W("downloads", "%s: the server's answer cannot be used to resume (HTTP %ld), restarting "
                                           "from 0", work.fileName.c_str(), httpStatus);
                        file.truncateTo(0, &err);
                        offset = 0;
                        written = 0;
                        etag.clear();
                        lastModified.clear();
                        total = -1;
                        if (restarts < MAX_RESTARTS) {
                            continue;
                        }
                        problem = Problem::HttpServer;
                        end = End::Retry;
                        break;
                    }
                    if (sink.headSeen && sink.plan.action == Plan::Action::AskRestart) {
                        end = End::AskRestart;
                        problem = Problem::RestartNeeded;
                        break;
                    }
                    if (sink.headSeen && sink.plan.action == Plan::Action::AlreadyComplete) {
                        total = sink.plan.total;
                        written = offset;
                        end = End::Completed;
                        break;
                    }
                    bool stopped;
                    {
                        std::lock_guard<std::mutex> lock(m);
                        stopped = stopReason != StopReason::None;
                    }
                    if (stopped) {
                        end = End::Stopped;
                        break;
                    }
                    if (sink.problem != Problem::None) {
                        problem = sink.problem;
                        needed = sink.neededBytes;
                        available = sink.availableBytes;
                        end = isRetryable(problem) ? End::Retry : End::Fail;
                        break;
                    }
                    switch (r.outcome) {
                        case TransferResult::Outcome::Complete:
                            if (total >= 0 && written < total) {
                                problem = Problem::Network;   // the connection ended early: resume later
                                end = End::Retry;
                            } else if (total >= 0 && written > total) {
                                problem = Problem::SizeMismatch;
                                end = End::Fail;
                            } else {
                                end = End::Completed;
                            }
                            break;
                        case TransferResult::Outcome::Network:
                            problem = Problem::Network;
                            end = End::Retry;
                            LOG_W("downloads", "%s: network error at %lld bytes: %s", work.fileName.c_str(),
                                  (long long) written, r.detail.c_str());
                            break;
                        case TransferResult::Outcome::HttpError:
                            problem = problemForStatus(httpStatus);
                            end = isRetryable(problem) ? End::Retry : End::Fail;
                            break;
                        default:
                            problem = Problem::HttpServer;
                            end = End::Retry;
                            LOG_W("downloads", "%s: transfer error: %s", work.fileName.c_str(), r.detail.c_str());
                            break;
                    }
                    break;
                }
            }
        }

        // ---- finalize the file outside the lock
        std::string finalPath = completedPath(work);
        if (end == End::Completed) {
            file.sync();
            file.close();
            int64_t size = fs::fileSize(part);
            if (total >= 0 && size != total) {
                problem = Problem::SizeMismatch;
                end = End::Fail;
                LOG_E("downloads", "%s: size %lld, expected %lld", work.fileName.c_str(), (long long) size,
                      (long long) total);
            } else {
                fs::ensureDir(dirOf(cfg.root, folderOf(work.kind)));
                fs::removeFile(finalPath);   // a stale file of an earlier download with this name
                if (!moveFile(part, finalPath)) {
                    problem = Problem::Storage;
                    end = End::Fail;
                    LOG_E("downloads", "%s: rename into place failed: errno %d", work.fileName.c_str(), errno);
                } else {
                    written = fs::fileSize(finalPath);
                }
            }
        }
        file.close();

        StopReason reason;
        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(m);
            reason = stopReason;
            Item *it = findLocked(work.key);
            if (it == nullptr) {
                removed = true;   // deleted meanwhile
            } else {
                it->etag = etag;
                it->lastModified = lastModified;
                if (end != End::Completed) {
                    it->downloadedBytes = written;
                    it->durableBytes = written;
                }
                if (written > startOffset) {
                    it->attempts = 0;   // progress was made: the backoff starts over
                }
                switch (end) {
                    case End::Completed:
                        it->state = State::Completed;
                        it->problem = Problem::None;
                        it->downloadedBytes = written;
                        it->durableBytes = written;
                        it->expectedBytes = written;
                        it->completedAt = cfg.wallClock();
                        it->attempts = 0;
                        LOG_I("downloads", "%s: completed (%lld bytes)", it->fileName.c_str(), (long long) written);
                        break;
                    case End::AskRestart:
                        it->state = State::Paused;
                        it->problem = Problem::RestartNeeded;
                        LOG_W("downloads", "%s: the server ignored the Range request; %lld bytes kept until the user "
                                           "confirms a restart", it->fileName.c_str(), (long long) written);
                        break;
                    case End::Stopped:
                        if (reason == StopReason::Pause) {
                            it->state = State::Paused;
                        } else if (reason == StopReason::Suspend) {
                            it->state = State::Queued;   // resumes after playback
                        } else if (reason == StopReason::Shutdown) {
                            it->state = State::Downloading;   // interrupted: recovered at the next start
                        }
                        LOG_I("downloads", "%s: stopped at %lld bytes (%s)", it->fileName.c_str(), (long long) written,
                              reason == StopReason::Pause ? "paused" : reason == StopReason::Suspend ? "playback"
                                                                     : reason == StopReason::Cancel ? "cancelled"
                                                                                                    : "app exit");
                        break;
                    case End::Retry: {
                        it->problem = problem;
                        it->httpStatus = httpStatus;
                        it->attempts++;
                        int max = problem == Problem::Network ? cfg.retry.maxNetworkAttempts : cfg.retry.maxServerAttempts;
                        if (!autoRetry || it->attempts > max) {
                            it->state = State::Failed;
                            LOG_W("downloads", "%s: %s after %d attempt(s): failed", it->fileName.c_str(),
                                  problemKey(problem), it->attempts);
                        } else {
                            double delay = backoffDelay(cfg.retry, it->attempts);
                            it->retryAt = cfg.clock() + delay;
                            it->state = problem == Problem::Network ? State::WaitingForNetwork : State::Queued;
                            LOG_I("downloads", "%s: %s, retry %d/%d in %.0f s", it->fileName.c_str(), problemKey(problem),
                                  it->attempts, max, delay);
                        }
                        break;
                    }
                    default:
                        it->state = State::Failed;
                        it->problem = problem;
                        it->httpStatus = httpStatus;
                        it->neededBytes = needed;
                        it->availableBytes = available;
                        LOG_W("downloads", "%s: failed (%s, HTTP %ld)", it->fileName.c_str(), problemKey(problem),
                              httpStatus);
                        break;
                }
                if (end == End::Retry && problem == Problem::NoSpace) {
                    it->neededBytes = needed;
                    it->availableBytes = available;
                }
            }
            if (reason == StopReason::Cancel && it != nullptr) {
                list.erase(std::remove_if(list.begin(), list.end(), [&work](const Item &i) { return i.key == work.key; }),
                           list.end());
                removed = true;
            }
        }
        if (removed) {
            // a transfer cancelled at the moment it completed leaves no file behind either
            deleteFiles(work, end == End::Completed || reason != StopReason::Cancel);
        }
        finishActive();
        touch();
        persist();
    }

    // ------------------------------------------------------------------ settings

    void DownloadManager::setAutoRetry(bool enabled) {
        {
            std::lock_guard<std::mutex> lock(m);
            autoRetry = enabled;
        }
        cv.notify_all();
    }

    void DownloadManager::setProfiles(const std::vector<Credentials> &profiles) {
        std::lock_guard<std::mutex> lock(m);
        creds = profiles;
    }

    void DownloadManager::setPlaybackActive(bool active) {
        {
            std::lock_guard<std::mutex> lock(m);
            if (suspended == active) {
                return;
            }
            suspended = active;
            if (active && activeCancel) {
                stopReason = StopReason::Suspend;
                activeCancel->store(true);
            }
            LOG_I("downloads", "playback %s: downloads %s", active ? "started" : "ended", active ? "paused" : "resume");
        }
        touch();
        cv.notify_all();
    }

    bool DownloadManager::playbackActive() const {
        std::lock_guard<std::mutex> lock(m);
        return suspended;
    }

    // ------------------------------------------------------------------ queries

    std::vector<Item> DownloadManager::items() const {
        std::lock_guard<std::mutex> lock(m);
        return list;
    }

    bool DownloadManager::get(const std::string &key, Item &out) const {
        std::lock_guard<std::mutex> lock(m);
        const Item *it = findLocked(key);
        if (it) {
            out = *it;
        }
        return it != nullptr;
    }

    bool DownloadManager::find(const std::string &profileId, Kind kind, const std::string &contentId, Item &out) const {
        return get(makeKey(profileId, kind, contentId), out);
    }

    bool DownloadManager::findCompleted(const std::string &profileId, Kind kind, const std::string &contentId,
                                        Item &out) const {
        return find(profileId, kind, contentId, out) && out.state == State::Completed;
    }

    bool DownloadManager::hasCompletedEpisodes(const std::string &profileId, const std::string &seriesId) const {
        std::lock_guard<std::mutex> lock(m);
        for (const auto &it: list) {
            if (it.kind == Kind::Episode && it.state == State::Completed && it.profileId == profileId
                && it.seriesId == seriesId) {
                return true;
            }
        }
        return false;
    }

    LiveProgress DownloadManager::live() const {
        std::lock_guard<std::mutex> lock(m);
        return liveProgress;
    }

    TransferStats DownloadManager::transferStats() const {
        std::lock_guard<std::mutex> lock(m);
        return lastStats;
    }

    Totals DownloadManager::totals() const {
        std::lock_guard<std::mutex> lock(m);
        Totals t;
        for (const auto &it: list) {
            if (it.state == State::Completed) {
                t.completed++;
                t.completedBytes += it.downloadedBytes;
            } else {
                t.active++;
                t.partialBytes += it.downloadedBytes;
            }
        }
        return t;
    }

    int64_t DownloadManager::freeBytes() const {
        return cfg.freeSpace(cfg.root);
    }

    // ------------------------------------------------------------------ actions

    DownloadManager::Enqueue DownloadManager::enqueue(Item item) {
        item.key = makeKey(item.profileId, item.kind, item.contentId);
        Enqueue result;
        {
            std::lock_guard<std::mutex> lock(m);
            if (Item *e = findLocked(item.key)) {
                if (e->state == State::Completed) {
                    return Enqueue::Completed;
                }
                if (e->state == State::Failed || e->state == State::Paused) {
                    if (e->problem == Problem::FileMissing) {
                        e->downloadedBytes = 0;
                        e->durableBytes = 0;
                    }
                    if (e->problem != Problem::RestartNeeded) {
                        e->problem = Problem::None;
                    }
                    e->state = e->problem == Problem::RestartNeeded ? State::Paused : State::Queued;
                    e->attempts = 0;
                    e->retryAt = 0;
                    result = Enqueue::Resumed;
                } else {
                    return Enqueue::Exists;
                }
            } else {
                const Credentials *c = nullptr;
                for (const auto &cr: creds) {
                    if (cr.profileId == item.profileId) {
                        c = &cr;
                    }
                }
                if (c == nullptr) {
                    return Enqueue::NoProfile;
                }
                int64_t avail = cfg.freeSpace(cfg.root);
                if (avail >= 0 && avail < safetyMargin(0)) {
                    return Enqueue::NoSpace;
                }
                std::vector<std::string> taken;
                for (const auto &it: list) {
                    taken.push_back(it.fileName);
                }
                item.profileName = c->name;
                item.profileTag = profileTag(c->server, c->username);
                item.extension = sanitizeExtension(item.extension);
                item.fileName = makeFileName(item.kind, item.contentId, item.extension, item.profileId, taken);
                item.state = State::Queued;
                item.problem = Problem::None;
                item.expectedBytes = -1;
                item.downloadedBytes = 0;
                item.durableBytes = 0;
                item.createdAt = cfg.wallClock();
                item.completedAt = 0;
                item.order = nextOrder++;
                item.attempts = 0;
                item.retryAt = 0;
                list.push_back(item);
                result = Enqueue::Added;
                LOG_I("downloads", "queued %s %s as %s", item.kind == Kind::Episode ? "episode" : "movie",
                      item.contentId.c_str(), item.fileName.c_str());
            }
        }
        // a partial file left behind by an earlier cancel / crash never belongs to a new entry
        if (result == Enqueue::Added) {
            fs::removeFile(partPath(item));
        }
        touch();
        persist();
        cv.notify_all();
        return result;
    }

    void DownloadManager::pause(const std::string &key) {
        {
            std::lock_guard<std::mutex> lock(m);
            Item *it = findLocked(key);
            if (it == nullptr || it->state == State::Completed) {
                return;
            }
            if (key == activeKey && activeCancel) {
                stopReason = StopReason::Pause;
                activeCancel->store(true);   // the worker saves the state when the transfer has stopped
            } else {
                it->state = State::Paused;
            }
        }
        touch();
        persist();
    }

    void DownloadManager::resume(const std::string &key) {
        {
            std::lock_guard<std::mutex> lock(m);
            Item *it = findLocked(key);
            if (it == nullptr || it->state == State::Completed || it->state == State::Downloading) {
                return;
            }
            if (it->problem == Problem::RestartNeeded) {
                return;   // confirmRestart() decides
            }
            if (it->problem == Problem::FileMissing) {
                it->downloadedBytes = 0;
                it->durableBytes = 0;
            }
            it->state = State::Queued;
            it->problem = Problem::None;
            it->attempts = 0;
            it->retryAt = 0;
        }
        touch();
        persist();
        cv.notify_all();
    }

    void DownloadManager::confirmRestart(const std::string &key) {
        Item copy;
        {
            std::lock_guard<std::mutex> lock(m);
            Item *it = findLocked(key);
            if (it == nullptr || it->state == State::Downloading || it->state == State::Completed) {
                return;
            }
            it->downloadedBytes = 0;
            it->durableBytes = 0;
            it->expectedBytes = -1;
            it->etag.clear();
            it->lastModified.clear();
            it->problem = Problem::None;
            it->state = State::Queued;
            it->attempts = 0;
            it->retryAt = 0;
            copy = *it;
        }
        fs::removeFile(partPath(copy));
        LOG_I("downloads", "%s: restart from 0 confirmed", copy.fileName.c_str());
        touch();
        persist();
        cv.notify_all();
    }

    void DownloadManager::deleteFiles(const Item &item, bool media) {
        fs::removeFile(partPath(item));
        if (media) {
            if (fs::exists(completedPath(item)) && !fs::removeFile(completedPath(item))) {
                LOG_E("downloads", "%s: could not delete the file: errno %d", item.fileName.c_str(), errno);
            }
        }
    }

    void DownloadManager::cancel(const std::string &key) {
        Item copy;
        bool active = false;
        {
            std::lock_guard<std::mutex> lock(m);
            Item *it = findLocked(key);
            if (it == nullptr || it->state == State::Completed) {
                return;
            }
            if (key == activeKey && activeCancel) {
                stopReason = StopReason::Cancel;
                activeCancel->store(true);
                active = true;   // the worker deletes the partial once the transfer has stopped
            } else {
                copy = *it;
                list.erase(std::remove_if(list.begin(), list.end(), [&key](const Item &i) { return i.key == key; }),
                           list.end());
            }
        }
        if (!active) {
            deleteFiles(copy, false);
            LOG_I("downloads", "%s: cancelled", copy.fileName.c_str());
        }
        touch();
        persist();
    }

    bool DownloadManager::remove(const std::string &key, std::string *error) {
        Item copy;
        {
            std::lock_guard<std::mutex> lock(m);
            Item *it = findLocked(key);
            if (it == nullptr) {
                return true;
            }
            if (key == activeKey && activeCancel) {
                stopReason = StopReason::Cancel;
                activeCancel->store(true);
                return true;
            }
            copy = *it;
        }
        std::string media = completedPath(copy);
        if (copy.state == State::Completed && fs::exists(media) && !fs::removeFile(media)) {
            if (error) {
                *error = "could not delete " + copy.fileName;
            }
            LOG_E("downloads", "%s: delete failed: errno %d", copy.fileName.c_str(), errno);
            return false;
        }
        fs::removeFile(partPath(copy));
        {
            std::lock_guard<std::mutex> lock(m);
            list.erase(std::remove_if(list.begin(), list.end(), [&key](const Item &i) { return i.key == key; }),
                       list.end());
        }
        LOG_I("downloads", "%s: deleted", copy.fileName.c_str());
        touch();
        persist();
        return true;
    }

    int DownloadManager::removeAllCompleted() {
        int n = 0;
        for (const Item &it: items()) {
            if (it.state == State::Completed && remove(it.key)) {
                n++;
            }
        }
        return n;
    }

    int DownloadManager::removePartials() {
        int n = 0;
        for (const Item &it: items()) {
            if (it.state != State::Completed) {
                cancel(it.key);
                n++;
            }
        }
        return n;
    }
}
