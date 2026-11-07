// Image service for channel logos, movie posters and series covers (main thread API, work on the job
// system). One loader, one disk cache, one scheduler, one texture LRU for every kind of image.
//
//   screen --want(requests)--> Scheduler --(<= 2 loads at a time, Low priority)--> worker:
//       DiskCache (/data/PS4IPTV/cache/images) -> libcurl HTTP(S) download -> stb_image decode ->
//       downscale to the display size(s) of the image kind
//   main thread, update(): at most 2 decoded images per frame become GL textures (power-of-two pages; exact
//   sizes for the two large art kinds, images/art_sizes.h), uploaded once with GLTexture::unlock - the same
//   CPU->GPU path as the fixed glyph atlas; the CPU copy is freed after the upload - kept in a bounded LRU and
//   shared by every widget that shows them.
//
// The disk cache holds the downloaded bytes once per URL; memory entries are per (kind, URL), so a poster
// shown in the grid does not keep its large detail-screen version alive.
// Screens bind with get(kind, url) (nullptr = show a placeholder) and rebind when generation() changes.
// Image requests never go through mpv/FFmpeg.

#ifndef PS4IPTV_IMAGES_IMAGE_LOADER_H
#define PS4IPTV_IMAGES_IMAGE_LOADER_H

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cross2d/c2d.h"
#include "disk_cache.h"
#include "art_sizes.h"
#include "image_pipeline.h"
#include "image_scheduler.h"
#include "lru_cache.h"
#include "../network/jobs.h"

enum class ImageKind {
    Logo,          // channel logo: [0] row tile, [1] details panel / Home card
    Poster,        // movie poster / series cover in grids, Home cards, series detail cover: [0]
    PosterLarge,   // movie detail poster, detail pages' poster-derived backdrops: [0]
    Backdrop,      // real landscape backdrop / fanart (provider backdrop_path): [0]; at most MAX_BACKDROPS in memory
    HeroPoster     // the Home hero's poster fallback: [0] (images/art_sizes.h)
};

struct ImageVariant {
    std::shared_ptr<c2d::Texture> texture;
    c2d::Vector2i size;              // the image occupies the top-left `size` of the texture
    c2d::Vector2i page;              // the texture itself (== size for exact pages)
};

struct ImageSet {
    std::vector<ImageVariant> variants;
    c2d::Vector2i source;            // the decoded source image, before any downscale
    images::Average average;         // HeroPoster: the image's mean colour (the Home showcase's tint)

    const ImageVariant &at(size_t i) const { return variants[i < variants.size() ? i : 0]; }
};

struct ImageRequest {
    ImageKind kind;
    std::string url;                 // raw value from the provider
};

class ImageLoader {

public:

    // the box each variant is prepared for (aspect ratio kept, never upscaled on the CPU)
    static std::vector<images::Box> boxes(ImageKind kind);

    // largest source image accepted for a kind (either side): bounds the transient decode buffer
    static int maxSourceDimension(ImageKind kind);

    // Backdrop / HeroPoster: an exact-size texture instead of a power-of-two page (images/art_sizes.h)
    static bool exactPage(ImageKind kind);

    // backdrops are 3.5 MiB textures: only the most recent ones stay in memory (one per screen is the norm)
    static const int MAX_BACKDROPS = images::art::MAX_BACKDROPS;

    ImageLoader(JobSystem &jobs, const std::string &cacheDir);

    ~ImageLoader();

    void setEnabled(bool enabled);

    bool enabled() const { return on; }

    // what the screen needs, in priority order; replaces the previous request
    void want(const std::vector<ImageRequest> &requests);

    // logos only (Live TV)
    void want(const std::vector<std::string> &logoUrls);

    // nullptr while loading, failed or disabled (show the placeholder)
    std::shared_ptr<ImageSet> get(ImageKind kind, const std::string &url);

    std::shared_ptr<ImageSet> get(const std::string &logoUrl) { return get(ImageKind::Logo, logoUrl); }

    // the image could not be loaded (it stays unavailable until it is wanted again after a retry delay)
    bool failed(ImageKind kind, const std::string &url);

    // changes whenever a new image becomes available
    unsigned generation() const { return gen; }

    // main thread, every frame: starts loads, turns decoded images into textures. true = redraw.
    bool update(double now);

    // drops every image from memory and disk; done(bytes freed) on the main thread
    void clearCache(std::function<void(int64_t)> done);

    // disk usage (scanned on a worker the first time); -1 until known
    int64_t diskBytes() const { return diskUsage; }

    void refreshDiskUsage(std::function<void()> done);

private:

    struct Pending {
        std::string key;      // kind + normalized URL
        std::string url;      // normalized URL
        ImageKind kind;
        images::LoadOutcome outcome;
        images::Average average;
    };

    const std::string &normalized(const std::string &raw);

    std::string key(ImageKind kind, const std::string &raw);

    void start(const std::string &key);

    void finish(const std::shared_ptr<Pending> &p);

    JobSystem &jobs;
    std::shared_ptr<images::DiskCache> disk;
    images::Scheduler scheduler;
    images::LruCache<std::shared_ptr<ImageSet>> memory;
    std::unordered_map<std::string, std::string> normalizedUrls;
    std::deque<std::shared_ptr<Pending>> decoded;
    std::deque<std::string> backdropKeys;   // loaded backdrops, oldest first
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    double now = 0;
    unsigned gen = 0;
    bool on = true;
    int64_t diskUsage = -1;
};

#endif // PS4IPTV_IMAGES_IMAGE_LOADER_H
