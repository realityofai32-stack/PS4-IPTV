// Host-side (Windows, MSVC) tests for the platform-independent parts of the playback test:
// credential redaction and test_streams.txt parsing. Run: scripts/run-host-tests.ps1

#include <cstdio>
#include <string>

#include "../src/redact.h"
#include "../src/test_env.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static bool contains(const std::string &s, const std::string &what) {
    return s.find(what) != std::string::npos;
}

static void testXtreamUrls() {
    const std::string ts = "http://iptv.example.net:8080/live/myUser42/s3cretPass/12345.ts";
    const std::string hls = "http://iptv.example.net:8080/live/myUser42/s3cretPass/12345.m3u8";
    redact::addUrl(ts);
    redact::addUrl(hls);

    std::string r = redact::apply("Mpv::load(" + ts + ") options: pause=yes");
    CHECK(!contains(r, "myUser42"));
    CHECK(!contains(r, "s3cretPass"));
    CHECK(contains(r, "/live/[REDACTED]/[REDACTED]/12345.ts"));
    CHECK(contains(r, "iptv.example.net:8080"));

    // mpv/ffmpeg log styles
    r = redact::apply("[ffmpeg] http: HTTP error 401 Unauthorized for 'http://iptv.example.net:8080/live/myUser42/s3cretPass/12345.ts'");
    CHECK(!contains(r, "myUser42") && !contains(r, "s3cretPass"));
    r = redact::apply("[hls] Opening 'http://edge2.example.org/hls/myUser42/s3cretPass/seg_001.ts' for reading");
    CHECK(!contains(r, "myUser42") && !contains(r, "s3cretPass"));  // other path layout: literal secrets
    r = redact::apply("Playing: http://iptv.example.net:8080/live/myUser42/s3cretPass/12345.m3u8");
    CHECK(contains(r, "/live/[REDACTED]/[REDACTED]/12345.m3u8"));

    // no double masking artefacts
    CHECK(!contains(r, "[REDACTED]]"));
    CHECK(!contains(r, "[[REDACTED]"));

    // unrelated text unchanged
    CHECK(redact::apply("[vd] Using video decoder: h264") == "[vd] Using video decoder: h264");

    // parse
    redact::UrlInfo u = redact::parseUrl(ts);
    CHECK(u.valid);
    CHECK(u.scheme == "http");
    CHECK(u.host == "iptv.example.net");
    CHECK(u.port == "8080");
    CHECK(u.extension == "ts");
    CHECK(u.sanitized == "http://iptv.example.net:8080/live/[REDACTED]/[REDACTED]/12345.ts");
    u = redact::parseUrl(hls);
    CHECK(u.extension == "m3u8");
}

static void testStructuralWithoutRegistration() {
    // credentials never registered (e.g. a redirect target): structural rules still apply
    std::string r = redact::apply("Opening 'http://other.example.com/live/zzUnknownUser/zzUnknownPw/9.ts'");
    CHECK(!contains(r, "zzUnknownUser") && !contains(r, "zzUnknownPw"));
    r = redact::apply("GET http://h.example.com/player_api.php?username=alice77&password=bob88&action=x");
    CHECK(!contains(r, "alice77") && !contains(r, "bob88"));
    CHECK(contains(r, "action=x"));
    r = redact::apply("http://carol99:dave00@h.example.com/stream.ts");
    CHECK(!contains(r, "carol99") && !contains(r, "dave00"));
    CHECK(contains(r, "@h.example.com/stream.ts"));
}

static void testUserInfoAndQueryRegistration() {
    redact::addUrl("http://erin123:frank456@host.example.com/get.php?username=erin123&password=frank456&type=m3u");
    std::string r = redact::apply("token erin123 and frank456 seen in some other message");
    CHECK(!contains(r, "erin123") && !contains(r, "frank456"));
    redact::UrlInfo u = redact::parseUrl("http://erin123:frank456@host.example.com/get.php?username=erin123");
    CHECK(u.valid);
    CHECK(u.host == "host.example.com");
    CHECK(!contains(u.sanitized, "erin123") && !contains(u.sanitized, "frank456"));
}

static void testShortSecretsNotLiteral() {
    // 2-char credentials are not replaced literally (would mangle text) but structurally
    redact::addUrl("http://h.example.com/live/ab/cd/1.ts");
    std::string r = redact::apply("abcd http://h.example.com/live/ab/cd/1.ts");
    CHECK(contains(r, "abcd "));
    CHECK(contains(r, "/live/[REDACTED]/[REDACTED]/1.ts"));
}

static void testConfigParsing() {
    std::string content = "\xEF\xBB\xBF# comment\r\n"
                          "TS=http://iptv.example.net:8080/live/u/p/1.ts\r\n"
                          "  hls = \"http://iptv.example.net:8080/live/u/p/1.m3u8\"  \r\n"
                          "\r\n"
                          "USER_AGENT=VLC/3.0.20\r\n"
                          "MPV_OPT=network-timeout=30\r\n"
                          "MPV_OPT=broken\r\n"
                          "garbage line\r\n"
                          "PASSWORD=should-not-be-echoed\r\n";
    StreamConfig c = parseStreamConfig(content, "/mnt/usb0/test_streams.txt");
    CHECK(c.found);
    CHECK(c.tsUrl == "http://iptv.example.net:8080/live/u/p/1.ts");
    CHECK(c.hlsUrl == "http://iptv.example.net:8080/live/u/p/1.m3u8");
    CHECK(c.userAgent == "VLC/3.0.20");
    CHECK(c.mpvOptions.size() == 1);
    CHECK(c.mpvOptions.size() == 1 && c.mpvOptions[0].first == "network-timeout"
          && c.mpvOptions[0].second == "30");
    CHECK(c.problems.size() == 3);  // broken MPV_OPT, line without '=', unknown key
    bool echoed = false;
    for (const auto &p: c.problems) {
        echoed |= contains(p, "should-not-be-echoed");
    }
    CHECK(!echoed);

    StreamConfig placeholder = parseStreamConfig("TS=<authenticated TS URL>\nHLS=\n", "x");
    bool flagged = false;
    for (const auto &p: placeholder.problems) {
        flagged |= contains(p, "placeholder");
    }
    CHECK(flagged);
    CHECK(placeholder.hlsUrl.empty());
}

int main() {
    testXtreamUrls();
    testStructuralWithoutRegistration();
    testUserInfoAndQueryRegistration();
    testShortSecretsNotLiteral();
    testConfigParsing();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
