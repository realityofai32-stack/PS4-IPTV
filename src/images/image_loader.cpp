#include <algorithm>
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

    // A power-of-two RGBA page with the image in its top-left corner, uploaded once. Same texture type
    // and CPU->GPU path (lock, write the CPU copy, unlock = glTexSubImage2D) as the glyph atlas pages.
    // Afterwards the CPU copy is released: these textures are never locked again (GLTexture's destructor
    // only frees `pixels` when it is set).
    // exact (the large kinds): the texture is exactly the image, clamped to its edges - complete for a
    // non-power-of-two size in core GLES2 (images/art_sizes.h).
    std::shared_ptr<c2d::Texture> makeTexture(const images::Image &img, int64_t &bytes, bool exact,
                                              c2d::Vector2i &pageSize) {
        if (!img.valid()) {
            return nullptr;
        }
        images::art::Page page = images::art::page(img.w, img.h, exact);
        int tw = page.w;
        int th = page.h;
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
        if (exact) {
            glBindTexture(GL_TEXTURE_2D, tex->texID);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        bytes += page.bytes;
        pageSize = {tw, th};
        return std::shared_ptr<c2d::Texture>(tex);
    }

    char kindChar(ImageKind k) {
        return (char) ('0' + (int) k);
    }
}

// Texture pages are powers of two (makeTexture), so a box is chosen by the page it lands in:
//   Poster       170 x 255 -> 256 x 256 page, 256 KiB (as 140 x 210 was): the grid's 166 x 249 tile and the Home
//                card's 170 x 255 poster are drawn at ~1:1 instead of the 1.19x upscale of v2
//   PosterLarge  400 x 600 -> 512 x 1024, 2 MiB: the 320 x 480 movie poster, and the detail pages' poster-derived
//                backdrop
//   Backdrop / HeroPoster: exact-size textures, see images/art_sizes.h (1280 x 720, 3.52 MiB / 512 x 768, 1.5 MiB)
// Every texture counts against the one byte-bounded LRU (MEMORY_BYTES): bigger images mean fewer cached ones,
// never a higher peak.
std::vector<images::Box> ImageLoader::boxes(ImageKind kind) {
    switch (kind) {
        case ImageKind::Poster:
            return {{170, 255}};
        case ImageKind::PosterLarge:
            return {{400, 600}};
        case ImageKind::Backdrop:
            return {images::art::BACKDROP};
        case ImageKind::HeroPoster:
            return {images::art::HERO_POSTER};
        default:
            return {{96, 52}, {340, 190}};
    }
}

int ImageLoader::maxSourceDimension(ImageKind kind) {
    return kind == ImageKind::Backdrop ? images::art::BACKDROP_MAX_SOURCE : images::MAX_DIMENSION;
}

bool ImageLoader::exactPage(ImageKind kind) {
    return kind == ImageKind::Backdrop || kind == ImageKind::HeroPoster;
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

bool ImageLoader::failed(ImageKind kind, const std::string &url) {
    if (!on || url.empty()) {
        return false;
    }
    std::string k = key(kind, url);
    return !k.empty() && scheduler.state(k) == images::Scheduler::State::Failed;
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
    int maxSource = maxSourceDimension(pending->kind);
    jobs.submit(JobPriority::Low, "image", [pending, cache, variantBoxes, maxSource](const CancelToken &token) {
        images::Fetcher fetch = [&token](const std::string &u) {
            http::Request req;
            req.url = u;
            req.connectTimeoutMs = 6000;
            req.totalTimeoutMs = 15000;
            req.stallTimeoutS = 8;
            req.maxBytes = MAX_IMAGE_BYTES;
            req.cancel = token.shared();
            req.credentials = false;   // artwork: an https -> http redirect carries nothing secret
            req.retries = 0;           // images have their own back-off (images::loadImage)
            http::Response r = http::get(req);
            images::FetchResponse f;
            f.status = r.status;
            f.contentType = r.contentType;
            f.body = std::move(r.body);
            f.detail = http::describe(r);
            if (r.error == http::Error::TooLarge) {
                f.status = 413;                // permanent: never retried
            } else if (r.error == http::Error::HttpsUnsupported || r.error == http::Error::Redirect
                       || r.error == http::Error::TlsCertificate) {
                f.status = 0;                  // permanent: not http(s), refused redirect, untrusted certificate
            } else if (r.error != http::Error::None) {
                f.networkError = true;         // retried after a back-off
            }
            return f;
        };
        pending->outcome = images::loadImage(pending->url, *cache, fetch, clockx::unixNow(), variantBoxes, maxSource);
        if ((pending->kind == ImageKind::HeroPoster || pending->kind == ImageKind::PosterLarge
             || pending->kind == ImageKind::Poster) && !pending->outcome.variants.empty()) {
            // the poster's mean colour (64 x 64 samples at most): the tint around it on Home / detail pages
            pending->average = images::averageColor(pending->outcome.variants[0]);
        }
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
        if (p->kind == ImageKind::Backdrop) {
            gen++;   // screens waiting for this backdrop rebind and show their poster fallback
        }
        return;
    }
    auto set = std::make_shared<ImageSet>();
    set->source = {o.sourceW, o.sourceH};
    set->average = p->average;
    int64_t bytes = 0;
    const bool exact = exactPage(p->kind);
    for (const auto &v: o.variants) {
        ImageVariant iv;
        iv.texture = makeTexture(v, bytes, exact, iv.page);
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
    if (p->kind == ImageKind::Backdrop) {
        // never a pile of 3.5 MiB backdrops: the oldest leaves memory (a screen still showing it keeps it alive)
        backdropKeys.erase(std::remove(backdropKeys.begin(), backdropKeys.end(), p->key), backdropKeys.end());
        backdropKeys.push_back(p->key);
        while ((int) backdropKeys.size() > MAX_BACKDROPS) {
            memory.erase(backdropKeys.front());
            scheduler.forget(backdropKeys.front());
            backdropKeys.pop_front();
        }
    }
    scheduler.finished(p->key, images::Scheduler::Result::Ok, now);
    gen++;
    if (exact) {
        // the large art: what the provider sent and what it became, so a soft picture can be traced to its source
        const ImageVariant &v = set->variants[0];
        LOG_I("images", "large image %s kind %d: source %dx%d -> texture %dx%d (page %dx%d exact, %lld KiB)%s",
              hash.c_str(), (int) p->kind, o.sourceW, o.sourceH, v.size.x, v.size.y, v.page.x, v.page.y,
              (long long) (bytes / 1024), o.fromDisk ? " from disk" : "");
    }
    LOG_V("images", "image %s kind %d ready: %s%s (memory %d images, %lld KiB)", hash.c_str(), (int) p->kind,
          o.detail.c_str(), o.fromDisk ? " from disk" : "", (int) memory.size(), (long long) (memory.bytes() / 1024));
}

void ImageLoader::clearCache(std::function<void(int64_t)> done) {
    memory.clear();     // widgets keep what they show until they rebind
    backdropKeys.clear();
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
