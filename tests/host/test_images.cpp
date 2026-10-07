// Channel logo pipeline: URL keys, disk cache bounds/integrity, decoding, resizing, job scheduling,
// memory LRU and failure fallbacks.

#include <cstdlib>
#include <cstring>

#include "check.h"
#include "cross2d/skeleton/stb_image_write.h"
#include "../../src/images/disk_cache.h"
#include "../../src/images/image_decode.h"
#include "../../src/images/image_key.h"
#include "../../src/images/image_pipeline.h"
#include "../../src/images/image_scheduler.h"
#include "../../src/images/lru_cache.h"
#include "../../src/platform/fs.h"
#include "../../src/storage/catalog_cache.h"

using namespace images;

namespace {
    std::string emptyDir(const char *name) {
        const char *base = std::getenv("PS4IPTV_TEST_TMP");
        std::string d = fs::join(base ? base : ".", name);
        fs::ensureDir(d);
        for (const auto &e: fs::listDir(d)) {
            if (e.dir) {
                for (const auto &f: fs::listDir(fs::join(d, e.name))) {
                    fs::removeFile(fs::join(fs::join(d, e.name), f.name));
                }
            } else {
                fs::removeFile(fs::join(d, e.name));
            }
        }
        return d;
    }

    void appendBytes(void *ctx, void *data, int size) {
        ((std::string *) ctx)->append((const char *) data, (size_t) size);
    }

    // RGBA test image: opaque red square on a transparent background
    std::vector<uint8_t> pixels(int w, int h) {
        std::vector<uint8_t> px((size_t) w * h * 4, 0);
        for (int y = h / 4; y < h * 3 / 4; y++) {
            for (int x = w / 4; x < w * 3 / 4; x++) {
                uint8_t *p = &px[((size_t) y * w + x) * 4];
                p[0] = 230;
                p[3] = 255;
            }
        }
        return px;
    }

    std::string png(int w, int h) {
        std::string out;
        std::vector<uint8_t> px = pixels(w, h);
        stbi_write_png_to_func(appendBytes, &out, w, h, 4, px.data(), w * 4);
        return out;
    }

    std::string jpeg(int w, int h) {
        std::string out;
        std::vector<uint8_t> px((size_t) w * h * 3, 128);
        stbi_write_jpg_to_func(appendBytes, &out, w, h, 3, px.data(), 90);
        return out;
    }

    const std::vector<Box> BOXES = {{100, 54}, {256, 150}};
}

TEST(image_url_normalize_and_cache_key) {
    CHECK(normalizeUrl("https://img.example.com/images/live/sd-1.png") == "https://img.example.com/images/live/sd-1.png");
    // the provider really sends a URL with a space before ".png"
    CHECK(normalizeUrl("https://img.example.com/images/live/5544f7b3 .png") == "https://img.example.com/images/live/5544f7b3%20.png");
    CHECK(normalizeUrl("  http://h/a b.png\n") == "http://h/a%20b.png");
    CHECK(normalizeUrl("HTTP://h/x.png") == "HTTP://h/x.png");
    CHECK(normalizeUrl("http://h/%C3%A7.png") == "http://h/%C3%A7.png");       // existing escapes kept
    CHECK(normalizeUrl("http://h/\xC3\xA7|.png") == "http://h/%C3%A7%7C.png");  // raw UTF-8 + '|'
    CHECK(normalizeUrl("").empty());
    CHECK(normalizeUrl("null").empty());
    CHECK(normalizeUrl("ftp://h/x.png").empty());
    CHECK(normalizeUrl("file:///etc/passwd").empty());
    CHECK(normalizeUrl("http://").empty());
    CHECK(normalizeUrl("http://h/" + std::string(MAX_URL_LENGTH, 'a')).empty());

    std::string k = cacheKey("https://img.example.com/images/live/sd-1.png");
    CHECK_EQ(k.size(), (size_t) 16);
    CHECK(k.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(k == cacheKey("https://img.example.com/images/live/sd-1.png"));   // deterministic
    CHECK(k != cacheKey("https://img.example.com/images/live/sd-2.png"));
    // credentials in a URL never reach the file name
    std::string withCreds = cacheKey("http://host/logo.php?username=alice&password=secret");
    CHECK(withCreds.find("alice") == std::string::npos && withCreds.find("secret") == std::string::npos);
    CHECK(verifyHash("http://a/1.png") != fnv1a64("http://a/1.png"));
}

TEST(image_disk_cache_roundtrip_and_integrity) {
    std::string dir = emptyDir("img-cache-integrity");
    DiskCache::Config cfg;
    cfg.dir = dir;
    DiskCache cache(cfg);
    const std::string url = "https://img.example.com/images/live/sd-1.png";
    std::string bytes;
    CHECK(cache.get(url, 1000, bytes) == DiskCache::Lookup::Miss);
    std::string data = png(16, 16);
    CHECK(cache.put(url, data, 1000));
    CHECK(cache.get(url, 1001, bytes) == DiskCache::Lookup::Fresh);
    CHECK(bytes == data);
    // the file name is the hash, the URL is nowhere on disk
    auto files = fs::listDir(dir);
    CHECK_EQ(files.size(), (size_t) 1);
    CHECK(files[0].name == cacheKey(url) + ".img");
    std::string raw;
    fs::readFile(fs::join(dir, files[0].name), raw, 1 << 20);
    CHECK(raw.find("img.example") == std::string::npos);

    // a bit flip in the payload: Corrupt, file removed, then Miss
    raw[raw.size() - 5] ^= 0x40;
    fs::writeFileReplace(fs::join(dir, files[0].name), raw);
    CHECK(cache.get(url, 1002, bytes) == DiskCache::Lookup::Corrupt);
    CHECK(bytes.empty());
    CHECK(!fs::exists(fs::join(dir, files[0].name)));
    CHECK(cache.get(url, 1003, bytes) == DiskCache::Lookup::Miss);

    // a header written for another URL (hash collision) never returns that URL's bytes
    std::string payload;
    int64_t at = 0;
    CHECK(DiskCache::decode(url, DiskCache::encode(url, "abc", 5), payload, at) && payload == "abc" && at == 5);
    CHECK(!DiskCache::decode(url, DiskCache::encode(url + "x", "abc", 5), payload, at));
    CHECK(!DiskCache::decode(url, DiskCache::encode(url, "abc", 5).substr(0, 20), payload, at));   // truncated
    CHECK(!DiskCache::decode(url, "", payload, at));

    // a fresh instance (next app start) finds the file again and cleans an interrupted write
    CHECK(cache.put(url, data, 1000));
    fs::writeFileReplace(fs::join(dir, "0123456789abcdef.img.tmp"), "partial");
    DiskCache again(cfg);
    CHECK(again.get(url, 1004, bytes) == DiskCache::Lookup::Fresh && bytes == data);
    CHECK(!fs::exists(fs::join(dir, "0123456789abcdef.img.tmp")));
    CHECK_EQ(again.fileCount(), 1);
    // empty and oversized payloads are not stored
    CHECK(!cache.put(url, "", 1000));
    CHECK(!cache.put(url, std::string(cfg.maxFileBytes + 1, 'x'), 1000));
}

TEST(image_disk_cache_bounds_lru) {
    std::string dir = emptyDir("img-cache-bounds");
    DiskCache::Config cfg;
    cfg.dir = dir;
    cfg.maxBytes = 10 * 1000;
    cfg.trimTo = 6 * 1000;
    DiskCache cache(cfg);
    std::string blob(1000 - 28, 'p');   // 1000 bytes per file with the header
    for (int i = 0; i < 10; i++) {
        CHECK(cache.put("http://h/" + std::to_string(i) + ".png", blob, 100));
    }
    CHECK_EQ(cache.totalBytes(), (int64_t) 10000);
    std::string bytes;
    // touch 0 and 1: most recently used
    CHECK(cache.get("http://h/0.png", 100, bytes) == DiskCache::Lookup::Fresh);
    CHECK(cache.get("http://h/1.png", 100, bytes) == DiskCache::Lookup::Fresh);
    // one more: over the bound -> trimmed to <= 6000 bytes, least recently used first
    CHECK(cache.put("http://h/new.png", blob, 100));
    CHECK(cache.totalBytes() <= cfg.trimTo);
    CHECK_EQ(cache.fileCount(), 6);
    CHECK_EQ((int) fs::listDir(dir).size(), 6);
    CHECK(cache.get("http://h/new.png", 100, bytes) == DiskCache::Lookup::Fresh);
    CHECK(cache.get("http://h/0.png", 100, bytes) == DiskCache::Lookup::Fresh);
    CHECK(cache.get("http://h/1.png", 100, bytes) == DiskCache::Lookup::Fresh);
    CHECK(cache.get("http://h/2.png", 100, bytes) == DiskCache::Lookup::Miss);
    CHECK(cache.get("http://h/9.png", 100, bytes) == DiskCache::Lookup::Fresh);
}

TEST(image_disk_cache_stale_and_clear) {
    std::string dir = emptyDir("img-cache-stale");
    DiskCache::Config cfg;
    cfg.dir = dir;
    cfg.maxAgeSeconds = 100;
    DiskCache cache(cfg);
    CHECK(cache.put("http://h/a.png", "AAAA", 1000));
    std::string bytes;
    CHECK(cache.get("http://h/a.png", 1100, bytes) == DiskCache::Lookup::Fresh);
    CHECK(cache.get("http://h/a.png", 1101, bytes) == DiskCache::Lookup::Stale);
    CHECK(bytes == "AAAA");   // stale bytes are still returned
    CHECK(cache.put("http://h/b.png", "BBBB", 1000));
    // unrelated files in the directory are left alone
    fs::writeFileReplace(fs::join(dir, "readme.txt"), "keep");
    CHECK(cache.clear() > 0);
    CHECK_EQ(cache.fileCount(), 0);
    CHECK_EQ(cache.totalBytes(), (int64_t) 0);
    CHECK(cache.get("http://h/a.png", 1000, bytes) == DiskCache::Lookup::Miss);
    CHECK(fs::exists(fs::join(dir, "readme.txt")));
}

TEST(image_decode_formats_and_failures) {
    Image img;
    std::string err;
    std::string p = png(40, 20);
    CHECK(sniff(p) == Format::Png);
    CHECK(decode(p, img, &err));
    CHECK(img.valid() && img.w == 40 && img.h == 20);
    CHECK(img.rgba[(10 * 40 + 20) * 4 + 0] == 230 && img.rgba[(10 * 40 + 20) * 4 + 3] == 255);  // red, opaque
    CHECK(img.rgba[3] == 0);                                                                    // corner clear
    std::string j = jpeg(24, 24);
    CHECK(sniff(j) == Format::Jpeg);
    CHECK(decode(j, img, &err) && img.w == 24 && img.rgba[3] == 255);
    // failures: never a half-filled image
    CHECK(!decode("", img, &err) && !img.valid());
    CHECK(!decode("<html><body>404 Not Found</body></html>", img, &err) && !img.valid());
    CHECK(!decode(p.substr(0, p.size() / 2), img, &err) && !img.valid());   // truncated download
    std::string webp = std::string("RIFF\x10\0\0\0WEBPVP8 ", 16) + std::string(32, '\0');
    CHECK(sniff(webp) == Format::Webp && !decodable(Format::Webp));
    CHECK(!decode(webp, img, &err) && err.find("WebP") != std::string::npos);
    std::string corrupt = p;
    for (size_t i = 60; i < corrupt.size() - 12; i++) {
        corrupt[i] = (char) (corrupt[i] ^ 0x5A);
    }
    CHECK(!decode(corrupt, img, &err) && !img.valid());
    // content types
    CHECK(acceptableContentType("image/png"));
    CHECK(acceptableContentType("Image/JPEG; charset=binary"));
    CHECK(acceptableContentType(""));
    CHECK(acceptableContentType("application/octet-stream"));
    CHECK(!acceptableContentType("text/html; charset=UTF-8"));
    CHECK(!acceptableContentType("application/json"));
    CHECK(!acceptableContentType("image/svg+xml"));
}

TEST(image_fit_and_alpha_correct_resize) {
    Size s = fitInside(128, 128, 100, 54, false);
    CHECK(s.w == 54 && s.h == 54);                 // square logo in a wide box
    s = fitInside(192, 128, 100, 54, false);
    CHECK(s.w == 81 && s.h == 54);                 // aspect ratio kept
    s = fitInside(434, 434, 256, 150, false);
    CHECK(s.w == 150 && s.h == 150);
    s = fitInside(64, 40, 256, 150, false);
    CHECK(s.w == 64 && s.h == 40);                 // small images are not upscaled on the CPU
    s = fitInside(64, 40, 256, 150, true);
    CHECK(s.w == 240 && s.h == 150);
    s = fitInside(1000, 1, 100, 54, false);
    CHECK(s.w == 100 && s.h == 1);                 // never 0
    CHECK(fitInside(0, 10, 10, 10, false).w == 0);

    // white opaque pixel next to a transparent BLACK pixel: the average must stay white, half transparent
    Image src;
    src.w = 2;
    src.h = 1;
    src.rgba = {255, 255, 255, 255, 0, 0, 0, 0};
    Image half = resizeArea(src, 1, 1);
    CHECK(half.valid());
    CHECK(half.rgba[0] == 255 && half.rgba[1] == 255 && half.rgba[2] == 255);
    CHECK(half.rgba[3] == 128);
    // fully transparent stays fully transparent
    src.rgba = {9, 9, 9, 0, 7, 7, 7, 0};
    CHECK(resizeArea(src, 1, 1).rgba[3] == 0);

    Image logo;
    CHECK(decode(png(256, 256), logo));
    Image small = prepare(logo, 100, 54);
    CHECK(small.w == 54 && small.h == 54 && small.valid());
    CHECK(small.rgba[(27 * 54 + 27) * 4 + 0] == 230 && small.rgba[(27 * 54 + 27) * 4 + 3] == 255);
    CHECK(small.rgba[3] == 0);
}

TEST(image_scheduler_dedup_priority_and_obsolete_jobs) {
    Scheduler::Config cfg;
    cfg.maxInFlight = 2;
    cfg.maxQueue = 5;
    Scheduler s(cfg);
    // duplicates and empty URLs are dropped; order = priority
    s.want({"sel", "a", "", "a", "b", "sel", "c"}, 0);
    CHECK_EQ(s.queued(), (size_t) 4);
    CHECK(s.next() == "sel");
    CHECK(s.next() == "a");
    CHECK(s.next().empty());              // in-flight limit
    CHECK_EQ(s.inFlight(), 2);
    // scrolling on: the old queue (b, c) is discarded, loads in flight are not started twice
    s.want({"x", "a", "y"}, 0);
    CHECK_EQ(s.queued(), (size_t) 2);
    CHECK(s.state("b") == Scheduler::State::Unknown);
    CHECK(s.state("a") == Scheduler::State::Loading);
    s.finished("sel", Scheduler::Result::Ok, 0);
    CHECK(s.state("sel") == Scheduler::State::Ready);
    CHECK(s.next() == "x");
    CHECK(s.next().empty());
    // Ready images are not requested again until the memory cache forgets them
    s.want({"sel"}, 0);
    CHECK_EQ(s.queued(), (size_t) 0);
    s.forget("sel");
    s.want({"sel"}, 0);
    CHECK_EQ(s.queued(), (size_t) 1);
    // a stale completion for something not loading is ignored
    s.finished("never", Scheduler::Result::Ok, 0);
    CHECK_EQ(s.inFlight(), 2);
    // the queue is bounded
    std::vector<std::string> many;
    for (int i = 0; i < 50; i++) {
        many.push_back("m" + std::to_string(i));
    }
    s.want(many, 0);
    CHECK_EQ(s.queued(), cfg.maxQueue);
    s.reset();
    CHECK_EQ(s.queued(), (size_t) 0);
    CHECK_EQ(s.inFlight(), 2);            // loads in flight still complete normally
    s.finished("a", Scheduler::Result::Ok, 0);
    s.finished("x", Scheduler::Result::Ok, 0);
    CHECK_EQ(s.inFlight(), 0);
}

TEST(image_scheduler_failure_backoff) {
    Scheduler::Config cfg;
    cfg.retryAfter = 60;
    Scheduler s(cfg);
    s.want({"net", "gone"}, 0);
    CHECK(s.next() == "net");
    CHECK(s.next() == "gone");
    s.finished("net", Scheduler::Result::RetryLater, 10);
    s.finished("gone", Scheduler::Result::Permanent, 10);
    CHECK(s.state("net") == Scheduler::State::Failed);
    s.want({"net", "gone"}, 30);
    CHECK_EQ(s.queued(), (size_t) 0);     // too early / permanent
    s.want({"net", "gone"}, 71);
    CHECK_EQ(s.queued(), (size_t) 1);     // the network failure is retried after the backoff
    CHECK(s.next() == "net");
}

TEST(image_memory_lru_bounds) {
    LruCache<int> lru(3, 1000);
    CHECK(lru.put("a", 1, 100).empty());
    CHECK(lru.put("b", 2, 100).empty());
    CHECK(lru.put("c", 3, 100).empty());
    CHECK(lru.get("a") != nullptr);       // a is now most recent
    std::vector<std::string> ev = lru.put("d", 4, 100);
    CHECK(ev.size() == 1 && ev[0] == "b");
    CHECK(!lru.contains("b") && lru.contains("a"));
    ev = lru.put("big", 5, 900);          // byte bound: evicts least recent (c, then a) until <= 1000
    CHECK(ev.size() == 2 && ev[0] == "c" && ev[1] == "a");
    CHECK_EQ(lru.bytes(), (int64_t) 1000);
    CHECK(lru.contains("big") && lru.contains("d"));
    lru.put("big", 6, 50);                // replace keeps the byte count right
    CHECK_EQ(lru.bytes(), (int64_t) 150);
    CHECK(*lru.get("big") == 6);
    lru.clear();
    CHECK(lru.size() == 0 && lru.bytes() == 0);
}

TEST(image_pipeline_fallbacks) {
    std::string dir = emptyDir("img-pipeline");
    DiskCache::Config cfg;
    cfg.dir = dir;
    cfg.maxAgeSeconds = 1000;
    DiskCache disk(cfg);
    int fetches = 0;
    FetchResponse next;
    Fetcher fetch = [&](const std::string &) {
        fetches++;
        return next;
    };
    const std::string url = "https://img.example.com/images/live/a.png";

    next.networkError = true;
    next.detail = "Server timeout";
    LoadOutcome o = loadImage(url, disk, fetch, 100, BOXES);
    CHECK(o.result == Scheduler::Result::RetryLater && o.variants.empty());

    next = FetchResponse();
    next.status = 404;
    CHECK(loadImage(url, disk, fetch, 100, BOXES).result == Scheduler::Result::Permanent);
    next.status = 503;
    CHECK(loadImage(url, disk, fetch, 100, BOXES).result == Scheduler::Result::RetryLater);

    next.status = 200;
    next.contentType = "text/html";
    next.body = "<html>blocked</html>";
    CHECK(loadImage(url, disk, fetch, 100, BOXES).result == Scheduler::Result::Permanent);
    next.contentType = "image/png";
    next.body = "\x89PNG\r\n\x1a\n garbage";
    CHECK(loadImage(url, disk, fetch, 100, BOXES).result == Scheduler::Result::Permanent);
    CHECK_EQ(disk.fileCount(), 0);        // nothing undecodable is cached

    next.body = png(128, 128);
    fetches = 0;
    o = loadImage(url, disk, fetch, 100, BOXES);
    CHECK(o.result == Scheduler::Result::Ok && !o.fromDisk && fetches == 1);
    CHECK(o.variants.size() == 2 && o.variants[0].w == 54 && o.variants[1].w == 128);
    CHECK_EQ(disk.fileCount(), 1);
    // second load: from disk, no network
    o = loadImage(url, disk, fetch, 200, BOXES);
    CHECK(o.result == Scheduler::Result::Ok && o.fromDisk && fetches == 1);

    // cached file damaged -> removed and downloaded again
    std::string path = disk.pathFor(url);
    std::string raw;
    fs::readFile(path, raw, 1 << 20);
    raw[40] ^= 0x21;
    fs::writeFileReplace(path, raw);
    o = loadImage(url, disk, fetch, 300, BOXES);
    CHECK(o.result == Scheduler::Result::Ok && !o.fromDisk && fetches == 2);

    // stale cache + download failure -> the stale copy is still shown
    next = FetchResponse();
    next.networkError = true;
    o = loadImage(url, disk, fetch, 300 + 5000, BOXES);
    CHECK(o.result == Scheduler::Result::Ok && o.staleUsed && o.variants.size() == 2);
}

TEST(image_real_provider_logos) {
    // optional: logos captured from the real provider (build/icon-samples, never committed)
    const char *dir = std::getenv("PS4IPTV_ICON_SAMPLES");
    if (dir == nullptr) {
        std::printf("  (skipped: PS4IPTV_ICON_SAMPLES not set)\n");
        return;
    }
    int ok = 0;
    int total = 0;
    for (const auto &e: fs::listDir(dir)) {
        std::string bytes;
        if (e.dir || !fs::readFile(fs::join(dir, e.name), bytes, 4 << 20)) {
            continue;
        }
        total++;
        Image img;
        std::string err;
        if (decode(bytes, img, &err) && prepare(img, 100, 54).valid() && prepare(img, 256, 150).valid()) {
            ok++;
        } else {
            std::printf("  %s: %s\n", e.name.c_str(), err.c_str());
        }
    }
    std::printf("  decoded %d of %d provider logos\n", ok, total);
    CHECK(total > 0);
    CHECK_EQ(ok, total);
}

TEST(catalog_cache_clear_keeps_image_cache) {
    std::string root = emptyDir("catalog-vs-images");
    std::string images = fs::join(fs::join(root, "cache"), CatalogCache::IMAGE_DIR);
    emptyDir("catalog-vs-images/cache/p1");
    fs::ensureDir(images);
    CatalogCache cc(root);
    CHECK(cc.save("p1", "live_streams", "[]", 1));
    fs::writeFileReplace(fs::join(images, "0123456789abcdef.img"), "img");
    cc.clearAll();
    std::string body;
    int64_t at;
    CHECK(!cc.load("p1", "live_streams", body, at));
    CHECK(fs::exists(fs::join(images, "0123456789abcdef.img")));
}
