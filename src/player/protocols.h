// Which media URLs the playback backend can open, and what an FFmpeg TLS failure means (host-testable).
//
// The FFmpeg 5.0 build (scripts/build-ffmpeg-tls.sh) has the protocols file, ftp, http, https, rtmp, tcp, tls and
// udp; HTTPS goes through mbedTLS with certificate verification against the packaged CA bundle (mpv options
// tls-verify / tls-ca-file, which mpv 0.34.1 also passes to HLS segment, key and redirect requests). HTTPS is only
// accepted when the linked FFmpeg reports both "https" and "tls" at run time (Playback::init records them), so a
// build without TLS still refuses it with a clear message instead of failing inside FFmpeg.

#ifndef PS4IPTV_PLAYER_PROTOCOLS_H
#define PS4IPTV_PLAYER_PROTOCOLS_H

#include <string>
#include <vector>

namespace protocols {

    // lower-case scheme ("https"); "file" for an absolute local path; "" when there is none
    std::string scheme(const std::string &url);

    // FFmpeg's input protocol names (avio_enum_protocols), recorded once at startup
    void setAvailable(const std::vector<std::string> &names);

    // true once setAvailable() saw both "https" and "tls"
    bool httpsPlayable();

    enum class Verdict {
        Ok,
        HttpsUnavailable,   // https:// with a backend that has no TLS
        Unsupported         // rtsp://, mms://, srt://... or no scheme at all
    };

    // `https` = httpsPlayable() in the app (a parameter for the tests)
    Verdict check(const std::string &url, bool https);

    // upper-case scheme for messages ("RTSP"), "?" when there is none
    std::string shownScheme(const std::string &url);

    // FFmpeg / mbedTLS log line -> TLS failure class (mpv relays them with the "ffmpeg" prefix)
    enum class TlsFailure {
        None,
        Certificate,   // the server certificate could not be verified (untrusted, expired, wrong host name)
        CaBundle,      // the packaged CA bundle could not be read
        Handshake      // no common protocol version / cipher, alert from the server, connection reset
    };

    TlsFailure classifyTls(const std::string &logLine);

    // "scheme://host[:port]" of a URL: the only part of a stream address the playback log names
    std::string origin(const std::string &url);
}

#endif // PS4IPTV_PLAYER_PROTOCOLS_H
