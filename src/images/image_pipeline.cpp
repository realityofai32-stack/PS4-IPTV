#include "image_pipeline.h"

namespace images {

    namespace {
        bool decodeVariants(const std::string &bytes, const std::vector<Box> &boxes, LoadOutcome &out) {
            Image img;
            std::string err;
            if (!decode(bytes, img, &err)) {
                out.detail = err;
                return false;
            }
            out.variants.clear();
            for (const Box &b: boxes) {
                out.variants.push_back(prepare(img, b.w, b.h));
            }
            out.detail = std::to_string(img.w) + "x" + std::to_string(img.h) + " " + formatName(sniff(bytes));
            return true;
        }
    }

    LoadOutcome loadImage(const std::string &url, DiskCache &disk, const Fetcher &fetch, int64_t now,
                          const std::vector<Box> &boxes) {
        LoadOutcome out;
        std::string cached;
        DiskCache::Lookup lookup = disk.get(url, now, cached);
        if (lookup == DiskCache::Lookup::Fresh) {
            if (decodeVariants(cached, boxes, out)) {
                out.result = Scheduler::Result::Ok;
                out.fromDisk = true;
                return out;
            }
            disk.remove(url);   // stored bytes no longer decode: fetch them again
            cached.clear();
        }

        FetchResponse r = fetch(url);
        std::string failure;
        Scheduler::Result failResult = Scheduler::Result::Permanent;
        if (r.networkError) {
            failure = "network: " + r.detail;
            failResult = Scheduler::Result::RetryLater;
        } else if (r.status != 200) {
            failure = "HTTP " + std::to_string(r.status);
            // 5xx / 429 are worth another try later; other statuses are not
            failResult = r.status >= 500 || r.status == 429 || r.status == 408 ? Scheduler::Result::RetryLater
                                                                                : Scheduler::Result::Permanent;
        } else if (r.body.empty()) {
            failure = "empty response";
        } else if (!acceptableContentType(r.contentType)) {
            failure = "not an image (Content-Type " + r.contentType + ")";
        } else if (decodeVariants(r.body, boxes, out)) {
            std::string err;
            if (!disk.put(url, r.body, now, &err)) {
                out.detail += " (not cached: " + err + ")";
            }
            out.result = Scheduler::Result::Ok;
            return out;
        } else {
            failure = out.detail;   // decode error
        }

        if (lookup == DiskCache::Lookup::Stale && !cached.empty() && decodeVariants(cached, boxes, out)) {
            out.result = Scheduler::Result::Ok;
            out.fromDisk = true;
            out.staleUsed = true;
            out.detail += " (stale copy; refresh failed: " + failure + ")";
            return out;
        }
        out.variants.clear();
        out.result = failResult;
        out.detail = failure;
        return out;
    }
}
