// HTTP(S) client for the Xtream API and images (libcurl 7.80 + mbedTLS from PacBrew, the same library
// pPlay links). Not used for playback: streams are opened by mpv/FFmpeg exactly as in pPlay.
// Blocking: call only from job worker threads.

#ifndef PS4IPTV_NETWORK_HTTP_H
#define PS4IPTV_NETWORK_HTTP_H

#include <atomic>
#include <memory>
#include <string>

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
    };

    enum class Error {
        None,
        Canceled,
        Timeout,
        Dns,
        Connect,
        Tls,
        TooLarge,
        HttpsUnsupported,
        Other
    };

    struct Response {
        Error error = Error::None;
        long status = 0;
        std::string body;
        std::string contentType;
        std::string detail;            // curl error text (redacted when logged)
        double seconds = 0;
        bool redirectedToHttps = false;

        bool ok() const { return error == Error::None && status >= 200 && status < 300; }
    };

    // Once at startup, on the main thread. caBundle: path of the PEM CA bundle (may be empty).
    bool globalInit(const std::string &caBundle, const std::string &userAgent);

    void globalShutdown();

    // set by globalInit (the download transport uses the same identity and CA bundle)
    const std::string &userAgent();

    const std::string &caBundle();

    Response get(const Request &request);

    // Short user-facing text for a failed response.
    std::string describe(const Response &response);
}

#endif // PS4IPTV_NETWORK_HTTP_H
