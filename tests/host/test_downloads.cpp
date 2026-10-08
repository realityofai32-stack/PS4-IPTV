// Offline downloads: model, HTTP Range decisions, the queue engine against a scripted fake server, crash
// recovery, free space, 64-bit sizes (sparse files past 4 GiB), offline playback source and progress identity.

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#endif

#include "check.h"
#include "../../src/app/offline.h"
#include "../../src/app/vod_progress.h"
#include "../../src/downloads/download_manager.h"
#include "../../src/downloads/part_file.h"
#include "../../src/platform/fs.h"
#include "../../src/storage/library_store.h"

using namespace dl;

namespace {

    std::string freshDir(const char *name) {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string dir = fs::join(base ? base : ".", name);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        fs::ensureDir(dir);
        return dir;
    }

    char byteAt(int64_t i) {
        return (char) ((i * 131 + (i >> 13) + 7) & 0xFF);
    }

    std::string content(int64_t from, int64_t n) {
        std::string s;
        s.resize((size_t) n);
        for (int64_t i = 0; i < n; i++) {
            s[(size_t) i] = byteAt(from + i);
        }
        return s;
    }

    bool fileMatches(const std::string &path, int64_t size) {
        std::string data;
        if (!fs::readFile(path, data, (size_t) size + 16) || (int64_t) data.size() != size) {
            return false;
        }
        return data == content(0, size);
    }

    // A scripted HTTP server: generated content, Range support switchable, injected failures.
    struct FakeTransport : public Transport {
        int64_t size = 300000;
        bool rangeSupport = true;
        bool sendLength = true;
        std::string etag = "\"v1\"";
        long failStatus = 0;          // next failCount requests answer this status
        int failCount = 0;
        int networkDown = 0;          // next N requests fail to connect
        int64_t dropAfter = -1;       // this request ends (network error) after this many body bytes
        int64_t hookAfter = -1;       // calls hook once after this many body bytes
        std::function<void()> hook;
        size_t chunk = 16 * 1024;
        double *clock = nullptr;      // advanced by secondsPerChunk for every body chunk
        double secondsPerChunk = 0;
        std::string acceptRanges = "bytes";
        std::vector<int> receiveBuffers;
        std::vector<long> transferBuffers;
        std::vector<int64_t> offsets;
        std::vector<std::string> ifRanges;
        std::vector<std::string> urls;

        TransferResult run(const TransferRequest &req, TransferSink &sink) override {
            offsets.push_back(req.offset);
            ifRanges.push_back(req.ifRange);
            urls.push_back(req.url);
            receiveBuffers.push_back(req.receiveBuffer);
            transferBuffers.push_back(req.transferBuffer);
            TransferResult r;
            if (networkDown > 0) {
                networkDown--;
                r.outcome = TransferResult::Outcome::Network;
                r.detail = "Could not connect";
                return r;
            }
            sink.onSocket(32 * 1024, req.receiveBuffer > 0 ? req.receiveBuffer : 32 * 1024);
            ResponseHead h;
            h.etag = etag;
            h.acceptRanges = rangeSupport ? acceptRanges : "none";
            if (failCount > 0) {
                failCount--;
                h.status = failStatus;
                sink.onHead(h);
                r.status = failStatus;
                r.outcome = TransferResult::Outcome::HttpError;
                return r;
            }
            int64_t start = 0;
            bool ranged = req.offset > 0 && rangeSupport && (req.ifRange.empty() || req.ifRange == etag);
            if (ranged && req.offset >= size) {
                h.status = 416;
                h.range.total = size;
                sink.onHead(h);
                r.status = 416;
                r.outcome = TransferResult::Outcome::HttpError;
                return r;
            }
            if (ranged) {
                start = req.offset;
                h.status = 206;
                h.range.valid = true;
                h.range.start = start;
                h.range.end = size - 1;
                h.range.total = size;
                h.contentLength = sendLength ? size - start : -1;
            } else {
                h.status = 200;
                h.contentLength = sendLength ? size : -1;
            }
            r.status = h.status;
            if (!sink.onHead(h)) {
                r.outcome = TransferResult::Outcome::Aborted;
                return r;
            }
            int64_t sent = 0;
            for (int64_t pos = start; pos < size;) {
                int64_t n = std::min<int64_t>((int64_t) chunk, size - pos);
                if (dropAfter >= 0 && sent + n > dropAfter) {
                    n = dropAfter - sent;
                    if (n > 0) {
                        std::string d = content(pos, n);
                        sink.onData(d.data(), d.size());
                    }
                    dropAfter = -1;
                    r.outcome = TransferResult::Outcome::Network;
                    r.detail = "Recv failure";
                    return r;
                }
                if (req.cancel && req.cancel->load()) {
                    r.outcome = TransferResult::Outcome::Aborted;
                    return r;
                }
                std::string d = content(pos, n);
                if (clock) {
                    *clock += secondsPerChunk;
                }
                if (!sink.onData(d.data(), d.size())) {
                    r.outcome = TransferResult::Outcome::Aborted;
                    return r;
                }
                pos += n;
                sent += n;
                if (hookAfter >= 0 && sent >= hookAfter) {
                    hookAfter = -1;
                    if (hook) {
                        hook();
                    }
                }
            }
            r.outcome = TransferResult::Outcome::Complete;
            return r;
        }
    };

    struct Harness {
        std::string dir;
        FakeTransport transport;
        double now = 100;
        int64_t free = 100ll * 1024 * 1024 * 1024;
        std::unique_ptr<DownloadManager> mgr;

        explicit Harness(const char *name, bool fresh = true) {
            dir = fresh ? freshDir(name) : fs::join(std::getenv("PS4IPTV_TEST_TMP") ? std::getenv("PS4IPTV_TEST_TMP") : ".", name);
            make();
        }

        void make() {
            ManagerConfig c;
            c.root = dir;
            c.transport = &transport;
            c.clock = [this] { return now; };
            c.wallClock = [] { return (int64_t) 1700000000; };
            c.freeSpace = [this](const std::string &) { return free; };
            c.buildUrl = [](const Credentials &cr, Kind k, const std::string &id, const std::string &ext) {
                return cr.server + (k == Kind::Episode ? "/series/" : "/movie/") + cr.username + "/" + cr.password + "/"
                       + id + "." + ext;
            };
            c.syncEvery = 64 * 1024;
            c.spaceCheckEvery = 64 * 1024;
            mgr.reset(new DownloadManager(c));
            mgr->setProfiles({creds()});
        }

        static Credentials creds() {
            Credentials c;
            c.profileId = "p1";
            c.name = "Home";
            c.server = "http://example.com:8080";
            c.username = "alice";
            c.password = "s3cret-pw";
            return c;
        }

        static Item movie(const std::string &id) {
            Item it;
            it.kind = Kind::Movie;
            it.profileId = "p1";
            it.contentId = id;
            it.title = "Movie " + id;
            it.extension = "mkv";
            it.poster = "http://img.example.com/" + id + ".jpg";
            return it;
        }

        Item get(const std::string &id, Kind kind = Kind::Movie) {
            Item out;
            mgr->get(makeKey("p1", kind, id), out);
            return out;
        }

        std::string finalPath(const std::string &id) {
            return fs::join(fs::join(dir, "movies"), "movie_" + id + ".mkv");
        }

        std::string partPath(const std::string &id) {
            return fs::join(fs::join(dir, "temp"), "movie_" + id + ".mkv.part");
        }
    };
}

// ---------------------------------------------------------------------- model

TEST(downloads_manifest_roundtrip_64bit) {
    std::vector<Item> items;
    Item a = Harness::movie("123");
    a.key = makeKey("p1", Kind::Movie, "123");
    a.fileName = "movie_123.mkv";
    a.state = State::Completed;
    a.expectedBytes = 7812500000ll;      // > 4 GiB
    a.downloadedBytes = 7812500000ll;
    a.durableBytes = 7812500000ll;
    a.createdAt = 1700000000;
    a.completedAt = 1700003600;
    a.title = "B\xC3\xBCy\xC3\xBCk Film \xE2\x80\x94 \"Kesik\"";
    items.push_back(a);
    Item b;
    b.kind = Kind::Episode;
    b.profileId = "p2";
    b.contentId = "9001";
    b.key = makeKey("p2", Kind::Episode, "9001");
    b.seriesId = "77";
    b.seriesName = "Show";
    b.season = 1;
    b.episode = 3;
    b.fileName = "episode_9001.mp4";
    b.extension = "mp4";
    b.state = State::Failed;
    b.problem = Problem::NoSpace;
    b.neededBytes = 5000000000ll;
    b.availableBytes = 3100000000ll;
    b.downloadedBytes = 4294967297ll;    // 4 GiB + 1
    b.durableBytes = 4294967296ll;
    items.push_back(b);
    Item c = Harness::movie("5");
    c.key = makeKey("p1", Kind::Movie, "5");
    c.fileName = "movie_5.mkv";
    c.state = State::Cancelled;          // never saved
    items.push_back(c);

    std::string text = serializeManifest(items);
    std::vector<Item> back;
    std::string err;
    CHECK(parseManifest(text, back, &err));
    CHECK_EQ(back.size(), (size_t) 2);
    CHECK_EQ(back[0].expectedBytes, 7812500000ll);
    CHECK_EQ(back[0].downloadedBytes, 7812500000ll);
    CHECK_EQ(back[0].title, a.title);
    CHECK(back[0].state == State::Completed);
    CHECK_EQ(back[1].downloadedBytes, 4294967297ll);
    CHECK_EQ(back[1].durableBytes, 4294967296ll);
    CHECK_EQ(back[1].neededBytes, 5000000000ll);
    CHECK(back[1].problem == Problem::NoSpace);
    CHECK_EQ(back[1].seriesId, std::string("77"));
    CHECK_EQ(back[1].episode, 3);
    CHECK_EQ(back[1].key, makeKey("p2", Kind::Episode, "9001"));
    // no URL and no credential is ever part of the manifest
    CHECK(text.find("/movie/") == std::string::npos && text.find("password") == std::string::npos);
}

TEST(downloads_manifest_rejects_unsafe_entries) {
    std::string text = "{\"version\":1,\"items\":["
                       "{\"type\":\"movie\",\"profileId\":\"p1\",\"contentId\":\"1\",\"file\":\"../../evil.mkv\"},"
                       "{\"type\":\"movie\",\"profileId\":\"p1\",\"contentId\":\"2\",\"file\":\"a/b.mkv\"},"
                       "{\"type\":\"song\",\"profileId\":\"p1\",\"contentId\":\"3\",\"file\":\"x.mkv\"},"
                       "{\"type\":\"movie\",\"profileId\":\"p1\",\"contentId\":\"4\",\"file\":\"movie_4.mkv\",\"extension\":\"m/k\"},"
                       "{\"type\":\"movie\",\"profileId\":\"p1\",\"contentId\":\"4\",\"file\":\"movie_4b.mkv\"}]}";
    std::vector<Item> out;
    std::string note;
    CHECK(parseManifest(text, out, &note));
    CHECK_EQ(out.size(), (size_t) 1);
    CHECK_EQ(out[0].fileName, std::string("movie_4.mkv"));
    CHECK_EQ(out[0].extension, std::string("mkv"));
    CHECK(!note.empty());
    std::vector<Item> none;
    CHECK(!parseManifest("{broken", none, &note));
}

TEST(downloads_safe_file_names) {
    CHECK_EQ(makeFileName(Kind::Movie, "123", "mkv", "p1", {}), std::string("movie_123.mkv"));
    CHECK_EQ(makeFileName(Kind::Episode, "77", ".MP4", "p1", {}), std::string("episode_77.mp4"));
    std::string evil = makeFileName(Kind::Movie, "../../etc/passwd", "mkv", "p1", {});
    CHECK(evil.find('/') == std::string::npos && evil.find("..") == std::string::npos);
    CHECK_EQ(sanitizeExtension("m k v"), std::string("mkv"));
    CHECK_EQ(sanitizeExtension("verylongext"), std::string("mkv"));
    CHECK_EQ(sanitizeExtension(""), std::string("mkv"));
    CHECK_EQ(sanitizeExtension("avi"), std::string("avi"));
    // a title never becomes a path; long ids are cut
    std::string longId(300, '9');
    CHECK(makeFileName(Kind::Movie, longId, "mkv", "p1", {}).size() < 70);
    // the same id from another profile gets its own name
    CHECK_EQ(makeFileName(Kind::Movie, "123", "mkv", "p2", {"movie_123.mkv"}), std::string("movie_123_p2.mkv"));
    CHECK_EQ(makeFileName(Kind::Movie, "123", "mkv", "p2", {"movie_123.mkv", "movie_123_p2.mkv"}),
             std::string("movie_123_p2_2.mkv"));
    CHECK(profileTag("http://a:1", "u") != profileTag("http://b:1", "u"));
    CHECK_EQ(profileTag("http://a:1", "u").size(), (size_t) 16);
}

TEST(downloads_content_range_and_plans) {
    ContentRange r = parseContentRange("bytes 4294967296-5368709119/5368709120");
    CHECK(r.valid);
    CHECK_EQ(r.start, 4294967296ll);
    CHECK_EQ(r.end, 5368709119ll);
    CHECK_EQ(r.total, 5368709120ll);
    r = parseContentRange("bytes 100-199/*");
    CHECK(r.valid && r.total == -1);
    r = parseContentRange("bytes */1000");
    CHECK(!r.valid && r.total == 1000);
    CHECK(!parseContentRange("bytes 200-100/1000").valid);
    CHECK(!parseContentRange("items 1-2/3").valid);
    CHECK(!parseContentRange("bytes 0-999/999").valid);

    ResponseHead h;
    // fresh download
    h.status = 200;
    h.contentLength = 1000;
    Plan p = planResponse(0, -1, h);
    CHECK(p.action == Plan::Action::Write && p.writeOffset == 0 && p.total == 1000);
    // resume honoured
    h = ResponseHead();
    h.status = 206;
    h.range = parseContentRange("bytes 5000000000-7999999999/8000000000");
    h.contentLength = 3000000000ll;
    p = planResponse(5000000000ll, 8000000000ll, h);
    CHECK(p.action == Plan::Action::Write && p.writeOffset == 5000000000ll && p.total == 8000000000ll);
    // resume answered from another offset
    h.range = parseContentRange("bytes 4000-7999/8000");
    p = planResponse(5000, 8000, h);
    CHECK(p.action == Plan::Action::RestartFromZero);
    // the file changed size on the server
    h.range = parseContentRange("bytes 5000-8999/9000");
    p = planResponse(5000, 8000, h);
    CHECK(p.action == Plan::Action::RestartFromZero);
    // server ignored Range: a small partial restarts at 0, a large one is never dropped silently
    h = ResponseHead();
    h.status = 200;
    h.contentLength = 9000;
    p = planResponse(5000, 9000, h);
    CHECK(p.action == Plan::Action::Write && p.writeOffset == 0);
    p = planResponse(RESTART_CONFIRM_BYTES + 1, -1, h);
    CHECK(p.action == Plan::Action::AskRestart && p.problem == Problem::RestartNeeded);
    // 416 for a complete partial
    h = ResponseHead();
    h.status = 416;
    h.range = parseContentRange("bytes */9000");
    CHECK(planResponse(9000, -1, h).action == Plan::Action::AlreadyComplete);
    CHECK(planResponse(100, -1, h).action == Plan::Action::RestartFromZero);
    // errors
    h = ResponseHead();
    h.status = 404;
    CHECK(planResponse(0, -1, h).problem == Problem::HttpNotFound);
    h.status = 403;
    CHECK(planResponse(0, -1, h).problem == Problem::HttpForbidden);
    h.status = 401;
    CHECK(planResponse(0, -1, h).problem == Problem::HttpAuth);
    h.status = 503;
    CHECK(planResponse(0, -1, h).problem == Problem::HttpServer);
}

TEST(downloads_retry_space_speed_helpers) {
    RetryPolicy rp;
    CHECK_EQ(backoffDelay(rp, 1), 5.0);
    CHECK_EQ(backoffDelay(rp, 2), 10.0);
    CHECK_EQ(backoffDelay(rp, 4), 40.0);
    CHECK_EQ(backoffDelay(rp, 9), 60.0);
    CHECK(isRetryable(Problem::Network) && isRetryable(Problem::HttpForbidden) && isRetryable(Problem::HttpServer));
    CHECK(!isRetryable(Problem::HttpNotFound) && !isRetryable(Problem::NoSpace) && !isRetryable(Problem::HttpAuth));

    const int64_t GB = 1024ll * 1024 * 1024;
    CHECK_EQ(safetyMargin(0), 512ll * 1024 * 1024);
    CHECK_EQ(safetyMargin(100 * GB), 2 * GB);
    CHECK(!hasRoomFor((int64_t) (4.8 * (double) GB), (int64_t) (3.1 * (double) GB)));
    CHECK(hasRoomFor(1 * GB, 2 * GB));
    CHECK(!hasRoomFor(1 * GB, GB + 100));
    CHECK(hasRoomFor(5 * GB, -1));   // unknown: checked while writing

    SpeedMeter s;
    s.reset(0, 0);
    s.sample(0.1, 1000000);           // merged (too close)
    CHECK_EQ(s.bytesPerSecond(), 0.0);
    s.sample(1.0, 4000000);
    CHECK(s.bytesPerSecond() > 3.9e6 && s.bytesPerSecond() < 4.1e6);
    s.sample(2.0, 4000000 + 8000000); // a burst moves the average only part of the way
    CHECK(s.bytesPerSecond() > 4.5e6 && s.bytesPerSecond() < 6e6);
    CHECK(s.eta(10 * GB) > 0);
    CHECK_EQ(SpeedMeter().eta(100), -1.0);

    CHECK_EQ(formatBytes(512), std::string("512 B"));
    CHECK_EQ(formatBytes(1536), std::string("2 KB"));
    CHECK_EQ(formatBytes(3565158), std::string("3.4 MB"));
    CHECK_EQ(formatBytes(8396800000ll), std::string("7.82 GB"));
    CHECK_EQ(percent(4294967296ll, 8589934592ll), 50);
    CHECK_EQ(percent(10, -1), -1);
    CHECK_EQ(percent(10, 10), 100);
    CHECK(recoveredState(State::Downloading, true) == State::Queued);
    CHECK(recoveredState(State::WaitingForNetwork, false) == State::Paused);
    CHECK(recoveredState(State::Completed, true) == State::Completed);
    CHECK(recoveredState(State::Paused, true) == State::Paused);
}

// ---------------------------------------------------------------------- engine

TEST(downloads_fresh_download_completes_and_renames) {
    Harness h("dl_fresh");
    CHECK(h.mgr->load(true, nullptr));
    CHECK(h.mgr->enqueue(Harness::movie("1")) == DownloadManager::Enqueue::Added);
    CHECK(h.mgr->enqueue(Harness::movie("1")) == DownloadManager::Enqueue::Exists);
    CHECK(h.mgr->step());
    Item it = h.get("1");
    CHECK(it.state == State::Completed);
    CHECK_EQ(it.downloadedBytes, h.transport.size);
    CHECK_EQ(it.expectedBytes, h.transport.size);
    CHECK_EQ(it.completedAt, (int64_t) 1700000000);
    CHECK(fileMatches(h.finalPath("1"), h.transport.size));
    CHECK(!fs::exists(h.partPath("1")));
    CHECK(h.transport.offsets.size() == 1 && h.transport.offsets[0] == 0);
    // the URL was built from the profile at transfer time, and never stored
    CHECK(h.transport.urls[0].find("/movie/alice/s3cret-pw/1.mkv") != std::string::npos);
    std::string manifest;
    fs::readFile(fs::join(fs::join(h.dir, "metadata"), "downloads.json"), manifest, 1 << 20);
    CHECK(manifest.find("s3cret-pw") == std::string::npos && manifest.find("alice") == std::string::npos);
    CHECK(manifest.find("\"completed\"") != std::string::npos);
    CHECK(!h.mgr->step());   // nothing left
    CHECK(h.mgr->enqueue(Harness::movie("1")) == DownloadManager::Enqueue::Completed);
    Totals t = h.mgr->totals();
    CHECK_EQ(t.completed, 1);
    CHECK_EQ(t.completedBytes, h.transport.size);
}

TEST(downloads_pause_and_resume_with_range_206) {
    Harness h("dl_pause");
    h.mgr->load(true, nullptr);
    h.mgr->enqueue(Harness::movie("2"));
    h.transport.hookAfter = 100000;
    h.transport.hook = [&h] { h.mgr->pause(makeKey("p1", Kind::Movie, "2")); };
    CHECK(h.mgr->step());
    Item it = h.get("2");
    CHECK(it.state == State::Paused);
    CHECK(it.downloadedBytes >= 100000 && it.downloadedBytes < h.transport.size);
    CHECK_EQ(fs::fileSize(h.partPath("2")), it.downloadedBytes);
    CHECK_EQ(it.durableBytes, it.downloadedBytes);   // flushed on pause
    CHECK(!h.mgr->step());                           // paused items never start by themselves
    h.mgr->resume(it.key);
    CHECK(h.mgr->step());
    CHECK(h.get("2").state == State::Completed);
    CHECK_EQ(h.transport.offsets.size(), (size_t) 2);
    CHECK_EQ(h.transport.offsets[1], it.downloadedBytes);
    CHECK_EQ(h.transport.ifRanges[1], std::string("\"v1\""));
    CHECK(fileMatches(h.finalPath("2"), h.transport.size));
}

TEST(downloads_server_ignoring_range_is_never_appended) {
    Harness h("dl_norange");
    h.mgr->load(true, nullptr);
    h.mgr->enqueue(Harness::movie("3"));
    h.transport.hookAfter = 50000;
    h.transport.hook = [&h] { h.mgr->pause(makeKey("p1", Kind::Movie, "3")); };
    h.mgr->step();
    h.transport.rangeSupport = false;   // answers 200 with the whole file
    h.mgr->resume(makeKey("p1", Kind::Movie, "3"));
    CHECK(h.mgr->step());
    CHECK(h.get("3").state == State::Completed);
    CHECK(fileMatches(h.finalPath("3"), h.transport.size));   // restarted at 0, not appended

    // a large partial is kept until the user confirms the restart
    Harness g("dl_norange_big");
    g.transport.size = RESTART_CONFIRM_BYTES + 3 * 1024 * 1024;
    g.transport.chunk = 1024 * 1024;
    g.mgr->load(true, nullptr);
    g.mgr->enqueue(Harness::movie("4"));
    g.transport.hookAfter = RESTART_CONFIRM_BYTES + 1024 * 1024;
    g.transport.hook = [&g] { g.mgr->pause(makeKey("p1", Kind::Movie, "4")); };
    g.mgr->step();
    int64_t kept = g.get("4").downloadedBytes;
    CHECK(kept > RESTART_CONFIRM_BYTES);
    g.transport.rangeSupport = false;
    g.mgr->resume(makeKey("p1", Kind::Movie, "4"));
    CHECK(g.mgr->step());
    Item it = g.get("4");
    CHECK(it.state == State::Paused && it.problem == Problem::RestartNeeded);
    CHECK_EQ(fs::fileSize(g.partPath("4")), kept);           // nothing appended, nothing dropped
    g.mgr->resume(it.key);                                    // a plain resume does not discard it
    CHECK(!g.mgr->step());
    g.mgr->confirmRestart(it.key);
    CHECK(g.mgr->step());
    CHECK(g.get("4").state == State::Completed);
    CHECK_EQ(g.transport.offsets.back(), (int64_t) 0);
    CHECK(fileMatches(g.finalPath("4"), g.transport.size));
}

TEST(downloads_network_loss_waits_and_resumes) {
    Harness h("dl_network");
    h.mgr->load(true, nullptr);
    h.mgr->enqueue(Harness::movie("5"));
    h.transport.dropAfter = 120000;
    CHECK(h.mgr->step());
    Item it = h.get("5");
    CHECK(it.state == State::WaitingForNetwork);
    CHECK(it.problem == Problem::Network);
    CHECK(it.downloadedBytes == 120000);
    CHECK(!h.mgr->step());          // backoff: not yet
    h.transport.networkDown = 1;    // still offline at the first retry
    h.now += 5.1;
    CHECK(h.mgr->step());
    CHECK(h.get("5").state == State::WaitingForNetwork);
    CHECK_EQ(h.get("5").attempts, 2);   // no progress: the backoff grows
    h.now += 5.1;
    CHECK(!h.mgr->step());          // 10 s now
    h.now += 5.1;
    CHECK(h.mgr->step());
    CHECK(h.get("5").state == State::Completed);
    CHECK_EQ(h.transport.offsets.back(), (int64_t) 120000);
    CHECK(fileMatches(h.finalPath("5"), h.transport.size));
}

TEST(downloads_retry_limits_and_http_errors) {
    Harness h("dl_errors");
    h.mgr->load(true, nullptr);
    h.mgr->enqueue(Harness::movie("6"));
    h.transport.failStatus = 404;
    h.transport.failCount = 1;
    CHECK(h.mgr->step());
    Item it = h.get("6");
    CHECK(it.state == State::Failed && it.problem == Problem::HttpNotFound && it.httpStatus == 404);

    // 403 (provider connection limit) is retried with backoff, then given up
    h.mgr->enqueue(Harness::movie("7"));
    h.transport.failStatus = 403;
    h.transport.failCount = 100;
    int transfers = 0;
    for (int i = 0; i < 50; i++) {
        h.now += 61;
        transfers += h.mgr->step() ? 1 : 0;
    }
    it = h.get("7");
    CHECK(it.state == State::Failed && it.problem == Problem::HttpForbidden);
    CHECK_EQ(transfers, RetryPolicy().maxServerAttempts + 1);
    h.transport.failCount = 0;
    h.mgr->resume(it.key);   // the user retries
    CHECK(h.mgr->step());
    CHECK(h.get("7").state == State::Completed);

    // Retry downloads off: a network error fails at once (the user resumes by hand)
    h.mgr->setAutoRetry(false);
    h.mgr->enqueue(Harness::movie("8"));
    h.transport.networkDown = 1;
    CHECK(h.mgr->step());
    CHECK(h.get("8").state == State::Failed);
    CHECK(h.get("8").problem == Problem::Network);
}

TEST(downloads_cancel_and_delete_keep_progress) {
    Harness h("dl_cancel");
    h.mgr->load(true, nullptr);
    h.mgr->enqueue(Harness::movie("9"));
    h.transport.hookAfter = 64000;
    h.transport.hook = [&h] { h.mgr->cancel(makeKey("p1", Kind::Movie, "9")); };
    CHECK(h.mgr->step());
    Item none;
    CHECK(!h.mgr->get(makeKey("p1", Kind::Movie, "9"), none));
    CHECK(!fs::exists(h.partPath("9")));
    // cancel of a queued item
    h.mgr->enqueue(Harness::movie("10"));
    h.mgr->cancel(makeKey("p1", Kind::Movie, "10"));
    CHECK(h.mgr->items().empty());

    // delete a completed download: file and entry go, favorites / history / progress stay
    LibraryStore lib(h.dir);
    lib.setProfile("p1");
    lib.toggleFavorite(iptv::ContentType::Movie, "11");
    HistoryEntry e;
    e.type = iptv::ContentType::Movie;
    e.id = "11";
    e.name = "Movie 11";
    e.position = 1215;
    e.duration = 6000;
    lib.updateProgress(e);
    h.mgr->enqueue(Harness::movie("11"));
    h.mgr->step();
    CHECK(fs::exists(h.finalPath("11")));
    CHECK(h.mgr->remove(makeKey("p1", Kind::Movie, "11")));
    CHECK(!fs::exists(h.finalPath("11")));
    CHECK(h.mgr->items().empty());
    CHECK(lib.isFavorite(iptv::ContentType::Movie, "11"));
    CHECK(lib.progressOf(iptv::ContentType::Movie, "11") != nullptr);
    CHECK_EQ(lib.progressOf(iptv::ContentType::Movie, "11")->position, 1215.0);
}

TEST(downloads_queue_order_one_at_a_time) {
    Harness h("dl_queue");
    h.mgr->load(true, nullptr);
    for (const char *id: {"21", "22", "23"}) {
        h.mgr->enqueue(Harness::movie(id));
    }
    h.mgr->pause(makeKey("p1", Kind::Movie, "21"));
    std::vector<std::string> order;
    h.transport.hook = nullptr;
    while (h.mgr->step()) {
        order.push_back(h.transport.urls.back().substr(h.transport.urls.back().rfind('/') + 1));
        // only one transfer runs at a time: nothing else is Downloading after a step
        int downloading = 0;
        for (const Item &it: h.mgr->items()) {
            downloading += it.state == State::Downloading;
        }
        CHECK_EQ(downloading, 0);
    }
    CHECK_EQ(order.size(), (size_t) 2);
    CHECK_EQ(order[0], std::string("22.mkv"));
    CHECK_EQ(order[1], std::string("23.mkv"));
    CHECK(h.get("21").state == State::Paused);
    h.mgr->resume(makeKey("p1", Kind::Movie, "21"));
    CHECK(h.mgr->step());
    CHECK(h.get("21").state == State::Completed);
}

TEST(downloads_playback_suspends_the_transfer) {
    Harness h("dl_playback");
    h.mgr->load(true, nullptr);
    h.mgr->enqueue(Harness::movie("31"));
    h.transport.hookAfter = 80000;
    h.transport.hook = [&h] { h.mgr->setPlaybackActive(true); };
    CHECK(h.mgr->step());
    Item it = h.get("31");
    CHECK(it.state == State::Queued);    // not paused by the user: continues after playback
    int64_t at = it.downloadedBytes;
    CHECK(at >= 80000);
    CHECK(!h.mgr->step());               // nothing runs during playback
    h.mgr->setPlaybackActive(false);
    CHECK(h.mgr->step());
    CHECK(h.get("31").state == State::Completed);
    CHECK_EQ(h.transport.offsets.back(), at);
    CHECK(fileMatches(h.finalPath("31"), h.transport.size));
}

TEST(downloads_free_space_checks) {
    Harness h("dl_space");
    h.mgr->load(true, nullptr);
    h.free = 100 * 1024 * 1024;   // below the 512 MB margin
    CHECK(h.mgr->enqueue(Harness::movie("41")) == DownloadManager::Enqueue::NoSpace);
    // the size becomes known with the response: 4.8 GB needed, 3.1 GB free
    h.free = 3100ll * 1024 * 1024;
    h.mgr->enqueue(Harness::movie("42"));
    h.transport.size = 4800ll * 1024 * 1024;
    CHECK(h.mgr->step());
    Item it = h.get("42");
    CHECK(it.state == State::Failed && it.problem == Problem::NoSpace);
    CHECK_EQ(it.availableBytes, 3100ll * 1024 * 1024);
    CHECK(it.neededBytes > 4800ll * 1024 * 1024);
    CHECK(!fs::exists(h.finalPath("42")));
    // unknown size: the transfer stops before the margin is reached
    h.transport.size = 400000;
    h.transport.sendLength = false;
    h.free = 1024ll * 1024 * 1024;
    h.mgr->enqueue(Harness::movie("43"));
    h.transport.hookAfter = 100000;
    h.transport.hook = [&h] { h.free = 200 * 1024 * 1024; };
    CHECK(h.mgr->step());
    it = h.get("43");
    CHECK(it.state == State::Failed && it.problem == Problem::NoSpace);
    // without Content-Length a download completes when the server closes the body
    h.free = 100ll * 1024 * 1024 * 1024;
    h.transport.hook = nullptr;
    h.mgr->confirmRestart(it.key);
    CHECK(h.mgr->step());
    CHECK(h.get("43").state == State::Completed);
    CHECK(fileMatches(h.finalPath("43"), 400000));
}

TEST(downloads_profile_missing_or_changed) {
    Harness h("dl_profile");
    h.mgr->load(true, nullptr);
    h.mgr->enqueue(Harness::movie("51"));
    h.mgr->setProfiles({});
    CHECK(h.mgr->step());
    CHECK(h.get("51").state == State::Failed && h.get("51").problem == Problem::ProfileMissing);
    // the id now belongs to a profile with another server: never sent there
    Credentials other = Harness::creds();
    other.server = "http://other.example.com";
    h.mgr->setProfiles({other});
    h.mgr->resume(makeKey("p1", Kind::Movie, "51"));
    CHECK(h.mgr->step());
    CHECK(h.get("51").problem == Problem::ProfileMissing);
    CHECK(h.transport.urls.empty());
    CHECK(h.mgr->enqueue(Harness::movie("52")) == DownloadManager::Enqueue::Added);
    h.mgr->setProfiles({});
    Item x = Harness::movie("53");
    CHECK(h.mgr->enqueue(x) == DownloadManager::Enqueue::NoProfile);
}

TEST(downloads_restart_recovery_and_corrupt_tail) {
    std::string name = "dl_recovery";
    int64_t at = 0;
    {
        Harness h(name.c_str());
        h.transport.size = 600000;
        h.mgr->load(true, nullptr);
        h.mgr->enqueue(Harness::movie("61"));
        // the app is closed in the middle of the transfer
        h.transport.hookAfter = 300000;
        h.transport.hook = [&h] { h.mgr->shutdown(); };
        CHECK(h.mgr->step());
        Item it = h.get("61");
        CHECK(it.state == State::Downloading);   // saved as interrupted
        at = it.durableBytes;
        CHECK(at >= 300000 && at < 600000);
    }
    // garbage after the last flushed byte (as after a power cut) is cut away
    const char *base = std::getenv("PS4IPTV_TEST_TMP");
    std::string part = fs::join(base ? base : ".", "dl_recovery/temp/movie_61.mkv.part");
    {
        PartFile f;
        CHECK(f.open(part, nullptr));
        int64_t size = f.size();
        CHECK(f.truncateTo(size, nullptr));
        std::string junk(70000, 'X');
        CHECK(f.write(junk.data(), junk.size(), nullptr));
    }
    {
        Harness h(name.c_str(), false);
        h.transport.size = 600000;
        CHECK(h.mgr->load(true, nullptr));
        Item it = h.get("61");
        CHECK(it.state == State::Queued);      // auto-resume on: queued again
        CHECK_EQ(it.downloadedBytes, at);
        CHECK_EQ(fs::fileSize(part), at);
        CHECK(h.mgr->step());
        CHECK(h.get("61").state == State::Completed);
        CHECK_EQ(h.transport.offsets[0], at);
        CHECK(fileMatches(h.finalPath("61"), 600000));
    }
    {
        // auto-resume off: interrupted downloads come back paused; a missing completed file is reported
        Harness h(name.c_str(), false);
        h.transport.size = 600000;
        fs::removeFile(h.finalPath("61"));
        fs::writeFileReplace(fs::join(fs::join(h.dir, "temp"), "orphan_9.mkv.part"), "zzz");
        CHECK(h.mgr->load(false, nullptr));
        Item it = h.get("61");
        CHECK(it.state == State::Failed && it.problem == Problem::FileMissing);
        CHECK(!fs::exists(fs::join(fs::join(h.dir, "temp"), "orphan_9.mkv.part")));
        h.mgr->resume(it.key);   // download it again
        CHECK(h.mgr->step());
        CHECK(h.get("61").state == State::Completed);
    }
}

TEST(downloads_paused_survive_restart) {
    Harness h("dl_restart_paused");
    h.mgr->load(true, nullptr);
    h.mgr->enqueue(Harness::movie("71"));
    h.mgr->enqueue(Harness::movie("72"));
    h.mgr->pause(makeKey("p1", Kind::Movie, "71"));
    h.mgr.reset();
    h.make();
    CHECK(h.mgr->load(false, nullptr));
    CHECK(h.get("71").state == State::Paused);
    CHECK(h.get("72").state == State::Paused);   // queued + auto-resume off
    CHECK_EQ(h.mgr->items().size(), (size_t) 2);
    CHECK(!h.mgr->step());
}

// ---------------------------------------------------------------------- 64-bit sizes on the disk

#ifdef _WIN32
namespace {
    bool makeSparse(const std::string &path) {
        HANDLE f = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        if (f == INVALID_HANDLE_VALUE) {
            return false;
        }
        DWORD bytes = 0;
        BOOL ok = DeviceIoControl(f, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytes, nullptr);
        CloseHandle(f);
        return ok != 0;
    }
}

TEST(downloads_resume_past_4_gib_sparse) {
    Harness h("dl_large");
    const int64_t size = (4ll << 30) + 512ll * 1024 * 1024 + 12345;   // 4.5 GiB + a bit
    const int64_t have = size - 3 * 1024 * 1024 - 777;
    h.transport.size = size;
    h.transport.chunk = 256 * 1024;
    CHECK(h.mgr->load(true, nullptr));
    h.mgr->enqueue(Harness::movie("81"));
    h.mgr->pause(makeKey("p1", Kind::Movie, "81"));
    // a sparse partial of 4.5 GiB, flushed (as a paused download)
    std::string part = h.partPath("81");
    if (!makeSparse(part)) {
        std::printf("  (sparse files unavailable here: 64-bit disk test skipped)\n");
        return;
    }
    {
        PartFile f;
        CHECK(f.open(part, nullptr));
        CHECK(f.truncateTo(have, nullptr));
        CHECK_EQ(f.size(), have);
    }
    CHECK_EQ(fs::fileSize(part), have);
    h.mgr.reset();
    {
        // the manifest of the paused download says how much is durable
        std::vector<Item> items;
        std::string text;
        std::string path = fs::join(fs::join(h.dir, "metadata"), "downloads.json");
        fs::readFile(path, text, 1 << 20);
        parseManifest(text, items, nullptr);
        items[0].durableBytes = have;
        items[0].downloadedBytes = have;
        items[0].expectedBytes = size;
        fs::writeFileAtomic(path, serializeManifest(items));
    }
    h.make();
    CHECK(h.mgr->load(true, nullptr));
    CHECK_EQ(h.get("81").downloadedBytes, have);
    h.mgr->resume(makeKey("p1", Kind::Movie, "81"));
    CHECK(h.mgr->step());
    Item it = h.get("81");
    CHECK(it.state == State::Completed);
    CHECK_EQ(it.downloadedBytes, size);
    CHECK_EQ(h.transport.offsets.back(), have);
    CHECK_EQ(fs::fileSize(h.finalPath("81")), size);
    // the tail written after the 4 GiB mark is the server's bytes at their real offsets
    FILE *f = fopen(h.finalPath("81").c_str(), "rb");
    CHECK(f != nullptr);
    if (f) {
        _fseeki64(f, size - 4096, SEEK_SET);
        std::string tail(4096, '\0');
        CHECK_EQ(fread(&tail[0], 1, 4096, f), (size_t) 4096);
        fclose(f);
        CHECK(tail == content(size - 4096, 4096));
    }
    Totals t = h.mgr->totals();
    CHECK_EQ(t.completedBytes, size);
    h.mgr->remove(it.key);
    CHECK(!fs::exists(h.finalPath("81")));
}
#endif

// ---------------------------------------------------------------------- offline playback

TEST(downloads_offline_source_and_shared_progress) {
    Harness h("dl_offline");
    h.mgr->load(true, nullptr);
    Item ep;
    ep.kind = Kind::Episode;
    ep.profileId = "p1";
    ep.contentId = "9003";
    ep.seriesId = "77";
    ep.seriesName = "Show";
    ep.season = 1;
    ep.episode = 3;
    ep.title = "Third";
    ep.extension = "mkv";
    h.mgr->enqueue(ep);
    Item ep2 = ep;
    ep2.contentId = "9001";
    ep2.episode = 1;
    h.mgr->enqueue(ep2);
    h.mgr->enqueue(Harness::movie("123"));
    while (h.mgr->step()) {
    }
    // offline: no profile credentials, no transport - the manifest alone is enough
    h.mgr->setProfiles({});
    std::string path = offline::localFile(*h.mgr, "p1", iptv::ContentType::Movie, "123");
    CHECK(!path.empty() && fs::fileSize(path) == h.transport.size);
    CHECK(offline::localFile(*h.mgr, "p1", iptv::ContentType::Movie, "999").empty());
    CHECK(offline::localFile(*h.mgr, "p2", iptv::ContentType::Movie, "123").empty());
    std::vector<Item> eps = offline::seriesEpisodes(*h.mgr, "p1", "77");
    CHECK_EQ(eps.size(), (size_t) 2);
    CHECK_EQ(eps[0].contentId, std::string("9001"));
    CHECK(h.mgr->hasCompletedEpisodes("p1", "77"));
    screens::VodItem v = offline::itemFor(*h.mgr, eps[1]);
    CHECK(v.type == iptv::ContentType::Series && v.id == "9003" && v.seriesId == "77" && v.season == 1 && v.episode == 3);
    CHECK_EQ(v.localPath, h.mgr->completedPath(eps[1]));
    CHECK_EQ(v.profileId, std::string("p1"));

    // streaming item for the same movie: plays the local file instead
    screens::VodItem stream;
    stream.type = iptv::ContentType::Movie;
    stream.id = "123";
    offline::preferLocal(*h.mgr, stream, "p1");
    CHECK_EQ(stream.localPath, path);

    // progress identity: started streaming, continued offline - one record, the same position
    LibraryStore lib(h.dir);
    lib.setProfile("p1");
    {
        VodProgress p(lib);
        screens::VodItem online;
        online.type = iptv::ContentType::Movie;
        online.id = "123";
        online.title = "Movie 123";
        p.begin(online, false, 0);
        p.observe(1215.25, 6000, true, 10);
        CHECK(p.record(11, 1215.25));
    }
    {
        VodProgress p(lib);
        screens::VodItem local = stream;   // localPath set
        double start = p.begin(local, true, 20);
        CHECK_EQ(start, 1215.25);
        p.observe(5600, 6000, true, 30);
        CHECK(p.record(31, 5600));        // 93 %: watched
    }
    const HistoryEntry *e = lib.progressOf(iptv::ContentType::Movie, "123");
    CHECK(e != nullptr && e->watched);
    CHECK_EQ(lib.continueWatching(10).size(), (size_t) 0);
    int records = 0;
    for (const HistoryEntry &x: lib.history()) {
        records += x.id == "123";
    }
    CHECK_EQ(records, 1);
}

// ---------------------------------------------------------------------- speed / diagnostics (Checkpoint 3.1)

TEST(downloads_transfer_stats_math) {
    StatsMeter m;
    m.stats().title = "Movie";
    m.begin(10.0, 1000, 256 * 1024);
    CHECK(m.stats().active && m.stats().startOffset == 1000 && m.stats().transferBuffer == 256 * 1024);
    CHECK_EQ(m.stats().title, std::string("Movie"));   // kept across begin()
    m.head(10.5, 206, 5000000, 1);
    CHECK(m.stats().lengthKnown && m.stats().httpStatus == 206 && m.stats().rangeSupport == 1);
    // first byte after 1 s, then 100 KB every 0.1 s (1 MB/s) for 3 s; each write takes 1 ms
    double t = 11.0;
    for (int i = 0; i < 30; i++) {
        m.data(t, 100000, 0.001);
        t += 0.1;
    }
    m.sync(0.2);
    m.finish(t - 0.1 + 0.2);
    const TransferStats &s = m.stats();
    CHECK(!s.active);
    CHECK_EQ(s.callbacks, (int64_t) 30);
    CHECK_EQ(s.bytes, (int64_t) 3000000);
    CHECK_EQ(s.minCallback, (int64_t) 100000);
    CHECK_EQ(s.maxCallback, (int64_t) 100000);
    CHECK(std::fabs(s.firstByte - 1.0) < 1e-9);
    CHECK(std::fabs(s.elapsed - 4.1) < 1e-6);
    CHECK(std::fabs(s.writeSeconds - 0.03) < 1e-9);
    CHECK_EQ(s.syncs, 1);
    CHECK(std::fabs(s.average() - 3000000 / 4.1) < 1);
    // network side: elapsed minus the wait for the first byte, the writes and the flush
    CHECK(std::fabs(s.networkSpeed() - 3000000 / (4.1 - 1.0 - 0.03 - 0.2)) < 1);
    CHECK(std::fabs(s.diskSpeed() - 1e8) < 1);
    CHECK(std::fabs(s.averageCallback() - 100000) < 1e-6);
    CHECK(std::fabs(s.callbacksPerSecond() - 30 / 4.1) < 1e-6);
    // peak: the best 1-second window (1 MB/s here)
    CHECK(s.peak > 0.95e6 && s.peak < 1.12e6);
    // nothing transferred: no division by zero
    TransferStats empty;
    CHECK(empty.average() == 0 && empty.networkSpeed() == 0 && empty.diskSpeed() == 0 && empty.averageCallback() == 0);
}

TEST(downloads_instrumentation_callbacks_and_buffers) {
    Harness h("dl_stats");
    h.mgr->load(true, nullptr);
    CHECK(h.mgr->transferStats().title.empty());   // nothing ran yet
    h.transport.clock = &h.now;
    h.transport.secondsPerChunk = 0.05;            // 16 KB every 50 ms
    h.mgr->enqueue(Harness::movie("s1"));
    CHECK(h.mgr->step());
    CHECK(h.get("s1").state == State::Completed);
    TransferStats s = h.mgr->transferStats();
    CHECK(!s.active);
    CHECK_EQ(s.title, std::string("Movie s1"));
    // one callback per 16 KB chunk; the last one is the remainder
    int64_t chunks = (h.transport.size + 16383) / 16384;
    CHECK_EQ(s.callbacks, chunks);
    CHECK_EQ(s.bytes, h.transport.size);
    CHECK_EQ(s.maxCallback, (int64_t) 16384);
    CHECK_EQ(s.minCallback, h.transport.size - (chunks - 1) * 16384);
    CHECK(s.httpStatus == 200 && s.lengthKnown && s.rangeSupport == 1);   // Accept-Ranges: bytes
    // the receive buffer and libcurl buffer of the configuration reach the transport; the socket's are reported
    CHECK_EQ(h.transport.receiveBuffers[0], 1024 * 1024);
    CHECK_EQ(h.transport.transferBuffers[0], 256L * 1024);
    CHECK_EQ(s.receiveBufferDefault, 32 * 1024);
    CHECK_EQ(s.receiveBuffer, 1024 * 1024);
    CHECK_EQ(s.transferBuffer, 256L * 1024);
    // speeds from the transferred bytes and the (monotonic) clock: 16384 B / 0.05 s
    CHECK(std::fabs(s.average() - 327680) < 30000);
    CHECK(s.peak > 300000 && s.peak < 360000);
    CHECK(s.smoothed > 300000 && s.smoothed < 360000);
    // a resume answered with 206 counts as Range support
    Harness r("dl_stats_range");
    r.mgr->load(true, nullptr);
    r.mgr->enqueue(Harness::movie("s2"));
    r.transport.hookAfter = 100000;
    r.transport.hook = [&r] { r.mgr->pause(makeKey("p1", Kind::Movie, "s2")); };
    r.mgr->step();
    r.mgr->resume(makeKey("p1", Kind::Movie, "s2"));
    r.mgr->step();
    s = r.mgr->transferStats();
    CHECK(s.httpStatus == 206 && s.rangeSupport == 1 && s.startOffset >= 100000);
    CHECK_EQ(s.bytes, r.transport.size - s.startOffset);
    // a server that ignores Range on a resume: no Range support
    Harness n("dl_stats_norange");
    n.mgr->load(true, nullptr);
    n.mgr->enqueue(Harness::movie("s3"));
    n.transport.hookAfter = 20000;
    n.transport.hook = [&n] { n.mgr->pause(makeKey("p1", Kind::Movie, "s3")); };
    n.mgr->step();
    n.transport.rangeSupport = false;
    n.mgr->resume(makeKey("p1", Kind::Movie, "s3"));
    n.mgr->step();
    CHECK_EQ(n.mgr->transferStats().rangeSupport, 0);
}

TEST(downloads_flush_and_manifest_are_bounded) {
    // byte bound: the harness flushes every 64 KB; the manifest is written with each flush, never per callback
    Harness h("dl_flush");
    h.mgr->load(true, nullptr);
    h.transport.size = 1000000;
    h.mgr->enqueue(Harness::movie("f1"));
    CHECK(h.mgr->step());
    TransferStats s = h.mgr->transferStats();
    CHECK_EQ(s.callbacks, (int64_t) 62);
    CHECK_EQ(s.syncs, 15 + 1);              // 15 x 64 KB while writing + the one when the transfer ends
    CHECK_EQ(s.manifestWrites, 15);         // one per durable flush; 62 callbacks wrote none
    CHECK(h.get("f1").state == State::Completed);

    // the production policy: 32 MiB or 15 s, whichever first, and only while data arrives
    Harness t("dl_flush_time");
    ManagerConfig defaults;
    CHECK_EQ(defaults.syncEvery, 32ll * 1024 * 1024);
    CHECK(defaults.syncInterval == 15);
    CHECK_EQ(defaults.receiveBuffer, 1024 * 1024);
    CHECK_EQ(defaults.transferBuffer, 256L * 1024);
    t.mgr.reset();
    {
        ManagerConfig c;
        c.root = t.dir;
        c.transport = &t.transport;
        c.clock = [&t] { return t.now; };
        c.freeSpace = [](const std::string &) { return (int64_t) 1 << 40; };
        c.buildUrl = [](const Credentials &, Kind, const std::string &id, const std::string &) { return "u/" + id; };
        t.mgr.reset(new DownloadManager(c));
        t.mgr->setProfiles({Harness::creds()});
    }
    t.mgr->load(true, nullptr);
    t.transport.size = 16384 * 40;          // 40 callbacks, 1 s apart (a slow link): 40 s
    t.transport.clock = &t.now;
    t.transport.secondsPerChunk = 1.0;
    t.mgr->enqueue(Harness::movie("f2"));
    unsigned before = t.mgr->generation();
    CHECK(t.mgr->step());
    s = t.mgr->transferStats();
    CHECK_EQ(s.callbacks, (int64_t) 40);
    CHECK_EQ(s.syncs, 2 + 1);               // at 15 s and 30 s, then at the end
    CHECK_EQ(s.manifestWrites, 2);
    // live progress for the UI: at most 4 updates per second of transfer time, not one per callback
    unsigned updates = t.mgr->generation() - before;
    CHECK(updates <= 40 + 8);
    CHECK(t.get("f2").state == State::Completed);
}

TEST(downloads_progress_rate_is_ui_rate_not_callback_rate) {
    Harness h("dl_ui_rate");
    h.mgr->load(true, nullptr);
    h.transport.size = 16384 * 400;         // 400 callbacks in 2 s (5 ms apart)
    h.transport.clock = &h.now;
    h.transport.secondsPerChunk = 0.005;
    h.mgr->enqueue(Harness::movie("u1"));
    unsigned before = h.mgr->generation();
    CHECK(h.mgr->step());
    unsigned changes = h.mgr->generation() - before;
    // 2 s at 4 updates per second + a few state changes; never one per callback
    CHECK(changes >= 6 && changes <= 16);
    CHECK_EQ(h.mgr->transferStats().callbacks, (int64_t) 400);
}
