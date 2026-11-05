#include "profile_store.h"
#include "../core/json.h"
#include "../platform/fs.h"
#include "../platform/redact.h"

namespace {
    // 1: Xtream profiles only (Checkpoints 1 - 3.1)
    // 2: "sourceType" per profile ("xtream" / "m3u") + playlist fields; version 1 files load as Xtream.
    //    Additive: "stalker" sources (server + mac), which older builds skip.
    const int FORMAT_VERSION = 2;
    const size_t MAX_FILE_BYTES = 1024 * 1024;

    void registerSecrets(const iptv::Profile &p) {
        redact::addSecret(p.password);
        redact::addSecret(p.username);
        if (!p.playlistUrl.empty()) {
            redact::addUrl(p.playlistUrl);   // get.php?username=..&password=.., tokens, user:pass@
        }
        if (!p.mac.empty()) {
            redact::addMac(p.mac);           // every spelling a portal or a log line could carry
        }
    }

    bool complete(const iptv::Profile &p) {
        switch (p.type) {
            case iptv::SourceType::Xtream:
                return !p.server.empty();
            case iptv::SourceType::M3u:
                return !p.playlistUrl.empty();
            default:
                return !p.server.empty() && !p.mac.empty();
        }
    }
}

namespace {
    // one profile as stored (profiles.json and the Stalker sidecar use the same entries)
    json::Value entryJson(const iptv::Profile &p) {
        json::Value o = json::Value::makeObject();
        o.set("id", json::Value::makeString(p.id));
        o.set("sourceType", json::Value::makeString(iptv::sourceTypeKey(p.type)));
        o.set("name", json::Value::makeString(p.name));
        if (p.type == iptv::SourceType::Xtream) {
            o.set("server", json::Value::makeString(p.server));
            o.set("username", json::Value::makeString(p.username));
            o.set("password", json::Value::makeString(p.password));
        } else if (p.type == iptv::SourceType::Stalker) {
            o.set("server", json::Value::makeString(p.server));
            o.set("mac", json::Value::makeString(p.mac));
        } else {
            o.set("playlistUrl", json::Value::makeString(p.playlistUrl));
            o.set("userAgent", json::Value::makeString(p.userAgent));
            o.set("playlistChannels", json::Value::makeInt(p.playlistChannels));
        }
        o.set("createdAt", json::Value::makeInt(p.createdAt));
        o.set("lastUsedAt", json::Value::makeInt(p.lastUsedAt));
        o.set("lastStatus", json::Value::makeString(p.lastStatus));
        return o;
    }

    // false for entries to skip: unknown source type (a newer writer), no id, incomplete
    bool entryFrom(const json::Value &o, int64_t version, iptv::Profile &p) {
        p = iptv::Profile();
        p.id = o["id"].asString();
        // version 1 has no source type: every profile was an Xtream one
        if (version >= 2 && !iptv::sourceTypeFromKey(o["sourceType"].asString(), p.type)) {
            return false;   // a source type this version does not know
        }
        p.name = o["name"].asString();
        p.server = o["server"].asString();
        p.username = o["username"].asString();
        p.password = o["password"].asString();
        p.playlistUrl = o["playlistUrl"].asString();
        p.userAgent = o["userAgent"].asString();
        p.playlistChannels = (int) o["playlistChannels"].asInt(-1);
        p.mac = p.type == iptv::SourceType::Stalker ? o["mac"].asString() : "";
        p.createdAt = o["createdAt"].asInt(0);
        p.lastUsedAt = o["lastUsedAt"].asInt(0);
        p.lastStatus = o["lastStatus"].asString();
        return !p.id.empty() && complete(p);
    }

    // written by builds that know Stalker sources; an older build saving profiles.json drops it (see sidecarPath)
    const char *const MARKER = "stalkerSidecar";
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
        list.push(entryJson(p));
    }
    root.set("profiles", std::move(list));
    root.set(MARKER, json::Value::makeInt(1));
    return json::write(root);
}

std::string ProfileStore::sidecarPath() const {
    return fs::join(dir, "profiles.stalker.json");
}

std::string ProfileStore::serializeSidecar() const {
    json::Value root = json::Value::makeObject();
    root.set("version", json::Value::makeInt(1));
    json::Value list = json::Value::makeArray();
    for (const auto &p: items) {
        if (p.isStalker()) {
            list.push(entryJson(p));
        }
    }
    root.set("profiles", std::move(list));
    return json::write(root);
}

int ProfileStore::restoreFromSidecar(const std::string &text) {
    json::Value root;
    if (!json::parse(text, root) || !root.isObject() || root["version"].asInt(0) != 1) {
        return 0;
    }
    int restored = 0;
    for (const auto &o: root["profiles"].items()) {
        iptv::Profile p;
        if (!entryFrom(o, 2, p) || !p.isStalker()) {
            continue;
        }
        const iptv::Profile *same = find(p.id);
        if (same != nullptr) {
            if (same->isStalker()) {
                continue;   // still there
            }
            p.id = newId();   // the older build gave this id to a source it added: keep both
        }
        registerSecrets(p);
        items.push_back(std::move(p));
        restored++;
    }
    return restored;
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
        if (!entryFrom(o, version, p)) {
            continue;
        }
        registerSecrets(p);
        loaded.push_back(std::move(p));
    }
    items = std::move(loaded);
    loadedVersion = (int) version;
    writtenWithMarker = root.has(MARKER);
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
        keepOriginal(text);
        afterLoad(warning);
        return true;
    }
    std::string first = err;
    if (fs::readFile(path() + ".bak", text, MAX_FILE_BYTES, &err) && deserialize(text, &err)) {
        if (warning) {
            *warning = "profiles.json unreadable (" + first + "), restored the previous copy";
        }
        afterLoad(warning);
        return true;
    }
    if (warning) {
        *warning = "profiles could not be loaded: " + first;
    }
    items.clear();
    activeProfileId.clear();
    return false;
}

void ProfileStore::keepOriginal(const std::string &text) {
    // Upgrading from version 1: the original file is kept once, untouched, as profiles.v1.json. The next save
    // writes version 2, which older app versions cannot read; this copy keeps the Xtream logins recoverable.
    if (loadedVersion >= FORMAT_VERSION) {
        return;
    }
    std::string copy = fs::join(dir, "profiles.v1.json");
    if (!fs::exists(copy)) {
        fs::writeFileAtomic(copy, text);
    }
}

void ProfileStore::afterLoad(std::string *warning) {
    // profiles.json last written by an older build (no marker): it dropped the Stalker sources it could not read;
    // the sidecar written by this build still has them
    std::string side;
    if (writtenWithMarker || !fs::exists(sidecarPath()) || !fs::readFile(sidecarPath(), side, MAX_FILE_BYTES)) {
        return;
    }
    int restored = restoreFromSidecar(side);
    if (restored > 0 && warning) {
        *warning += std::string(warning->empty() ? "" : "; ") + std::to_string(restored)
                    + " Stalker source(s) restored (an older version saved the source list without them)";
    }
}

bool ProfileStore::save(std::string *error) {
    if (!fs::ensureDir(dir, error)) {
        return false;
    }
    // the sidecar first: profiles.json then names it (marker), so a crash in between never loses a source
    bool anyStalker = false;
    for (const auto &p: items) {
        anyStalker |= p.isStalker();
    }
    if (anyStalker) {
        if (!fs::writeFileAtomic(sidecarPath(), serializeSidecar(), error)) {
            return false;
        }
    } else if (fs::exists(sidecarPath())) {
        fs::removeFile(sidecarPath());   // every Stalker source was deleted here: nothing to bring back
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
