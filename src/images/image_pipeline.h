// One image load as run on a worker thread (host-testable; the HTTP fetch is injected):
//   disk cache (fresh) -> decode
//   otherwise download -> content checks -> decode -> store in the disk cache
//   download failed but a stale cached copy exists -> use the stale copy
// A cached file that does not decode is deleted and downloaded again once. Every failure is classified
// for the scheduler: RetryLater (network) or Permanent (HTTP error status, not an image, undecodable).

#ifndef PS4IPTV_IMAGES_IMAGE_PIPELINE_H
#define PS4IPTV_IMAGES_IMAGE_PIPELINE_H

#include <functional>
#include <string>
#include <vector>

#include "disk_cache.h"
#include "image_decode.h"
#include "image_scheduler.h"

namespace images {

    struct FetchResponse {
        bool networkError = false;   // no HTTP response (DNS, connect, TLS, timeout, canceled)
        long status = 0;
        std::string contentType;
        std::string body;
        std::string detail;          // for the log (never the URL)
    };

    using Fetcher = std::function<FetchResponse(const std::string &normalizedUrl)>;

    struct Box {
        int w;
        int h;
    };

    struct LoadOutcome {
        Scheduler::Result result = Scheduler::Result::Permanent;
        std::vector<Image> variants;   // one per requested box, when result == Ok
        bool fromDisk = false;
        bool staleUsed = false;
        std::string detail;            // failure reason / notes for the log
    };

    LoadOutcome loadImage(const std::string &normalizedUrl, DiskCache &disk, const Fetcher &fetch, int64_t now,
                          const std::vector<Box> &boxes);
}

#endif // PS4IPTV_IMAGES_IMAGE_PIPELINE_H
