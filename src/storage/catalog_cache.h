// Versioned on-disk cache of Xtream list responses: <dataDir>/cache/<profileId>/<name>.json (+ .meta).
// The raw response is stored, so loading goes through the same parser as a network response.
//
// Schema 2 (Checkpoint 2.5): the meta records the schema version, the body size, an FNV-1a checksum of
// the body and how many items the parser kept when it was saved. A copy written by another schema, or one
// whose size / checksum / item count does not match, is deleted and never partially used: the caller then
// fetches the list from the provider again.

#ifndef PS4IPTV_STORAGE_CATALOG_CACHE_H
#define PS4IPTV_STORAGE_CATALOG_CACHE_H

#include <cstdint>
#include <string>

class CatalogCache {

public:

    static const int FORMAT_VERSION = 2;

    // <dataDir>/cache/images/ belongs to the image cache (images::DiskCache): clearAll() leaves it alone
    static constexpr const char *IMAGE_DIR = "images";

    struct Meta {
        int64_t savedAt = 0;
        int items = -1;        // items the parser kept from this body when it was saved (-1 = not recorded)
    };

    explicit CatalogCache(std::string dataDir);

    // items: parsed item count to verify on load (-1 = do not verify)
    bool save(const std::string &profileId, const std::string &name, const std::string &body, int64_t now,
              int items = -1, std::string *error = nullptr) const;

    // false when missing, unreadable, damaged or written by another schema (such copies are deleted)
    bool load(const std::string &profileId, const std::string &name, std::string &body, Meta &meta) const;

    bool load(const std::string &profileId, const std::string &name, std::string &body, int64_t &savedAt) const;

    // deletes one saved response (e.g. after it failed to parse)
    void remove(const std::string &profileId, const std::string &name) const;

    void clearProfile(const std::string &profileId) const;

    void clearAll() const;

    static uint64_t checksum(const std::string &body);

private:

    std::string dir(const std::string &profileId) const;

    std::string root;
};

#endif // PS4IPTV_STORAGE_CATALOG_CACHE_H
