// Channel logo service (main thread API, work on the job system).
//
//   screen --want(urls)--> Scheduler --(<= 2 loads at a time, Low priority)--> worker:
//       DiskCache (/data/PS4IPTV/cache/images) -> libcurl HTTPS download -> stb_image decode ->
//       downscale to the two display sizes
//   main thread, update(): at most 2 decoded images per frame become GL textures (power-of-two pages,
//   uploaded once with GLTexture::unlock - the same CPU->GPU path as the fixed glyph atlas), kept in a
//   bounded LRU and shared by every widget that shows them.
//
// Screens bind rows with get(url) (nullptr = show initials) and rebind when generation() changes.
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
#include "image_pipeline.h"
#include "image_scheduler.h"
#include "lru_cache.h"
#include "../network/jobs.h"

// one logo in its two display sizes; the image occupies the top-left `size` of each texture
struct LogoImages {
    std::shared_ptr<c2d::Texture> small;
    c2d::Vector2i smallSize;
    std::shared_ptr<c2d::Texture> large;
    c2d::Vector2i largeSize;
};

class ImageLoader {

public:

    // image boxes the variants are prepared for (channel row logo, details panel logo)
    static const images::Box ROW_BOX;
    static const images::Box DETAIL_BOX;

    ImageLoader(JobSystem &jobs, const std::string &cacheDir);

    ~ImageLoader();

    void setEnabled(bool enabled);

    bool enabled() const { return on; }

    // raw stream_icon values in priority order; replaces the previous request
    void want(const std::vector<std::string> &urls);

    // nullptr while loading, failed or disabled (show the initials placeholder)
    std::shared_ptr<LogoImages> get(const std::string &url);

    // changes whenever a new logo becomes available
    unsigned generation() const { return gen; }

    // main thread, every frame: starts loads, turns decoded images into textures. true = redraw.
    bool update(double now);

    // drops every logo from memory and disk; done(bytes freed) on the main thread
    void clearCache(std::function<void(int64_t)> done);

    // disk usage (scanned on a worker the first time); -1 until known
    int64_t diskBytes() const { return diskUsage; }

    void refreshDiskUsage(std::function<void()> done);

private:

    struct Pending {
        std::string url;
        images::LoadOutcome outcome;
    };

    const std::string &normalized(const std::string &raw);

    void start(const std::string &url);

    void finish(const std::shared_ptr<Pending> &p);

    JobSystem &jobs;
    std::shared_ptr<images::DiskCache> disk;
    images::Scheduler scheduler;
    images::LruCache<std::shared_ptr<LogoImages>> memory;
    std::unordered_map<std::string, std::string> normalizedUrls;
    std::deque<std::shared_ptr<Pending>> decoded;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    double now = 0;
    unsigned gen = 0;
    bool on = true;
    int64_t diskUsage = -1;
    int loaded = 0;
    int failed = 0;
};

#endif // PS4IPTV_IMAGES_IMAGE_LOADER_H
