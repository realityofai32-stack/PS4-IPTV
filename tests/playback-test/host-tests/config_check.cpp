// Host-side check of a test_streams.txt with the exact parser and redaction code the PS4 app uses
// (stream_config.cpp, redact.cpp). Prints only sanitized information.
// Usage: config_check <path to test_streams.txt>   (exit code 0 = TS and HLS READY)

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "../src/redact.h"
#include "../src/test_env.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        std::printf("usage: config_check <test_streams.txt>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::printf("cannot open %s\n", argv[1]);
        return 2;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    StreamConfig cfg = parseStreamConfig(ss.str(), argv[1]);

    // same secret registration the app does in refreshEnvironment()
    redact::addUrl(cfg.tsUrl);
    redact::addUrl(cfg.hlsUrl);

    int ready = 0;
    for (int i = 0; i < 2; i++) {
        const std::string &url = i == 0 ? cfg.tsUrl : cfg.hlsUrl;
        redact::UrlInfo u = redact::parseUrl(url);
        bool ok = !url.empty() && u.valid && u.scheme == "http";
        ready += ok;
        std::printf("%-4s %-9s %s%s\n", i == 0 ? "TS" : "HLS", ok ? "READY" : "NOT READY",
                    u.valid ? u.sanitized.c_str() : "(missing/invalid)",
                    u.valid && u.scheme != "http" ? "   (only plain http is supported by the PS4 FFmpeg)" : "");
        // the sanitized form must not contain anything the redactor considers secret
        if (u.valid && redact::apply(u.sanitized) != u.sanitized) {
            std::printf("     !! sanitized URL still contains a registered secret\n");
            ready = -10;
        }
    }
    for (const auto &p: cfg.problems) {
        std::printf("problem: %s\n", p.c_str());
    }
    return ready == 2 && cfg.problems.empty() ? 0 : 1;
}
