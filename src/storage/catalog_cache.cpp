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
}

CatalogCache::CatalogCache(std::string dataDir) : root(fs::join(dataDir, "cache")) {}

std::string CatalogCache::dir(const std::string &profileId) const {
    return fs::join(root, profileId);
}

bool CatalogCache::save(const std::string &profileId, const std::string &name, const std::string &body, int64_t now,
                        std::string *error) const {
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
    std::string base = fs::join(dir(profileId), name);
    // body first: a crash between the two writes leaves a meta that does not match the body size
    return fs::writeFileAtomic(base + ".json", body, error)
           && fs::writeFileAtomic(base + ".meta", json::write(meta, false), error);
}

bool CatalogCache::load(const std::string &profileId, const std::string &name, std::string &body,
                        int64_t &savedAt) const {
    if (!safeName(profileId) || !safeName(name)) {
        return false;
    }
    std::string base = fs::join(dir(profileId), name);
    std::string metaText;
    json::Value meta;
    if (!fs::readFile(base + ".meta", metaText, 4096) || !json::parse(metaText, meta)
        || meta["version"].asInt(0) != FORMAT_VERSION) {
        return false;
    }
    if (!fs::readFile(base + ".json", body, MAX_CACHE_BYTES) || (int64_t) body.size() != meta["bytes"].asInt(-1)) {
        body.clear();
        return false;
    }
    savedAt = meta["savedAt"].asInt(0);
    return true;
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
