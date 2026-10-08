// Download integration test: the real libcurl transport (dl::CurlTransport, libcurl 7.80.0 as on the PS4) and
// the download manager against scripts/download-test-server.py on 127.0.0.1 - Content-Length, Range / 206,
// If-Range, redirects, interrupted bodies with retry, 403 then OK, 404, bodies without a length, a server that
// ignores Range, pause / cancel from another thread, the worker thread, and a resume past 4 GiB.
// Run by scripts/run-download-integration-test.ps1 (PS4IPTV_TEST_SERVER = http://127.0.0.1:<port>).

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#endif

#include <curl/curl.h>

#include "check.h"
#include "../../src/core/json.h"
#include "../../src/downloads/curl_transport.h"
#include "../../src/downloads/download_manager.h"
#include "../../src/downloads/part_file.h"
#include "../../src/platform/fs.h"

using namespace dl;

namespace {

    std::string server() {
        const char *s = std::getenv("PS4IPTV_TEST_SERVER");
        return s ? s : "";
    }

    double monotonic() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    char byteAt(int64_t i) {
        return (char) ((i * 131 + (i >> 13) + 7) & 0xFF);
    }

    bool fileMatches(const std::string &path, int64_t size) {
        std::string data;
        if (!fs::readFile(path, data, (size_t) size + 16) || (int64_t) data.size() != size) {
            return false;
        }
        for (int64_t i = 0; i < size; i++) {
            if (data[(size_t) i] != byteAt(i)) {
                return false;
            }
        }
        return true;
    }

    size_t toString(char *d, size_t s, size_t n, void *u) {
        ((std::string *) u)->append(d, s * n);
        return s * n;
    }

    std::string httpGet(const std::string &path) {
        std::string body;
        CURL *c = curl_easy_init();
        std::string url = server() + path;
        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, toString);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
        curl_easy_perform(c);
        curl_easy_cleanup(c);
        return body;
    }

    // Range headers the server saw for paths containing `needle`
    std::vector<std::string> rangesFor(const std::string &needle) {
        json::Value log;
        json::parse(httpGet("/_log"), log);
        std::vector<std::string> out;
        for (const auto &e: log.items()) {
            if (e["path"].asString().find(needle) != std::string::npos) {
                out.push_back(e["range"].asString());
            }
        }
        return out;
    }

    struct It {
        std::string dir;
        CurlTransport transport{"PS4IPTV-test/1.0", ""};
        double clockOffset = 0;
        std::unique_ptr<DownloadManager> mgr;

        It(const char *name, const std::string &mode, bool keep = false) {
            const char *base = std::getenv("PS4IPTV_TEST_TMP");
            dir = fs::join(base ? base : ".", name);
            if (!keep) {
                std::error_code ec;
                std::filesystem::remove_all(dir, ec);
            }
            fs::ensureDir(dir);
            make(mode);
        }

        std::string mode;

        // the server's behaviour is chosen by a path prefix, so the profile (server + user) stays the same
        void make(const std::string &m) {
            mode = m;
            ManagerConfig c;
            c.root = dir;
            c.transport = &transport;
            c.clock = [this] { return monotonic() + clockOffset; };
            c.wallClock = [] { return (int64_t) 1700000000; };
            c.buildUrl = [this](const Credentials &cr, Kind k, const std::string &id, const std::string &ext) {
                return cr.server + "/" + mode + (k == Kind::Episode ? "/series/" : "/movie/") + cr.username + "/"
                       + cr.password + "/" + id + "." + ext;
            };
            mgr.reset(new DownloadManager(c));
            Credentials cr;
            cr.profileId = "p1";
            cr.name = "Test";
            cr.server = m.empty() ? "http://127.0.0.1:9" : server();
            cr.username = "alice";
            cr.password = "pw";
            mgr->setProfiles({cr});
        }

        static Item movie(const std::string &id) {
            Item it;
            it.kind = Kind::Movie;
            it.profileId = "p1";
            it.contentId = id;
            it.title = "Movie " + id;
            it.extension = "mkv";
            return it;
        }

        Item get(const std::string &id) {
            Item out;
            mgr->get(makeKey("p1", Kind::Movie, id), out);
            return out;
        }

        std::string finalPath(const std::string &id) {
            return fs::join(fs::join(dir, "movies"), "movie_" + id + ".mkv");
        }

        // runs steps until nothing is runnable; retry timers are skipped by moving the clock
        void drain(int maxSteps = 20) {
            for (int i = 0; i < maxSteps; i++) {
                if (!mgr->step()) {
                    clockOffset += 61;
                    if (!mgr->step()) {
                        return;
                    }
                }
            }
        }
    };
}

TEST(it_plain_download_with_length) {
    httpGet("/_reset");
    It t("it_plain", "plain");
    t.mgr->load(true, nullptr);
    t.mgr->enqueue(It::movie("1500000"));
    t.drain();
    Item it = t.get("1500000");
    CHECK(it.state == State::Completed);
    CHECK_EQ(it.downloadedBytes, (int64_t) 1500000);
    CHECK(fileMatches(t.finalPath("1500000"), 1500000));
    CHECK_EQ(it.etag, std::string("\"1500000-1500000\""));
}

TEST(it_redirect_is_followed) {
    It t("it_redirect", "redirect");
    t.mgr->load(true, nullptr);
    t.mgr->enqueue(It::movie("700001"));
    t.drain();
    CHECK(t.get("700001").state == State::Completed);
    CHECK(fileMatches(t.finalPath("700001"), 700001));
}

TEST(it_interrupted_body_resumes_with_range) {
    httpGet("/_reset");
    It t("it_flaky", "flaky");
    t.mgr->load(true, nullptr);
    t.mgr->enqueue(It::movie("1200000"));
    CHECK(t.mgr->step());
    Item it = t.get("1200000");
    CHECK(it.state == State::WaitingForNetwork);      // never Failed at the first drop
    CHECK(it.problem == Problem::Network);
    CHECK_EQ(it.downloadedBytes, (int64_t) 600000);
    t.drain();
    CHECK(t.get("1200000").state == State::Completed);
    CHECK(fileMatches(t.finalPath("1200000"), 1200000));
    std::vector<std::string> ranges = rangesFor("/flaky/movie/");
    CHECK_EQ(ranges.size(), (size_t) 2);
    if (ranges.size() == 2) {
        CHECK_EQ(ranges[0], std::string(""));
        CHECK_EQ(ranges[1], std::string("bytes=600000-"));
    }
}

TEST(it_server_ignoring_range_restarts_cleanly) {
    httpGet("/_reset");
    It t("it_norange", "slow");
    t.mgr->load(true, nullptr);
    t.mgr->enqueue(It::movie("900000"));
    std::string key = makeKey("p1", Kind::Movie, "900000");
    std::thread pauser([&t, key] {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        t.mgr->pause(key);   // from another thread, while the worker transfers
    });
    CHECK(t.mgr->step());
    pauser.join();
    Item it = t.get("900000");
    CHECK(it.state == State::Paused);
    CHECK(it.downloadedBytes > 0 && it.downloadedBytes < 900000);
    // the same file is now served by a server without Range support
    t.make("norange");
    CHECK(t.mgr->load(true, nullptr));
    t.mgr->resume(key);
    t.drain();
    CHECK(t.get("900000").state == State::Completed);
    CHECK(fileMatches(t.finalPath("900000"), 900000));   // restarted at 0, never appended
    std::vector<std::string> ranges = rangesFor("/norange/movie/");
    CHECK(!ranges.empty() && ranges[0].find("bytes=") == 0);
}

TEST(it_pause_resume_206_and_cancel) {
    httpGet("/_reset");
    It t("it_pause", "slow");
    t.mgr->load(true, nullptr);
    t.mgr->enqueue(It::movie("800000"));
    std::string key = makeKey("p1", Kind::Movie, "800000");
    std::thread pauser([&t, key] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        t.mgr->pause(key);
    });
    t.mgr->step();
    pauser.join();
    int64_t at = t.get("800000").downloadedBytes;
    CHECK(at > 0 && at < 800000);
    t.mgr->resume(key);
    t.drain();
    CHECK(t.get("800000").state == State::Completed);
    CHECK(fileMatches(t.finalPath("800000"), 800000));
    std::vector<std::string> ranges = rangesFor("/slow/movie/");
    CHECK(ranges.size() >= 2 && ranges.back() == "bytes=" + std::to_string(at) + "-");

    t.mgr->enqueue(It::movie("800001"));
    std::string k2 = makeKey("p1", Kind::Movie, "800001");
    std::thread canceller([&t, k2] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        t.mgr->cancel(k2);
    });
    t.mgr->step();
    canceller.join();
    Item none;
    CHECK(!t.mgr->get(k2, none));
    CHECK(!fs::exists(fs::join(fs::join(t.dir, "temp"), "movie_800001.mkv.part")));
}

TEST(it_http_errors) {
    httpGet("/_reset");
    It missing("it_missing", "missing");
    missing.mgr->load(true, nullptr);
    missing.mgr->enqueue(It::movie("1000"));
    missing.drain();
    Item it = missing.get("1000");
    CHECK(it.state == State::Failed && it.problem == Problem::HttpNotFound && it.httpStatus == 404);

    It forbid("it_forbid", "forbid1");
    forbid.mgr->load(true, nullptr);
    forbid.mgr->enqueue(It::movie("300000"));
    CHECK(forbid.mgr->step());
    it = forbid.get("300000");
    CHECK(it.state == State::Queued && it.problem == Problem::HttpForbidden);   // retried after a delay
    forbid.drain();
    CHECK(forbid.get("300000").state == State::Completed);
    CHECK(fileMatches(forbid.finalPath("300000"), 300000));
}

TEST(it_body_without_length) {
    It t("it_nolength", "nolength");
    t.mgr->load(true, nullptr);
    t.mgr->enqueue(It::movie("654321"));
    t.drain();
    Item it = t.get("654321");
    CHECK(it.state == State::Completed);
    CHECK_EQ(it.downloadedBytes, (int64_t) 654321);
    CHECK(fileMatches(t.finalPath("654321"), 654321));
}

TEST(it_unreachable_server_waits_for_network) {
    It t("it_unreachable", "");   // a closed port on 127.0.0.1
    t.mgr->load(true, nullptr);
    t.mgr->enqueue(It::movie("1000"));
    CHECK(t.mgr->step());
    Item it = t.get("1000");
    CHECK(it.state == State::WaitingForNetwork && it.problem == Problem::Network);
}

TEST(it_worker_thread) {
    It t("it_worker", "plain");
    t.mgr->load(true, nullptr);
    t.mgr->startWorker();
    t.mgr->enqueue(It::movie("400000"));
    t.mgr->enqueue(It::movie("400001"));
    double until = monotonic() + 30;
    while (monotonic() < until && (t.get("400000").state != State::Completed || t.get("400001").state != State::Completed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    t.mgr->stopWorker();
    CHECK(t.get("400000").state == State::Completed);
    CHECK(t.get("400001").state == State::Completed);
    CHECK(fileMatches(t.finalPath("400001"), 400001));
}

#ifdef _WIN32
TEST(it_resume_past_4_gib) {
    httpGet("/_reset");
    It t("it_big", "big");
    const int64_t size = (5ll << 30) + 2000000;           // "big" mode: 5 GiB + id bytes
    const int64_t have = size - 1500000;
    t.mgr->load(true, nullptr);
    t.mgr->enqueue(It::movie("2000000"));
    std::string key = makeKey("p1", Kind::Movie, "2000000");
    t.mgr->pause(key);
    std::string part = fs::join(fs::join(t.dir, "temp"), "movie_2000000.mkv.part");
    HANDLE f = CreateFileA(part.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD bytes = 0;
    bool sparse = f != INVALID_HANDLE_VALUE && DeviceIoControl(f, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytes, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        CloseHandle(f);
    }
    if (!sparse) {
        std::printf("  (sparse files unavailable: skipped)\n");
        return;
    }
    {
        PartFile p;
        CHECK(p.open(part, nullptr) && p.truncateTo(have, nullptr));
    }
    std::string mpath = fs::join(fs::join(t.dir, "metadata"), "downloads.json");
    std::string text;
    fs::readFile(mpath, text, 1 << 20);
    std::vector<Item> items;
    parseManifest(text, items, nullptr);
    items[0].durableBytes = have;
    items[0].downloadedBytes = have;
    items[0].expectedBytes = size;
    items[0].etag = "\"2000000-" + std::to_string(size) + "\"";
    fs::writeFileAtomic(mpath, serializeManifest(items));
    t.make("big");
    CHECK(t.mgr->load(true, nullptr));
    t.mgr->resume(key);
    t.drain();
    Item it = t.get("2000000");
    CHECK(it.state == State::Completed);
    CHECK_EQ(it.downloadedBytes, size);
    std::vector<std::string> ranges = rangesFor("/big/movie/");
    CHECK(!ranges.empty() && ranges.back() == "bytes=" + std::to_string(have) + "-");
    CHECK_EQ(fs::fileSize(t.finalPath("2000000")), size);
    FILE *fp = fopen(t.finalPath("2000000").c_str(), "rb");
    CHECK(fp != nullptr);
    if (fp) {
        _fseeki64(fp, size - 100000, SEEK_SET);
        std::string tail(100000, '\0');
        CHECK_EQ(fread(&tail[0], 1, tail.size(), fp), tail.size());
        fclose(fp);
        bool same = true;
        for (int64_t i = 0; i < 100000; i++) {
            same &= tail[(size_t) i] == byteAt(size - 100000 + i);
        }
        CHECK(same);
    }
    t.mgr->remove(key);
}
#endif
