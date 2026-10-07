#include <cstdlib>
#include <cstring>

#include "image_loader.h"
#include "image_key.h"
#include "../network/http.h"
#include "../platform/clock.h"
#include "../platform/log.h"

namespace {
    const int MAX_UPLOADS_PER_FRAME = 2;         // texture creation is cheap, but never a burst per frame
    const size_t MEMORY_ENTRIES = 320;
    const int64_t MEMORY_BYTES = 48ll * 1024 * 1024;   // GPU bytes of all cached textures
    const size_t MAX_IMAGE_BYTES = 3u * 1024 * 1024;

    int pow2(int v) {
        int p = 8;
        while (p < v) {
            p <<= 1;
        }
        return p;
    }

    // A power-of-two RGBA page with the image in its top-left corner, uploaded once. Same texture type
    // and CPU->GPU path (lock, write the CPU copy, unlock = glTexSubImage2D) as the glyph atlas pages.
    // Afterwards the CPU copy is released: these textures are never locked again (GLTexture's destructor
    // only frees `pixels` when it is set).
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
        free(tex->pixels);
        tex->pixels = nullptr;
        bytes += (int64_t) tw * th * 4;
        return std::shared_ptr<c2d::Texture>(tex);
    }

    char kindChar(ImageKind k) {
        return (char) ('0' + (int) k);
    }
}

std::vector<images::Box> ImageLoader::boxes(ImageKind kind) {
    switch (kind) {
        case ImageKind::Poster:
            return {{140, 210}};
        case ImageKind::PosterLarge:
            return {{340, 510}};
        default:
            return {{96, 52}, {340, 190}};
    }
}

ImageLoader::ImageLoader(JobSystem &j, const std::string &cacheDir)
        : jobs(j), memory(MEMORY_ENTRIES, MEMORY_BYTES) {
    images::DiskCache::Config cfg;
    cfg.dir = cacheDir;
    cfg.maxBytes = 96ll * 1024 * 1024;     // posters are larger than logos
    cfg.trimTo = 80ll * 1024 * 1024;
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

std::string ImageLoader::key(ImageKind kind, const std::string &raw) {
    const std::string &u = normalized(raw);
    return u.empty() ? std::string() : kindChar(kind) + u;
}

void ImageLoader::want(const std::vector<ImageRequest> &requests) {
    std::vector<std::string> list;
    if (on) {
        list.reserve(requests.size());
        for (const auto &r: requests) {
            if (!r.url.empty()) {
                list.push_back(key(r.kind, r.url));
            }
        }
    }
    scheduler.want(list, now);
}

void ImageLoader::want(const std::vector<std::string> &logoUrls) {
    std::vector<ImageRequest> requests;
    requests.reserve(logoUrls.size());
    for (const auto &u: logoUrls) {
        requests.push_back({ImageKind::Logo, u});
    }
    want(requests);
}

std::shared_ptr<ImageSet> ImageLoader::get(ImageKind kind, const std::string &url) {
    if (!on || url.empty()) {
        return nullptr;
    }
    std::string k = key(kind, url);
    if (k.empty()) {
        return nullptr;
    }
    std::shared_ptr<ImageSet> *v = memory.get(k);
    return v ? *v : nullptr;
}

bool ImageLoader::update(double t) {
    now = t;
    if (on) {
        for (std::string k = scheduler.next(); !k.empty(); k = scheduler.next()) {
            start(k);
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

void ImageLoader::start(const std::string &k) {
    auto pending = std::make_shared<Pending>();
    pending->key = k;
    pending->kind = (ImageKind) (k[0] - '0');
    pending->url = k.substr(1);
    std::shared_ptr<images::DiskCache> cache = disk;
    std::weak_ptr<bool> weak = alive;
    std::vector<images::Box> variantBoxes = boxes(pending->kind);
    jobs.submit(JobPriority::Low, "image", [pending, cache, variantBoxes](const CancelToken &token) {
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
        pending->outcome = images::loadImage(pending->url, *cache, fetch, clockx::unixNow(), variantBoxes);
    }, [this, weak, pending]() {
        if (weak.lock()) {
            decoded.push_back(pending);
        }
    });
}

void ImageLoader::finish(const std::shared_ptr<Pending> &p) {
    const images::LoadOutcome &o = p->outcome;
    std::string hash = images::cacheKey(p->url);   // logged instead of the URL
    if (o.result != images::Scheduler::Result::Ok || o.variants.empty()) {
        scheduler.finished(p->key, o.result == images::Scheduler::Result::Ok ? images::Scheduler::Result::Permanent
                                                                             : o.result, now);
        LOG_I("images", "image %s unavailable (%s): %s", hash.c_str(),
              o.result == images::Scheduler::Result::RetryLater ? "retry later" : "placeholder", o.detail.c_str());
        return;
    }
    auto set = std::make_shared<ImageSet>();
    int64_t bytes = 0;
    for (const auto &v: o.variants) {
        ImageVariant iv;
        iv.texture = makeTexture(v, bytes);
        iv.size = {v.w, v.h};
        if (!iv.texture) {
            scheduler.finished(p->key, images::Scheduler::Result::RetryLater, now);
            LOG_W("images", "image %s: texture creation failed", hash.c_str());
            return;
        }
        set->variants.push_back(iv);
    }
    for (const auto &evicted: memory.put(p->key, set, bytes)) {
        scheduler.forget(evicted);
    }
    scheduler.finished(p->key, images::Scheduler::Result::Ok, now);
    gen++;
    LOG_V("images", "image %s kind %d ready: %s%s (memory %d images, %lld KiB)", hash.c_str(), (int) p->kind,
          o.detail.c_str(), o.fromDisk ? " from disk" : "", (int) memory.size(), (long long) (memory.bytes() / 1024));
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
