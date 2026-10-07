// Persistent image cache: <dir>/<cacheKey>.img holding the downloaded bytes (host-testable, thread-safe).
//
// File layout (little endian): "PIC1" | u64 verifyHash(url) | i64 fetchedAt (unix) | u32 length |
// u32 FNV-1a-32 of the payload | payload. A wrong hash, length or checksum is reported as Corrupt and the
// file is deleted. The cache is bounded by total bytes: when a write pushes it over maxBytes, the least
// recently used files are removed until it is under trimTo. Files older than maxAgeSeconds are Stale: the
// caller re-downloads them but may still show the stale copy if that fails.

#ifndef PS4IPTV_IMAGES_DISK_CACHE_H
#define PS4IPTV_IMAGES_DISK_CACHE_H

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace images {

    class DiskCache {

    public:

        struct Config {
            std::string dir;
            int64_t maxBytes = 48ll * 1024 * 1024;
            int64_t trimTo = 40ll * 1024 * 1024;
            int64_t maxAgeSeconds = 14ll * 24 * 3600;
            size_t maxFileBytes = 2u * 1024 * 1024;
        };

        enum class Lookup {
            Miss,
            Fresh,
            Stale,     // bytes returned, but older than maxAgeSeconds
            Corrupt    // file removed
        };

        explicit DiskCache(Config config);

        // scans the directory once (lazily on first use): sizes for the bound, removes *.tmp leftovers
        void open();

        Lookup get(const std::string &normalizedUrl, int64_t now, std::string &bytes);

        bool put(const std::string &normalizedUrl, const std::string &bytes, int64_t now,
                 std::string *error = nullptr);

        void remove(const std::string &normalizedUrl);

        // deletes every cache file; returns the bytes freed
        int64_t clear();

        int64_t totalBytes();

        int fileCount();

        std::string pathFor(const std::string &normalizedUrl) const;

        static std::string encode(const std::string &normalizedUrl, const std::string &payload, int64_t fetchedAt);

        // false when the header does not match `normalizedUrl` or the payload is damaged
        static bool decode(const std::string &normalizedUrl, const std::string &file, std::string &payload,
                           int64_t &fetchedAt);

    private:

        struct Entry {
            int64_t size = 0;
            int64_t lastUse = 0;
        };

        void openLocked();

        void trimLocked(const std::string &keep);

        void removeLocked(const std::string &key);

        Config cfg;
        std::mutex mutex;
        bool opened = false;
        int64_t total = 0;
        int64_t useClock = 0;
        std::unordered_map<std::string, Entry> entries;   // key -> entry
    };
}

#endif // PS4IPTV_IMAGES_DISK_CACHE_H
