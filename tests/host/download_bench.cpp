// Download speed benchmark: the app's real download pipeline (DownloadManager + CurlTransport with libcurl 7.80 +
// PartFile writes + manifest) against the real provider, for a set of receive / transfer buffer sizes.
// Run by scripts/run-download-bench.ps1. Credentials come from config/test_streams.txt (git-ignored); nothing
// secret is printed (no URL, no host, no user name, no password). Partial files are deleted afterwards.
//
//   download_bench.exe <stream_id> <ext> <seconds> <gap seconds> <rcvbufKB>/<curlbufKB> [...]
//   (rcvbufKB 0 = the system default)

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <string>
#include <thread>

#include <curl/curl.h>

#include "../../src/downloads/curl_transport.h"
#include "../../src/downloads/download_manager.h"
#include "../../src/platform/fs.h"

using namespace dl;

namespace {
    double monotonic() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    bool credentials(const std::string &root, Credentials &out) {
        std::string text;
        if (!fs::readFile(fs::join(root, "config/test_streams.txt"), text, 1 << 16)) {
            return false;
        }
        std::smatch m;
        if (!std::regex_search(text, m, std::regex("(https?://[^/\\s]+)/live/([^/\\s]+)/([^/\\s]+)/"))) {
            return false;
        }
        out.profileId = "bench";
        out.name = "bench";
        out.server = m[1].str();
        out.username = m[2].str();
        out.password = m[3].str();
        return true;
    }

    void sleepSeconds(double s) {
        std::this_thread::sleep_for(std::chrono::milliseconds((int) (s * 1000)));
    }
}

int main(int argc, char **argv) {
    if (argc < 6) {
        std::printf("usage: download_bench <repo> <stream_id> <ext> <seconds> <gap> <rcvbufKB>/<curlbufKB>...\n");
        return 2;
    }
    std::string repo = argv[1];
    std::string id = argv[2];
    std::string ext = argv[3];
    double seconds = std::atof(argv[4]);
    double gap = std::atof(argv[5]);
    Credentials cr;
    if (!credentials(repo, cr)) {
        std::printf("no Xtream URL in config/test_streams.txt\n");
        return 2;
    }
    curl_global_init(CURL_GLOBAL_ALL);
    std::string ca = fs::join(repo, "third_party/cacert/cacert.pem");
    CurlTransport transport("PS4IPTV/0.2.0", fs::exists(ca) ? ca : "");
    std::printf("movie %s.%s, %.0f s per configuration, %.0f s between connections\n", id.c_str(), ext.c_str(),
                seconds, gap);
    std::printf("%-9s %-8s %8s %8s %8s %8s %8s %9s %8s %6s %5s %5s %s\n", "rcvbuf", "curlbuf", "MB", "avg", "network",
                "disk", "peak", "cb avg", "cb/s", "first", "syncs", "manif", "socket buffer default -> in use");
    for (int a = 6; a < argc; a++) {
        if (a > 6) {
            sleepSeconds(gap);   // the account allows one connection: let the provider see the last one closed
        }
        int rcv = 0, cbuf = 256;
        if (std::sscanf(argv[a], "%d/%d", &rcv, &cbuf) != 2) {
            std::printf("bad configuration %s\n", argv[a]);
            continue;
        }
        std::string dir = fs::join(repo, "build/host-tests/bench-tmp");
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        fs::ensureDir(dir);
        ManagerConfig c;
        c.root = dir;
        c.transport = &transport;
        c.clock = monotonic;
        c.wallClock = [] { return (int64_t) 1700000000; };
        c.receiveBuffer = rcv * 1024;
        c.transferBuffer = cbuf * 1024;
        c.freeSpace = [](const std::string &) { return (int64_t) 1 << 40; };
        c.buildUrl = [](const Credentials &k, Kind, const std::string &sid, const std::string &e) {
            return k.server + "/movie/" + k.username + "/" + k.password + "/" + sid + "." + e;
        };
        TransferStats s;
        {
            DownloadManager mgr(c);
            std::string warning;
            mgr.load(false, &warning);
            mgr.setProfiles({cr});
            Item it;
            it.kind = Kind::Movie;
            it.profileId = cr.profileId;
            it.contentId = id;
            it.title = "bench " + id;
            it.extension = ext;
            mgr.enqueue(it);
            mgr.startWorker();
            sleepSeconds(seconds);
            s = mgr.transferStats();
            mgr.shutdown();
            mgr.stopWorker();
        }
        std::filesystem::remove_all(dir, ec);
        std::printf("%-9s %-8s %8.1f %8.2f %8.2f %8.1f %8.2f %7.1fKB %8.0f %5.2fs %5d %5d %d KB -> %d KB\n",
                    rcv ? (std::to_string(rcv) + "KB").c_str() : "default", (std::to_string(cbuf) + "KB").c_str(),
                    s.bytes / 1e6, s.average() / 1e6, s.networkSpeed() / 1e6, s.diskSpeed() / 1e6, s.peak / 1e6,
                    s.averageCallback() / 1024, s.callbacksPerSecond(), s.firstByte, s.syncs, s.manifestWrites,
                    s.receiveBufferDefault / 1024, s.receiveBuffer / 1024);
        std::fflush(stdout);
    }
    curl_global_cleanup();
    return 0;
}
