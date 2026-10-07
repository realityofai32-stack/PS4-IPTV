// Versioned on-disk cache of Xtream list responses: <dataDir>/cache/<profileId>/<name>.json (+ .meta).
// The raw response is stored, so loading goes through the same parser as a network response; a corrupt
// or old-format file is simply ignored.

#ifndef PS4IPTV_STORAGE_CATALOG_CACHE_H
#define PS4IPTV_STORAGE_CATALOG_CACHE_H

#include <cstdint>
#include <string>

class CatalogCache {

public:

    static const int FORMAT_VERSION = 1;

    explicit CatalogCache(std::string dataDir);

    bool save(const std::string &profileId, const std::string &name, const std::string &body, int64_t now,
              std::string *error = nullptr) const;

    // false when missing, unreadable or written by another format version
    bool load(const std::string &profileId, const std::string &name, std::string &body, int64_t &savedAt) const;

    void clearProfile(const std::string &profileId) const;

    void clearAll() const;

private:

    std::string dir(const std::string &profileId) const;

    std::string root;
};

#endif // PS4IPTV_STORAGE_CATALOG_CACHE_H
