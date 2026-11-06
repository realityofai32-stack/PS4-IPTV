// What the HTTP client may follow and retry (host-testable; network/http.cpp applies it).
//
// Schemes: only http:// and https://. The PS4 libcurl (PacBrew 7.80) also speaks file, dict, gopher, ftp, imap,
// pop3, smtp, telnet, tftp, mqtt, rtsp, ldap and smb; addresses come from providers (artwork in an M3U list or an
// API answer, redirects), so without this a hostile list could read console files or send raw bytes to devices
// on the local network.
//
// Redirects (followed one by one here, not by libcurl, so each hop is checked):
//   - never to another scheme than http / https
//   - never from https to http for a request that carries credentials (API calls, playlist addresses, portal
//     calls): a provider that sends the client back to plain HTTP must be entered as http:// explicitly
//   - requests with their own header lines (a portal's MAC cookie and Bearer token) stay on the original host
//     ("www." in front of it or not)
//   - at most MAX_REDIRECTS hops (loops end there)
//
// Retries: GET requests only, at most MAX_RETRIES more attempts, only for failures that are usually temporary and
// were quick to happen (connection refused / reset / interrupted, DNS lookup failure, HTTP 502 / 503 / 504), with
// growing pauses (backoffSeconds), never after the caller canceled, never once half of the request's time budget is
// used, never for timeouts (a dead or stalled server is not asked again), HTTP 4xx, TLS errors or answers that were
// too large.

#ifndef PS4IPTV_NETWORK_HTTP_POLICY_H
#define PS4IPTV_NETWORK_HTTP_POLICY_H

#include <string>

namespace httppolicy {

    const int MAX_REDIRECTS = 8;
    const int MAX_RETRIES = 2;

    // pause before retry number `attempt` (1, 2, ...): 0.75 s, 2 s
    double backoffSeconds(int attempt);

    // "http" or "https" (any letter case) followed by "://"
    bool schemeAllowed(const std::string &url);

    struct Origin {
        std::string scheme;   // lower case
        std::string host;     // lower case, without user information and port
        int port = 0;         // explicit or the scheme's default
    };

    bool parseOrigin(const std::string &url, Origin &out);

    enum class Redirect {
        Follow,
        BadScheme,       // not http / https
        Downgrade,       // https -> http for a request with credentials
        OtherHost,       // a request with its own headers leaving its host
        Invalid          // no usable address
    };

    Redirect checkRedirect(const std::string &from, const std::string &to, bool credentials, bool sameHostOnly);

    // the failure kinds the retry decision needs (mirrors http::Error)
    enum class Failure {
        None,            // the transfer worked; `status` decides
        Canceled,
        Timeout,
        Dns,
        Connect,
        Interrupted,     // reset / closed / partial / nothing received
        Tls,
        TlsCertificate,
        TooLarge,
        Redirect,
        Other
    };

    bool shouldRetry(Failure failure, long status, double elapsedSeconds, long totalTimeoutMs, int retriesDone,
                     int maxRetries, bool canceled);
}

#endif // PS4IPTV_NETWORK_HTTP_POLICY_H
