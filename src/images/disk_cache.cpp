#include <algorithm>
#include <cstring>
#include <vector>

#include "disk_cache.h"
#include "image_key.h"
#include "../platform/fs.h"

namespace images {

    namespace {
        const char MAGIC[4] = {'P', 'I', 'C', '1'};
        const size_t HEADER = 4 + 8 + 8 + 4 + 4;
        const char *EXT = ".img";

        void putU64(std::string &s, uint64_t v) {
            for (int i = 0; i < 8; i++) {
                s += (char) ((v >> (8 * i)) & 0xFF);
            }
        }

        void putU32(std::string &s, uint32_t v) {
            for (int i = 0; i < 4; i++) {
                s += (char) ((v >> (8 * i)) & 0xFF);
            }
        }

        uint64_t getU64(const std::string &s, size_t at) {
            uint64_t v = 0;
            for (int i = 7; i >= 0; i--) {
                v = (v << 8) | (unsigned char) s[at + (size_t) i];
            }
            return v;
        }

        uint32_t getU32(const std::string &s, size_t at) {
            uint32_t v = 0;
            for (int i = 3; i >= 0; i--) {
                v = (v << 8) | (unsigned char) s[at + (size_t) i];
            }
            return v;
        }

        uint32_t fnv32(const char *data, size_t n) {
            uint32_t h = 2166136261u;
            for (size_t i = 0; i < n; i++) {
                h = (h ^ (unsigned char) data[i]) * 16777619u;
            }
            return h;
        }

        bool endsWith(const std::string &s, const char *suffix) {
            size_t n = strlen(suffix);
            return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
        }

        bool isKey(const std::string &name) {
            if (name.size() != 16 + strlen(EXT) || !endsWith(name, EXT)) {
                return false;
            }
            for (size_t i = 0; i < 16; i++) {
                char c = name[i];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                    return false;
                }
            }
            return true;
        }
    }

    DiskCache::DiskCache(Config config) : cfg(std::move(config)) {}

    std::string DiskCache::pathFor(const std::string &url) const {
        return fs::join(cfg.dir, cacheKey(url) + EXT);
    }

    std::string DiskCache::encode(const std::string &url, const std::string &payload, int64_t fetchedAt) {
        std::string s;
        s.reserve(HEADER + payload.size());
        s.append(MAGIC, 4);
        putU64(s, verifyHash(url));
        putU64(s, (uint64_t) fetchedAt);
        putU32(s, (uint32_t) payload.size());
        putU32(s, fnv32(payload.data(), payload.size()));
        s += payload;
        return s;
    }

    bool DiskCache::decode(const std::string &url, const std::string &file, std::string &payload,
                           int64_t &fetchedAt) {
        if (file.size() < HEADER || memcmp(file.data(), MAGIC, 4) != 0 || getU64(file, 4) != verifyHash(url)) {
            return false;
        }
        uint32_t len = getU32(file, 20);
        if ((size_t) len != file.size() - HEADER || len == 0) {
            return false;
        }
        if (fnv32(file.data() + HEADER, len) != getU32(file, 24)) {
            return false;
        }
        fetchedAt = (int64_t) getU64(file, 12);
        payload = file.substr(HEADER);
        return true;
    }

    void DiskCache::open() {
        std::lock_guard<std::mutex> lock(mutex);
        openLocked();
    }

    void DiskCache::openLocked() {
        if (opened) {
            return;
        }
        opened = true;
        fs::ensureDir(cfg.dir);
        int64_t newest = 0;
        for (const auto &e: fs::listDir(cfg.dir)) {
            if (e.dir) {
                continue;
            }
            if (endsWith(e.name, ".tmp")) {
                fs::removeFile(fs::join(cfg.dir, e.name));  // interrupted write
                continue;
            }
            if (!isKey(e.name)) {
                continue;
            }
            Entry entry;
            entry.size = e.size;
            entry.lastUse = e.mtime;
            newest = std::max(newest, e.mtime);
            entries[e.name.substr(0, 16)] = entry;
            total += e.size;
        }
        useClock = newest;
        trimLocked("");
    }

    DiskCache::Lookup DiskCache::get(const std::string &url, int64_t now, std::string &bytes) {
        std::lock_guard<std::mutex> lock(mutex);
        openLocked();
        bytes.clear();
        std::string key = cacheKey(url);
        auto it = entries.find(key);
        if (it == entries.end()) {
            return Lookup::Miss;
        }
        std::string file;
        int64_t fetchedAt = 0;
        if (!fs::readFile(fs::join(cfg.dir, key + EXT), file, cfg.maxFileBytes + HEADER)
            || !decode(url, file, bytes, fetchedAt)) {
            removeLocked(key);
            bytes.clear();
            return Lookup::Corrupt;
        }
        it->second.lastUse = ++useClock;
        if (cfg.maxAgeSeconds > 0 && now - fetchedAt > cfg.maxAgeSeconds) {
            return Lookup::Stale;
        }
        return Lookup::Fresh;
    }

    bool DiskCache::put(const std::string &url, const std::string &bytes, int64_t now, std::string *error) {
        if (bytes.empty() || bytes.size() > cfg.maxFileBytes) {
            if (error) {
                *error = "image size out of range";
            }
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex);
        openLocked();
        std::string key = cacheKey(url);
        std::string data = encode(url, bytes, now);
        if (!fs::writeFileReplace(fs::join(cfg.dir, key + EXT), data, error)) {
            return false;
        }
        auto it = entries.find(key);
        if (it != entries.end()) {
            total -= it->second.size;
        }
        Entry &e = entries[key];
        e.size = (int64_t) data.size();
        e.lastUse = ++useClock;
        total += e.size;
        trimLocked(key);
        return true;
    }

    void DiskCache::remove(const std::string &url) {
        std::lock_guard<std::mutex> lock(mutex);
        openLocked();
        removeLocked(cacheKey(url));
    }

    void DiskCache::removeLocked(const std::string &key) {
        auto it = entries.find(key);
        if (it != entries.end()) {
            total -= it->second.size;
            entries.erase(it);
        }
        fs::removeFile(fs::join(cfg.dir, key + EXT));
    }

    void DiskCache::trimLocked(const std::string &keep) {
        if (total <= cfg.maxBytes) {
            return;
        }
        std::vector<std::pair<int64_t, std::string>> order;
        order.reserve(entries.size());
        for (const auto &e: entries) {
            if (e.first != keep) {
                order.emplace_back(e.second.lastUse, e.first);
            }
        }
        std::sort(order.begin(), order.end());
        for (const auto &o: order) {
            if (total <= cfg.trimTo) {
                break;
            }
            removeLocked(o.second);
        }
    }

    int64_t DiskCache::clear() {
        std::lock_guard<std::mutex> lock(mutex);
        openLocked();
        int64_t freed = 0;
        for (const auto &e: fs::listDir(cfg.dir)) {
            if (!e.dir && (isKey(e.name) || endsWith(e.name, ".tmp"))) {
                if (fs::removeFile(fs::join(cfg.dir, e.name))) {
                    freed += e.size;
                }
            }
        }
        entries.clear();
        total = 0;
        return freed;
    }

    int64_t DiskCache::totalBytes() {
        std::lock_guard<std::mutex> lock(mutex);
        openLocked();
        return total;
    }

    int DiskCache::fileCount() {
        std::lock_guard<std::mutex> lock(mutex);
        openLocked();
        return (int) entries.size();
    }
}
