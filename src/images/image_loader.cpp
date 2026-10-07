#include <cstring>

#include "image_loader.h"
#include "image_key.h"
#include "../network/http.h"
#include "../platform/clock.h"
#include "../platform/log.h"

const images::Box ImageLoader::ROW_BOX = {96, 52};
const images::Box ImageLoader::DETAIL_BOX = {340, 190};

namespace {
    const int MAX_UPLOADS_PER_FRAME = 2;         // texture creation is cheap, but never a burst per frame
    const size_t MEMORY_ENTRIES = 160;
    const int64_t MEMORY_BYTES = 24ll * 1024 * 1024;
    const size_t MAX_IMAGE_BYTES = 2u * 1024 * 1024;

    int pow2(int v) {
        int p = 8;
        while (p < v) {
            p <<= 1;
        }
        return p;
    }

    // A power-of-two RGBA page with the image in its top-left corner, uploaded once. Same texture type
    // and CPU->GPU path (lock, write the CPU copy, unlock = glTexSubImage2D) as the glyph atlas pages.
    std::shared_ptr<c2d::Texture> makeTexture(const images::Image &img, int64_t &bytes) {
        if (!img.valid()) {
            return nullptr;
        }
        int tw = pow2(img.w);
        int th = pow2(img.h);
        auto *tex = new c2d::C2DTexture(c2d::Vector2f((float) tw, (float) th), c2d::Texture::Format::RGBA8);
        void *pixels = nullptr;
        int pitch = 0;
        if (!tex->available || tex->lock(nullptr, &pixels, &pitch) != 0 || pixels == nullptr || pitch < tw * 4) {
            delete tex;
            return nullptr;
        }
        auto *dst = (uint8_t *) pixels;
        memset(dst, 0, (size_t) pitch * (size_t) th);
        for (int y = 0; y < img.h; y++) {
            memcpy(dst + (size_t) y * (size_t) pitch, &img.rgba[(size_t) y * (size_t) img.w * 4], (size_t) img.w * 4);
        }
        tex->unlock();
        bytes += (int64_t) tw * th * 4 * 2;   // GPU texture + libcross2d's CPU copy
        return std::shared_ptr<c2d::Texture>(tex);
    }
}

ImageLoader::ImageLoader(JobSystem &j, const std::string &cacheDir)
        : jobs(j), memory(MEMORY_ENTRIES, MEMORY_BYTES) {
    images::DiskCache::Config cfg;
    cfg.dir = cacheDir;
    cfg.maxFileBytes = MAX_IMAGE_BYTES;
    disk = std::make_shared<images::DiskCache>(cfg);
}

ImageLoader::~ImageLoader() {
    *alive = false;
}

void ImageLoader::setEnabled(bool enabled) {
    if (enabled != on) {
        on = enabled;
        if (!on) {
            scheduler.want({}, now);
        }
        gen++;
    }
}

const std::string &ImageLoader::normalized(const std::string &raw) {
    auto it = normalizedUrls.find(raw);
    if (it != normalizedUrls.end()) {
        return it->second;
    }
    if (normalizedUrls.size() > 8192) {
        normalizedUrls.clear();
    }
    return normalizedUrls.emplace(raw, images::normalizeUrl(raw)).first->second;
}

void ImageLoader::want(const std::vector<std::string> &urls) {
    std::vector<std::string> list;
    if (on) {
        list.reserve(urls.size());
        for (const auto &u: urls) {
            if (!u.empty()) {
                list.push_back(normalized(u));
            }
        }
    }
    scheduler.want(list, now);
}

std::shared_ptr<LogoImages> ImageLoader::get(const std::string &url) {
    if (!on || url.empty()) {
        return nullptr;
    }
    const std::string &u = normalized(url);
    if (u.empty()) {
        return nullptr;
    }
    std::shared_ptr<LogoImages> *v = memory.get(u);
    return v ? *v : nullptr;
}

bool ImageLoader::update(double t) {
    now = t;
    if (on) {
        for (std::string url = scheduler.next(); !url.empty(); url = scheduler.next()) {
            start(url);
        }
    }
    bool changed = false;
    for (int i = 0; i < MAX_UPLOADS_PER_FRAME && !decoded.empty(); i++) {
        std::shared_ptr<Pending> p = decoded.front();
        decoded.pop_front();
        finish(p);
        changed = true;
    }
    return changed;
}

void ImageLoader::start(const std::string &url) {
    auto pending = std::make_shared<Pending>();
    pending->url = url;
    std::shared_ptr<images::DiskCache> cache = disk;
    std::weak_ptr<bool> weak = alive;
    jobs.submit(JobPriority::Low, "logo", [pending, cache](const CancelToken &token) {
        images::Fetcher fetch = [&token](const std::string &u) {
            http::Request req;
            req.url = u;
            req.connectTimeoutMs = 6000;
            req.totalTimeoutMs = 15000;
            req.stallTimeoutS = 8;
            req.maxBytes = MAX_IMAGE_BYTES;
            req.cancel = token.shared();
            http::Response r = http::get(req);
            images::FetchResponse f;
            f.status = r.status;
            f.contentType = r.contentType;
            f.body = std::move(r.body);
            f.detail = http::describe(r);
            if (r.error == http::Error::TooLarge) {
                f.status = 413;                // permanent: never retried
            } else if (r.error == http::Error::HttpsUnsupported) {
                f.status = 0;                  // permanent
            } else if (r.error != http::Error::None) {
                f.networkError = true;         // retried after a back-off
            }
            return f;
        };
        pending->outcome = images::loadImage(pending->url, *cache, fetch, clockx::unixNow(), {ROW_BOX, DETAIL_BOX});
    }, [this, weak, pending]() {
        if (weak.lock()) {
            decoded.push_back(pending);
        }
    });
}

void ImageLoader::finish(const std::shared_ptr<Pending> &p) {
    const images::LoadOutcome &o = p->outcome;
    std::string key = images::cacheKey(p->url);   // logged instead of the URL
    if (o.result != images::Scheduler::Result::Ok || o.variants.size() != 2) {
        scheduler.finished(p->url, o.result == images::Scheduler::Result::Ok ? images::Scheduler::Result::Permanent
                                                                             : o.result, now);
        failed++;
        LOG_I("images", "logo %s unavailable (%s): %s", key.c_str(),
              o.result == images::Scheduler::Result::RetryLater ? "retry later" : "placeholder", o.detail.c_str());
        return;
    }
    auto logo = std::make_shared<LogoImages>();
    int64_t bytes = 0;
    logo->small = makeTexture(o.variants[0], bytes);
    logo->smallSize = {o.variants[0].w, o.variants[0].h};
    logo->large = makeTexture(o.variants[1], bytes);
    logo->largeSize = {o.variants[1].w, o.variants[1].h};
    if (!logo->small || !logo->large) {
        scheduler.finished(p->url, images::Scheduler::Result::RetryLater, now);
        failed++;
        LOG_W("images", "logo %s: texture creation failed", key.c_str());
        return;
    }
    for (const auto &evicted: memory.put(p->url, logo, bytes)) {
        scheduler.forget(evicted);
    }
    scheduler.finished(p->url, images::Scheduler::Result::Ok, now);
    loaded++;
    gen++;
    LOG_V("images", "logo %s ready: %s%s (memory %d logos, %lld KiB)", key.c_str(), o.detail.c_str(),
          o.fromDisk ? " from disk" : "", (int) memory.size(), (long long) (memory.bytes() / 1024));
}

void ImageLoader::clearCache(std::function<void(int64_t)> done) {
    memory.clear();     // widgets keep what they show until they rebind
    scheduler.reset();
    gen++;
    std::shared_ptr<images::DiskCache> cache = disk;
    auto freed = std::make_shared<int64_t>(0);
    std::weak_ptr<bool> weak = alive;
    jobs.submit(JobPriority::High, "clear-images", [cache, freed](const CancelToken &) {
        *freed = cache->clear();
    }, [this, weak, freed, done]() {
        if (weak.lock()) {
            diskUsage = 0;
            LOG_I("images", "image cache cleared: %lld KiB freed", (long long) (*freed / 1024));
            if (done) {
                done(*freed);
            }
        }
    });
}

void ImageLoader::refreshDiskUsage(std::function<void()> done) {
    std::shared_ptr<images::DiskCache> cache = disk;
    auto bytes = std::make_shared<int64_t>(0);
    std::weak_ptr<bool> weak = alive;
    jobs.submit(JobPriority::Low, "image-cache-size", [cache, bytes](const CancelToken &) {
        *bytes = cache->totalBytes();
    }, [this, weak, bytes, done]() {
        if (weak.lock()) {
            diskUsage = *bytes;
            if (done) {
                done();
            }
        }
    });
}
