// libcurl transport for downloads: the same libcurl 7.80 + mbedTLS the app uses for the Xtream API (not the
// playback FFmpeg network layer), so HTTPS, redirects, Range requests, progress and cancellation work the way
// curl provides them. One easy handle per transfer; curl_global_init must have run (http::globalInit).
//
// Options used (all present in libcurl 7.80.0):
//   CURLOPT_FOLLOWLOCATION / MAXREDIRS 8      redirects (providers often redirect /movie/ to a CDN host)
//   CURLOPT_RANGE "<offset>-"                 resume; the response is validated by dl::planResponse
//                                             (206 + Content-Range at the offset, or 200 = whole file)
//   CURLOPT_HTTPHEADER "If-Range: <etag>"     a changed file comes back as 200, never appended
//   CURLOPT_LOW_SPEED_LIMIT 1 / _TIME         a stalled connection ends as a (resumable) network error
//   CURLOPT_CONNECTTIMEOUT_MS                 no total timeout: files are several GB
//   CURLOPT_HEADERFUNCTION                    status, Content-Length, Content-Range, ETag, Last-Modified of
//                                             the final response (reset on each new status line)
//   CURLOPT_XFERINFOFUNCTION                  cancellation (pause / cancel / playback started)
//   CURLOPT_CAINFO                            the packaged CA bundle for HTTPS
// No Accept-Encoding: media is stored byte for byte and Content-Length must describe the file.

#ifndef PS4IPTV_DOWNLOADS_CURL_TRANSPORT_H
#define PS4IPTV_DOWNLOADS_CURL_TRANSPORT_H

#include "transport.h"

namespace dl {

    class CurlTransport : public Transport {
    public:
        CurlTransport(std::string userAgent, std::string caBundle);

        TransferResult run(const TransferRequest &request, TransferSink &sink) override;

    private:
        std::string userAgent;
        std::string caBundle;
    };
}

#endif // PS4IPTV_DOWNLOADS_CURL_TRANSPORT_H
