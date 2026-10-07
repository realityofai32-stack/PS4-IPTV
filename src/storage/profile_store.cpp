#include "profile_store.h"
#include "../core/json.h"
#include "../platform/fs.h"
#include "../platform/redact.h"

namespace {
    const int FORMAT_VERSION = 1;
    const size_t MAX_FILE_BYTES = 1024 * 1024;

    void registerSecrets(const iptv::Profile &p) {
        redact::addSecret(p.password);
        redact::addSecret(p.username);
    }
}

ProfileStore::ProfileStore(std::string dataDir) : dir(std::move(dataDir)) {}

std::string ProfileStore::path() const {
    return fs::join(dir, "profiles.json");
}

std::string ProfileStore::serialize() const {
    json::Value root = json::Value::makeObject();
    root.set("version", json::Value::makeInt(FORMAT_VERSION));
    root.set("activeProfileId", json::Value::makeString(activeProfileId));
    json::Value list = json::Value::makeArray();
    for (const auto &p: items) {
        json::Value o = json::Value::makeObject();
        o.set("id", json::Value::makeString(p.id));
        o.set("name", json::Value::makeString(p.name));
        o.set("server", json::Value::makeString(p.server));
        o.set("username", json::Value::makeString(p.username));
        o.set("password", json::Value::makeString(p.password));
        o.set("createdAt", json::Value::makeInt(p.createdAt));
        o.set("lastUsedAt", json::Value::makeInt(p.lastUsedAt));
        o.set("lastStatus", json::Value::makeString(p.lastStatus));
        list.push(std::move(o));
    }
    root.set("profiles", std::move(list));
    return json::write(root);
}

bool ProfileStore::deserialize(const std::string &text, std::string *error) {
    json::Value root;
    std::string err;
    if (!json::parse(text, root, &err)) {
        if (error) {
            *error = "profiles.json is corrupt: " + err;
        }
        return false;
    }
    int64_t version = root["version"].asInt(0);
    if (!root.isObject() || version < 1 || version > FORMAT_VERSION) {
        if (error) {
            *error = "profiles.json has an unsupported format version " + std::to_string(version);
        }
        return false;
    }
    std::vector<iptv::Profile> loaded;
    for (const auto &o: root["profiles"].items()) {
        iptv::Profile p;
        p.id = o["id"].asString();
        p.name = o["name"].asString();
        p.server = o["server"].asString();
        p.username = o["username"].asString();
        p.password = o["password"].asString();
        p.createdAt = o["createdAt"].asInt(0);
        p.lastUsedAt = o["lastUsedAt"].asInt(0);
        p.lastStatus = o["lastStatus"].asString();
        if (p.id.empty() || p.server.empty()) {
            continue;
        }
        registerSecrets(p);
        loaded.push_back(std::move(p));
    }
    items = std::move(loaded);
    activeProfileId = root["activeProfileId"].asString();
    if (find(activeProfileId) == nullptr) {
        activeProfileId = items.empty() ? "" : items.front().id;
    }
    return true;
}

bool ProfileStore::load(std::string *warning) {
    std::string text;
    std::string err;
    if (!fs::exists(path()) && !fs::exists(path() + ".bak")) {
        items.clear();
        activeProfileId.clear();
        return true;
    }
    if (fs::readFile(path(), text, MAX_FILE_BYTES, &err) && deserialize(text, &err)) {
        return true;
    }
    std::string first = err;
    if (fs::readFile(path() + ".bak", text, MAX_FILE_BYTES, &err) && deserialize(text, &err)) {
        if (warning) {
            *warning = "profiles.json unreadable (" + first + "), restored the previous copy";
        }
        return true;
    }
    if (warning) {
        *warning = "profiles could not be loaded: " + first;
    }
    items.clear();
    activeProfileId.clear();
    return false;
}

bool ProfileStore::save(std::string *error) {
    if (!fs::ensureDir(dir, error)) {
        return false;
    }
    return fs::writeFileAtomic(path(), serialize(), error);
}

const iptv::Profile *ProfileStore::find(const std::string &id) const {
    for (const auto &p: items) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

const iptv::Profile *ProfileStore::active() const {
    return find(activeProfileId);
}

void ProfileStore::setActive(const std::string &id) {
    if (find(id) != nullptr) {
        activeProfileId = id;
    }
}

std::string ProfileStore::newId() {
    while (true) {
        std::string id = "p" + std::to_string(nextIdHint++);
        if (find(id) == nullptr) {
            return id;
        }
    }
}

std::string ProfileStore::upsert(iptv::Profile profile, int64_t now) {
    registerSecrets(profile);
    if (!profile.id.empty()) {
        for (auto &p: items) {
            if (p.id == profile.id) {
                profile.createdAt = p.createdAt;
                p = std::move(profile);
                return p.id;
            }
        }
    }
    profile.id = newId();
    profile.createdAt = now;
    items.push_back(std::move(profile));
    if (activeProfileId.empty()) {
        activeProfileId = items.back().id;
    }
    return items.back().id;
}

bool ProfileStore::remove(const std::string &id) {
    for (auto it = items.begin(); it != items.end(); ++it) {
        if (it->id == id) {
            items.erase(it);
            if (activeProfileId == id) {
                activeProfileId = items.empty() ? "" : items.front().id;
            }
            return true;
        }
    }
    return false;
}

void ProfileStore::setLastStatus(const std::string &id, const std::string &status, int64_t now) {
    for (auto &p: items) {
        if (p.id == id) {
            p.lastStatus = status;
            p.lastUsedAt = now;
        }
    }
}
