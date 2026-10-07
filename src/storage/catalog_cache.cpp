#include <cstdio>

#include "catalog_cache.h"
#include "../core/json.h"
#include "../platform/fs.h"

namespace {
    const size_t MAX_CACHE_BYTES = 128u * 1024 * 1024;

    bool safeName(const std::string &s) {
        if (s.empty() || s.size() > 64) {
            return false;
        }
        for (char c: s) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
                return false;
            }
        }
        return true;
    }

    std::string hex(uint64_t v) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%016llx", (unsigned long long) v);
        return buf;
    }
}

CatalogCache::CatalogCache(std::string dataDir) : root(fs::join(dataDir, "cache")) {}

std::string CatalogCache::dir(const std::string &profileId) const {
    return fs::join(root, profileId);
}

uint64_t CatalogCache::checksum(const std::string &body) {
    uint64_t h = 1469598103934665603ull;   // FNV-1a 64
    for (unsigned char c: body) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

bool CatalogCache::save(const std::string &profileId, const std::string &name, const std::string &body, int64_t now,
                        int items, std::string *error) const {
    if (!safeName(profileId) || !safeName(name)) {
        if (error) {
            *error = "invalid cache name";
        }
        return false;
    }
    if (!fs::ensureDir(dir(profileId), error)) {
        return false;
    }
    json::Value meta = json::Value::makeObject();
    meta.set("version", json::Value::makeInt(FORMAT_VERSION));
    meta.set("savedAt", json::Value::makeInt(now));
    meta.set("bytes", json::Value::makeInt((int64_t) body.size()));
    meta.set("fnv64", json::Value::makeString(hex(checksum(body))));
    meta.set("items", json::Value::makeInt(items));
    std::string base = fs::join(dir(profileId), name);
    // the old meta goes first: a crash between the two writes leaves no meta matching a half-replaced pair.
    // The body is a cache (checksummed, refetched when damaged): replaced without keeping a backup copy.
    fs::removeFile(base + ".meta");
    fs::removeFile(base + ".json.bak");
    return fs::writeFileReplace(base + ".json", body, error)
           && fs::writeFileAtomic(base + ".meta", json::write(meta, false), error);
}

bool CatalogCache::load(const std::string &profileId, const std::string &name, std::string &body, Meta &out) const {
    out = Meta();
    if (!safeName(profileId) || !safeName(name)) {
        return false;
    }
    std::string base = fs::join(dir(profileId), name);
    std::string metaText;
    json::Value meta;
    if (!fs::readFile(base + ".meta", metaText, 4096)) {
        return false;
    }
    if (!json::parse(metaText, meta) || meta["version"].asInt(0) != FORMAT_VERSION) {
        remove(profileId, name);   // another schema (or damaged): never used, refetched
        return false;
    }
    if (!fs::readFile(base + ".json", body, MAX_CACHE_BYTES) || (int64_t) body.size() != meta["bytes"].asInt(-1)
        || hex(checksum(body)) != meta["fnv64"].asString()) {
        body.clear();
        remove(profileId, name);
        return false;
    }
    out.savedAt = meta["savedAt"].asInt(0);
    out.items = (int) meta["items"].asInt(-1);
    return true;
}

bool CatalogCache::load(const std::string &profileId, const std::string &name, std::string &body,
                        int64_t &savedAt) const {
    Meta m;
    if (!load(profileId, name, body, m)) {
        return false;
    }
    savedAt = m.savedAt;
    return true;
}

void CatalogCache::remove(const std::string &profileId, const std::string &name) const {
    if (!safeName(profileId) || !safeName(name)) {
        return;
    }
    std::string base = fs::join(dir(profileId), name);
    fs::removeFile(base + ".meta");
    fs::removeFile(base + ".json");
}

void CatalogCache::clearProfile(const std::string &profileId) const {
    if (!safeName(profileId)) {
        return;
    }
    for (const auto &e: fs::listDir(dir(profileId))) {
        if (!e.dir) {
            fs::removeFile(fs::join(dir(profileId), e.name));
        }
    }
}

void CatalogCache::clearAll() const {
    for (const auto &e: fs::listDir(root)) {
        if (e.dir && e.name != IMAGE_DIR) {
            clearProfile(e.name);
        }
    }
}
