// HTTP(S) client for the Xtream / Stalker APIs, M3U playlists and images (libcurl 7.80 + mbedTLS from PacBrew, the
// same library pPlay links). Not used for playback: streams are opened by mpv/FFmpeg exactly as in pPlay.
// Blocking: call only from job worker threads. Schemes, redirects and retries follow network/http_policy.h;
// certificates are always verified against the packaged CA bundle.

#ifndef PS4IPTV_NETWORK_HTTP_H
#define PS4IPTV_NETWORK_HTTP_H

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace http {

    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    struct Request {
        std::string url;
        long connectTimeoutMs = 8000;
        long totalTimeoutMs = 30000;
        long stallTimeoutS = 15;        // abort if no data for this long
        size_t maxBytes = 64u * 1024 * 1024;
        CancelFlag cancel;
        std::string userAgent;          // "" = the app's (globalInit)
        // extra request header lines ("Name: value"), e.g. a portal's Cookie / Authorization. Never logged.
        // A request with headers never follows a redirect to another host (network/http_policy.h).
        std::vector<std::string> headers;
        // the address or the headers carry credentials (API calls, playlist addresses, portal calls): an https
        // address is never followed to plain http. Artwork sets false.
        bool credentials = true;
        // extra attempts after a quick temporary failure (http_policy.h: connection refused / reset / interrupted,
        // DNS, HTTP 502 / 503 / 504), with pauses; 0 = one attempt
        int retries = 2;
    };

    enum class Error {
        None,
        Canceled,
        Timeout,
        Dns,
        Connect,
        Tls,                // TLS handshake failed
        TlsCertificate,     // the server certificate could not be verified (never accepted anyway)
        Interrupted,        // connection reset / closed early / partial answer
        Redirect,           // a redirect the policy refuses (another scheme, https -> http, another host)
        TooLarge,
        HttpsUnsupported,   // a scheme other than http / https (the name is historical)
        Other
    };

    struct Response {
        Error error = Error::None;
        long status = 0;
        std::string body;
        std::string contentType;
        std::string detail;            // curl error text (redacted when logged)
        double seconds = 0;              // all attempts and pauses
        bool redirectedToHttps = false;
        bool downgradeRefused = false;   // Error::Redirect because https would have become http
        int attempts = 0;

        bool ok() const { return error == Error::None && status >= 200 && status < 300; }
    };

    // Once at startup, on the main thread. caBundle: path of the PEM CA bundle (may be empty).
    bool globalInit(const std::string &caBundle, const std::string &userAgent);

    void globalShutdown();

    // frees this thread's curl handle (and its connections): call on a worker thread before it ends
    void releaseThreadResources();

    // set by globalInit (the download transport uses the same identity and CA bundle)
    const std::string &userAgent();

    const std::string &caBundle();

    Response get(const Request &request);

    // Short user-facing text for a failed response.
    std::string describe(const Response &response);
}

#endif // PS4IPTV_NETWORK_HTTP_H
