// HTTPS playback: protocol acceptance (with / without TLS in the backend), plain HTTP unchanged, TLS failure
// classification of FFmpeg's mbedTLS log lines, and log-safe stream origins.

#include "check.h"
#include "../../src/player/protocols.h"
#include "../../src/platform/redact.h"

using protocols::Verdict;

TEST(https_scheme_parsing) {
    CHECK_EQ(protocols::scheme("https://cdn.example.com/a.m3u8"), std::string("https"));
    CHECK_EQ(protocols::scheme("HTTPS://cdn.example.com/a.m3u8"), std::string("https"));
    CHECK_EQ(protocols::scheme("http://h:8080/live/u/p/1.ts"), std::string("http"));
    CHECK_EQ(protocols::scheme("/data/PS4IPTV/downloads/movie.mkv"), std::string("file"));
    CHECK_EQ(protocols::scheme("rtsp://cam.example.com/s"), std::string("rtsp"));
    CHECK_EQ(protocols::scheme("not a url"), std::string(""));
    CHECK_EQ(protocols::scheme("://x"), std::string(""));
    CHECK_EQ(protocols::shownScheme("srt://x"), std::string("SRT"));
    CHECK_EQ(protocols::shownScheme("garbage"), std::string("?"));
}

TEST(https_accepted_only_with_tls_in_the_backend) {
    // the rebuilt FFmpeg (https + tls protocols): accepted
    CHECK(protocols::check("https://cdn.example.com/live/stream.m3u8", true) == Verdict::Ok);
    CHECK(protocols::check("https://cdn.example.com/movie.mp4", true) == Verdict::Ok);
    // a backend without TLS refuses it with its own verdict (clear message, no FFmpeg error)
    CHECK(protocols::check("https://cdn.example.com/live/stream.m3u8", false) == Verdict::HttpsUnavailable);
    // the run-time capability comes from FFmpeg's protocol list: both https and tls are needed
    protocols::setAvailable({"file", "http", "tcp"});
    CHECK(!protocols::httpsPlayable());
    protocols::setAvailable({"file", "http", "https", "tcp"});
    CHECK(!protocols::httpsPlayable());
    protocols::setAvailable({"file", "ftp", "http", "https", "rtmp", "tcp", "tls", "udp"});
    CHECK(protocols::httpsPlayable());
}

TEST(https_http_and_other_protocols_unchanged) {
    for (bool tls: {false, true}) {
        CHECK(protocols::check("http://h.example.com:8080/live/u/p/1.ts", tls) == Verdict::Ok);
        CHECK(protocols::check("http://h.example.com/a.m3u8", tls) == Verdict::Ok);
        CHECK(protocols::check("rtmp://h.example.com/app/stream", tls) == Verdict::Ok);
        CHECK(protocols::check("udp://239.0.0.1:1234", tls) == Verdict::Ok);
        CHECK(protocols::check("/data/PS4IPTV/downloads/movie.mkv", tls) == Verdict::Ok);
        CHECK(protocols::check("rtsp://cam.example.com/s", tls) == Verdict::Unsupported);
        CHECK(protocols::check("mms://h.example.com/s", tls) == Verdict::Unsupported);
        CHECK(protocols::check("relative/path.mkv", tls) == Verdict::Unsupported);
    }
}

TEST(https_tls_failure_classification) {
    using protocols::TlsFailure;
    // FFmpeg 5.0 tls_mbedtls.c texts as mpv relays them
    CHECK(protocols::classifyTls("tls: mbedtls_ssl_handshake returned -0x2700") == TlsFailure::Certificate);
    CHECK(protocols::classifyTls("mbedtls_ssl_get_verify_result reported problems with the certificate.")
          == TlsFailure::Certificate);
    CHECK(protocols::classifyTls("The certificate is not correctly signed by the trusted CA.") == TlsFailure::Certificate);
    CHECK(protocols::classifyTls("mbedtls_x509_crt_parse_file for CA cert returned -15616") == TlsFailure::CaBundle);
    CHECK(protocols::classifyTls("tls: mbedtls_ssl_handshake returned -0x7780") == TlsFailure::Handshake);
    CHECK(protocols::classifyTls("A fatal alert message was received from the peer, has the peer a correct certificate?")
          == TlsFailure::Handshake);
    CHECK(protocols::classifyTls("TLS handshake was aborted by peer.") == TlsFailure::Handshake);
    CHECK(protocols::classifyTls("HTTP error 404 Not Found") == TlsFailure::None);
    CHECK(protocols::classifyTls("Failed to resolve hostname cdn.example.com") == TlsFailure::None);
}

TEST(https_log_origin_hides_path_query_and_credentials) {
    CHECK_EQ(protocols::origin("https://cdn.example.com:8443/live/user1/pass1/77.m3u8?token=abc"),
             std::string("https://cdn.example.com:8443"));
    CHECK_EQ(protocols::origin("https://user:secret@cdn.example.com/x.ts"), std::string("https://cdn.example.com"));
    CHECK_EQ(protocols::origin("http://h.example.com"), std::string("http://h.example.com"));
    CHECK_EQ(protocols::origin("/data/a.mkv"), std::string("file"));
    // a full HTTPS URL that reaches the log anyway is masked like an HTTP one
    std::string line = redact::apply("Opening 'https://cdn.example.com/live/someuser/somepass/1.ts?token=t0k3n' for reading");
    CHECK(line.find("someuser") == std::string::npos && line.find("somepass") == std::string::npos);
    CHECK(line.find("t0k3n") == std::string::npos);
}
