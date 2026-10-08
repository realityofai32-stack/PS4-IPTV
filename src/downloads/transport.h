// HTTP transfer interface of the download engine: libcurl in the app and the integration test
// (curl_transport.cpp), scripted fakes in the unit tests.

#ifndef PS4IPTV_DOWNLOADS_TRANSPORT_H
#define PS4IPTV_DOWNLOADS_TRANSPORT_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "download_model.h"

namespace dl {

    struct TransferRequest {
        std::string url;                // authenticated URL: never logged, never stored
        int64_t offset = 0;             // > 0: "Range: bytes=<offset>-"
        std::string ifRange;            // ETag or Last-Modified of the partial file ("" = none)
        long connectTimeoutMs = 15000;
        long stallTimeoutS = 30;        // no data for this long: a network error (resumable)
        std::shared_ptr<std::atomic<bool>> cancel;
    };

    class TransferSink {
    public:
        virtual ~TransferSink() = default;

        // the final response's status and headers, once, before the first body byte (redirects already
        // followed). false aborts the transfer.
        virtual bool onHead(const ResponseHead &head) = 0;

        // body bytes of a 2xx response; false aborts the transfer (disk error, pause...)
        virtual bool onData(const char *data, size_t n) = 0;
    };

    struct TransferResult {
        enum class Outcome {
            Complete,       // the server sent the whole body
            HttpError,      // the final status is >= 400 (no body written); see status
            Network,        // DNS, connect, timeout, stall, connection lost mid-body (resumable)
            Aborted,        // the sink or the cancel flag stopped it
            Other
        };
        Outcome outcome = Outcome::Other;
        long status = 0;
        std::string detail;             // diagnostic text (no URL, no credentials)
    };

    class Transport {
    public:
        virtual ~Transport() = default;

        virtual TransferResult run(const TransferRequest &request, TransferSink &sink) = 0;
    };
}

#endif // PS4IPTV_DOWNLOADS_TRANSPORT_H
