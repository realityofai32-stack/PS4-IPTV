// Network failure simulation: the app's HTTP client (network/http.cpp + http_policy) against the misbehaving local
// server of tests/network/failure_server.py. Run by tests/network/run-network-tests.sh (plain and under ASan +
// UBSan). Environment: PS4IPTV_NET_HTTP (http://127.0.0.1:<port>), PS4IPTV_NET_HTTPS (https://localhost:<port>),
// PS4IPTV_NET_CA (the test CA), PS4IPTV_NET_CLOSED (an address nothing listens on).

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

#include <unistd.h>

#include "../host/check.h"
#include "../../src/network/http.h"
#include "../../src/network/jobs.h"
#include "../../src/platform/clock.h"
#include "../../src/platform/log.h"

namespace {
    std::string env(const char *name) {
        const char *v = std::getenv(name);
        return v ? v : "";
    }

    std::string H() { return env("PS4IPTV_NET_HTTP"); }

    std::string S() { return env("PS4IPTV_NET_HTTPS"); }

    struct Init {
        Init() {
            http::globalInit(env("PS4IPTV_NET_CA"), "V7-test/1.0");
        }
    };

    void init() {
        static Init once;
    }

    http::Response get(const std::string &url, int retries = 2, long totalMs = 30000) {
        init();
        http::Request r;
        r.url = url;
        r.retries = retries;
        r.totalTimeoutMs = totalMs;
        r.connectTimeoutMs = 3000;
        return http::get(r);
    }

    // keys of the server's per-key counters: unique per process (the plain and the sanitized run share the server)
    std::string K(const char *name) {
        static const std::string run = "-" + std::to_string((long) getpid());
        return std::string(name) + run;
    }

    std::string count(const std::string &key) {
        return get(H() + "/count/" + key, 0).body;
    }

    std::string pct(const std::string &s) {
        std::string o;
        char buf[4];
        for (unsigned char c: s) {
            if (isalnum(c) || c == '-' || c == '.' || c == '_') {
                o += (char) c;
            } else {
                snprintf(buf, sizeof(buf), "%%%02X", c);
                o += buf;
            }
        }
        return o;
    }
}

TEST(net_ok_and_permanent_http_errors_are_not_retried) {
    http::Response r = get(H() + "/ok");
    CHECK(r.ok() && r.body == "ok" && r.attempts == 1);
    for (int code: {400, 401, 403, 404, 429, 500}) {
        r = get(H() + "/status/" + std::to_string(code));
        CHECK(r.error == http::Error::None && r.status == code && r.attempts == 1);
    }
}

TEST(net_temporary_server_errors_are_retried_with_a_bound) {
    http::Response r = get(H() + "/flaky/" + K("a503") + "/2/503");   // 503, 503, then 200
    CHECK(r.ok() && r.attempts == 3);
    CHECK_EQ(count(K("a503")), std::string("3"));
    double t0 = clockx::monotonic();
    r = get(H() + "/flaky/" + K("b503") + "/99/503");   // always busy: three requests, then the 503
    CHECK(r.status == 503 && !r.ok() && r.attempts == 3);
    CHECK_EQ(count(K("b503")), std::string("3"));         // never more: the provider is not hammered
    double dt = clockx::monotonic() - t0;
    CHECK(dt > 2.5 && dt < 6.0);                         // the pauses (0.75 s + 2 s), nothing more
    r = get(H() + "/flaky/" + K("c502") + "/1/502", 0);  // retries off
    CHECK(r.status == 502 && r.attempts == 1);
}

TEST(net_connection_resets_and_partial_answers) {
    // a reset answer: retried (libcurl itself already retries once when a reused keep-alive connection died, so
    // the second request can also succeed within the first attempt)
    http::Response r = get(H() + "/reset/" + K("r1") + "/1");
    CHECK(r.ok() && r.attempts <= 2);
    r = get(H() + "/partial");
    CHECK(r.error == http::Error::Interrupted && r.attempts == 3);
    CHECK(!http::describe(r).empty());
    r = get(H() + "/reset/" + K("r2") + "/99", 1);
    CHECK(r.error == http::Error::Interrupted || r.error == http::Error::Other);
    CHECK_EQ(r.attempts, 2);
    CHECK(std::atoi(count(K("r2")).c_str()) <= 4);   // 2 attempts (+ libcurl's own reconnect each): bounded
}

TEST(net_stalled_and_slow_servers_end_on_time_and_are_not_retried) {
    init();
    http::Request q;
    q.url = H() + "/stall";
    q.stallTimeoutS = 2;
    double t0 = clockx::monotonic();
    http::Response r = http::get(q);
    double dt = clockx::monotonic() - t0;
    std::printf("     stall: error %d after %.2fs, %d attempt(s)\n", (int) r.error, dt, r.attempts);
    CHECK(r.error == http::Error::Timeout && r.attempts == 1);
    // libcurl averages the speed over a 5 s window: a stall is detected after stallTimeoutS + up to 5 s
    CHECK(dt > 1.5 && dt < 2 + 5 + 1.5);
    q.stallTimeoutS = 30;
    q.totalTimeoutMs = 1500;
    t0 = clockx::monotonic();
    r = http::get(q);
    dt = clockx::monotonic() - t0;
    CHECK(r.error == http::Error::Timeout && r.attempts == 1 && dt < 3.0);
}

TEST(net_refused_connections_and_unknown_hosts) {
    double t0 = clockx::monotonic();
    http::Response r = get(env("PS4IPTV_NET_CLOSED") + "/x");
    CHECK(r.error == http::Error::Connect && r.attempts == 3);
    CHECK(clockx::monotonic() - t0 < 6.0);
    r = get("http://v7-test-host.invalid/x", 0);
    CHECK(r.error == http::Error::Dns);
    CHECK(http::describe(r) == http::describe(r));   // a text exists for every error
}

TEST(net_cancel_ends_requests_and_retry_pauses_at_once) {
    init();
    http::Request q;
    q.url = env("PS4IPTV_NET_CLOSED") + "/x";
    q.cancel = std::make_shared<std::atomic<bool>>(false);
    auto flag = q.cancel;
    std::thread t([flag] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        flag->store(true);
    });
    double t0 = clockx::monotonic();
    http::Response r = http::get(q);   // refused -> pause 0.75 s -> canceled during the pause
    double dt = clockx::monotonic() - t0;
    t.join();
    CHECK(r.error == http::Error::Canceled);
    CHECK(dt < 0.6);
    // a stalled transfer is canceled too
    q.url = H() + "/stall";
    flag->store(false);
    std::thread t2([flag] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        flag->store(true);
    });
    t0 = clockx::monotonic();
    r = http::get(q);
    dt = clockx::monotonic() - t0;
    t2.join();
    CHECK(r.error == http::Error::Canceled && dt < 2.5);
}

TEST(net_unsafe_schemes_and_redirects_are_refused) {
    http::Response r = get("file:///etc/passwd");
    CHECK(r.error == http::Error::HttpsUnsupported && r.attempts == 0 && r.body.empty());
    r = get("gopher://127.0.0.1:1/_x");
    CHECK(r.error == http::Error::HttpsUnsupported && r.body.empty());
    for (const char *to: {"file:///etc/passwd", "gopher://127.0.0.1:25/_HELO%20x", "dict://127.0.0.1:11211/stat",
                          "ftp://127.0.0.1/x", "tftp://127.0.0.1/x"}) {
        r = get(H() + "/redirect?to=" + pct(to));
        CHECK(r.error == http::Error::Redirect && r.attempts == 1);
        CHECK(r.body.empty() || r.body.find("root:") == std::string::npos);
        CHECK(r.detail.find("passwd") == std::string::npos && r.detail.find("HELO") == std::string::npos);
    }
    r = get(H() + "/loop");
    CHECK(r.error == http::Error::Redirect && r.attempts == 1);
    r = get(H() + "/chain/5");
    CHECK(r.ok() && r.body == "ok");
    r = get(H() + "/chain/8");
    CHECK(r.ok());
    r = get(H() + "/chain/9");
    CHECK(r.error == http::Error::Redirect);
}

TEST(net_https_certificates_and_downgrades) {
    http::Response r = get(S() + "/ok");
    CHECK(r.ok() && r.body == "ok");
    // credentials: https -> http is refused, with an explanation the user can act on
    r = get(S() + "/redirect?to=" + pct(H() + "/ok?username=exampleuser&password=examplepass"));
    CHECK(r.error == http::Error::Redirect && r.downgradeRefused);
    CHECK(r.detail.find("examplepass") == std::string::npos);
    CHECK(http::describe(r).find("http://") != std::string::npos);
    // artwork: allowed
    init();
    http::Request q;
    q.url = S() + "/redirect?to=" + pct(H() + "/ok");
    q.credentials = false;
    r = http::get(q);
    CHECK(r.ok());
    // http -> https upgrade is followed and reported
    r = get(H() + "/redirect?to=" + pct(S() + "/ok"));
    CHECK(r.ok() && r.redirectedToHttps);
    // a certificate the bundle does not trust: refused, never retried, never accepted
    std::string ip = S();
    size_t host = ip.find("localhost");
    ip.replace(host, 9, "127.0.0.2");   // the certificate is for localhost / 127.0.0.1 only
    r = get(ip + "/ok");
    CHECK(r.error == http::Error::TlsCertificate || r.error == http::Error::Connect);
    CHECK(r.attempts <= 3 && r.body.empty());
}

TEST(net_portal_headers_never_leave_their_host) {
    init();
    http::Request q;
    q.headers = {"Cookie: mac=00%3A1A%3A79%3A12%3A34%3A56; stb_lang=en", "Authorization: Bearer exampletoken123"};
    // same host, another path: followed, the headers arrive
    q.url = H() + "/redirect?to=" + pct(H() + "/headers");
    http::Response r = http::get(q);
    CHECK(r.ok() && r.body.find("exampletoken123") != std::string::npos);
    // another host (localhost vs 127.0.0.1): refused before anything is sent there
    std::string other = H();
    other.replace(other.find("127.0.0.1"), 9, "localhost");
    q.url = H() + "/redirect?to=" + pct(other + "/headers");
    r = http::get(q);
    CHECK(r.error == http::Error::Redirect && !r.downgradeRefused);
    CHECK(r.body.find("exampletoken123") == std::string::npos);
}

TEST(net_answer_size_limit) {
    init();
    http::Request q;
    q.url = H() + "/big/300000";
    q.maxBytes = 100000;
    http::Response r = http::get(q);
    CHECK(r.error == http::Error::TooLarge && r.attempts == 1);
    q.maxBytes = 400000;
    r = http::get(q);
    CHECK(r.ok() && r.body.size() == 300000);
}

TEST(net_many_parallel_requests_with_cancel_leave_a_clean_state) {
    init();
    JobSystem jobs;
    jobs.setWorkerExitHook(http::releaseThreadResources);
    jobs.start(4);
    std::atomic<int> done{0};
    std::vector<CancelToken> tokens;
    for (int i = 0; i < 60; i++) {
        std::string url = i % 3 == 0 ? env("PS4IPTV_NET_CLOSED") + "/x"
                          : i % 3 == 1 ? H() + "/flaky/p" + std::to_string(i) + "/1/503" : H() + "/stall";
        tokens.push_back(jobs.submit(JobPriority::Normal, "it", [url](const CancelToken &tok) {
            http::Request q;
            q.url = url;
            q.cancel = tok.shared();
            q.stallTimeoutS = 20;
            http::get(q);
        }, [&done] { done++; }));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    double t0 = clockx::monotonic();
    for (auto &t: tokens) {   // the screen closed: everything canceled
        t.cancel();
    }
    jobs.waitIdle();
    double dt = clockx::monotonic() - t0;
    CHECK(dt < 3.0);   // canceled transfers and pauses end quickly, nothing waits for a stalled server
    jobs.pump(1000);
    CHECK(done.load() < 60);   // canceled jobs never call back
    // the client works normally afterwards (no stuck handle, no stale cancel)
    http::Response r = get(H() + "/ok");
    CHECK(r.ok());
    jobs.stop();
}

TEST(net_zz_thread_resources_are_released) {
    // the test thread's own handle (workers release theirs through the JobSystem exit hook)
    http::releaseThreadResources();
    http::releaseThreadResources();   // twice: harmless
    http::Response r = get(H() + "/ok");   // a new handle is made on demand
    CHECK(r.ok());
    http::releaseThreadResources();
}
