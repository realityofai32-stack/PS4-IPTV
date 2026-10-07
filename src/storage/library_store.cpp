#include "library_store.h"
#include "../core/json.h"
#include "../platform/fs.h"

namespace {
    const int FORMAT_VERSION = 1;
    const char *TYPE_KEYS[3] = {"live", "movie", "series"};
    const std::set<std::string> EMPTY_SET;
    const std::vector<HistoryEntry> EMPTY_HISTORY;

    bool readJson(const std::string &path, std::string &text) {
        std::string err;
        return fs::readFile(path, text, 8u * 1024 * 1024, &err);
    }
}

namespace progress {
    bool isWatched(double position, double duration) {
        return duration > 0 && position >= duration * WATCHED_FRACTION;
    }

    bool canResume(double position, double duration, bool watched) {
        return !watched && position >= MIN_RESUME_SECONDS && !isWatched(position, duration);
    }

    double resumeFrom(double position) {
        return position > 10 ? position - 5 : 0;
    }

    double fraction(double position, double duration) {
        if (duration <= 0 || position <= 0) {
            return 0;
        }
        return position >= duration ? 1.0 : position / duration;
    }
}

LibraryStore::LibraryStore(std::string dataDir) : dir(std::move(dataDir)) {}

void LibraryStore::setProfile(const std::string &profileId) {
    profile = profileId;
}

bool LibraryStore::isFavorite(iptv::ContentType type, const std::string &id) const {
    auto it = favs.find(profile);
    return it != favs.end() && it->second.byType[(int) type].count(id) > 0;
}

bool LibraryStore::toggleFavorite(iptv::ContentType type, const std::string &id) {
    auto &set = favs[profile].byType[(int) type];
    if (set.erase(id) > 0) {
        return false;
    }
    set.insert(id);
    return true;
}

const std::set<std::string> &LibraryStore::favorites(iptv::ContentType type) const {
    auto it = favs.find(profile);
    return it == favs.end() ? EMPTY_SET : it->second.byType[(int) type];
}

void LibraryStore::addHistory(const HistoryEntry &entry) {
    auto &list = hist[profile];
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->type == entry.type && it->id == entry.id) {
            list.erase(it);
            break;
        }
    }
    list.insert(list.begin(), entry);
    if (list.size() > HISTORY_LIMIT) {
        list.resize(HISTORY_LIMIT);
    }
}

const std::vector<HistoryEntry> &LibraryStore::history() const {
    auto it = hist.find(profile);
    return it == hist.end() ? EMPTY_HISTORY : it->second;
}

void LibraryStore::clearHistory() {
    hist[profile].clear();
}

const HistoryEntry *LibraryStore::progressOf(iptv::ContentType type, const std::string &id) const {
    for (const auto &e: history()) {
        if (e.type == type && e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

void LibraryStore::updateProgress(const HistoryEntry &entry) {
    addHistory(entry);
}

void LibraryStore::resetProgress(iptv::ContentType type, const std::string &id) {
    for (auto &e: hist[profile]) {
        if (e.type == type && e.id == id) {
            e.position = 0;
            e.watched = false;
        }
    }
}

std::vector<const HistoryEntry *> LibraryStore::continueWatching(size_t limit) const {
    std::vector<const HistoryEntry *> out;
    std::set<std::string> seriesSeen;
    for (const auto &e: history()) {
        if (out.size() >= limit) {
            break;
        }
        if (e.type == iptv::ContentType::Live) {
            continue;
        }
        if (e.type == iptv::ContentType::Series && !e.seriesId.empty()) {
            // only the most recent episode of a series decides (a finished episode hides the series)
            if (!seriesSeen.insert(e.seriesId).second) {
                continue;
            }
        }
        if (progress::canResume(e.position, e.duration, e.watched)) {
            out.push_back(&e);
        }
    }
    return out;
}

std::vector<const HistoryEntry *> LibraryStore::recentlyWatched(size_t limit) const {
    std::vector<const HistoryEntry *> out;
    std::set<std::string> seriesSeen;
    for (const auto &e: history()) {
        if (out.size() >= limit) {
            break;
        }
        if (e.type == iptv::ContentType::Series && !e.seriesId.empty() && !seriesSeen.insert(e.seriesId).second) {
            continue;
        }
        out.push_back(&e);
    }
    return out;
}

void LibraryStore::removeProfile(const std::string &profileId) {
    favs.erase(profileId);
    hist.erase(profileId);
}

std::string LibraryStore::serializeFavorites() const {
    json::Value root = json::Value::makeObject();
    root.set("version", json::Value::makeInt(FORMAT_VERSION));
    json::Value profiles = json::Value::makeObject();
    for (const auto &p: favs) {
        json::Value o = json::Value::makeObject();
        for (int t = 0; t < 3; t++) {
            json::Value arr = json::Value::makeArray();
            for (const auto &id: p.second.byType[t]) {
                arr.push(json::Value::makeString(id));
            }
            o.set(TYPE_KEYS[t], std::move(arr));
        }
        profiles.set(p.first, std::move(o));
    }
    root.set("profiles", std::move(profiles));
    return json::write(root, false);
}

std::string LibraryStore::serializeHistory() const {
    json::Value root = json::Value::makeObject();
    root.set("version", json::Value::makeInt(FORMAT_VERSION));
    json::Value profiles = json::Value::makeObject();
    for (const auto &p: hist) {
        json::Value arr = json::Value::makeArray();
        for (const auto &e: p.second) {
            json::Value o = json::Value::makeObject();
            o.set("type", json::Value::makeString(TYPE_KEYS[(int) e.type]));
            o.set("id", json::Value::makeString(e.id));
            o.set("name", json::Value::makeString(e.name));
            o.set("icon", json::Value::makeString(e.icon));
            o.set("extra", json::Value::makeString(e.extra));
            o.set("watchedAt", json::Value::makeInt(e.watchedAt));
            o.set("position", json::Value::makeNumber(e.position));
            o.set("duration", json::Value::makeNumber(e.duration));
            if (e.type != iptv::ContentType::Live) {
                o.set("watched", json::Value::makeBool(e.watched));
                o.set("ext", json::Value::makeString(e.extension));
            }
            if (!e.seriesId.empty()) {
                o.set("seriesId", json::Value::makeString(e.seriesId));
                o.set("seriesName", json::Value::makeString(e.seriesName));
                o.set("season", json::Value::makeInt(e.season));
                o.set("episode", json::Value::makeInt(e.episode));
            }
            arr.push(std::move(o));
        }
        profiles.set(p.first, std::move(arr));
    }
    root.set("profiles", std::move(profiles));
    return json::write(root, false);
}

bool LibraryStore::deserializeFavorites(const std::string &text, std::string *error) {
    json::Value root;
    std::string err;
    if (!json::parse(text, root, &err) || root["version"].asInt(0) != FORMAT_VERSION) {
        if (error) {
            *error = "favorites.json unreadable: " + (err.empty() ? std::string("bad version") : err);
        }
        return false;
    }
    favs.clear();
    for (const auto &p: root["profiles"].members()) {
        Favorites f;
        for (int t = 0; t < 3; t++) {
            for (const auto &id: p.second[TYPE_KEYS[t]].items()) {
                std::string s = id.asString();
                if (!s.empty()) {
                    f.byType[t].insert(s);
                }
            }
        }
        favs[p.first] = f;
    }
    return true;
}

bool LibraryStore::deserializeHistory(const std::string &text, std::string *error) {
    json::Value root;
    std::string err;
    if (!json::parse(text, root, &err) || root["version"].asInt(0) != FORMAT_VERSION) {
        if (error) {
            *error = "history.json unreadable: " + (err.empty() ? std::string("bad version") : err);
        }
        return false;
    }
    hist.clear();
    for (const auto &p: root["profiles"].members()) {
        std::vector<HistoryEntry> list;
        for (const auto &o: p.second.items()) {
            HistoryEntry e;
            std::string type = o["type"].asString();
            e.type = type == "movie" ? iptv::ContentType::Movie : type == "series" ? iptv::ContentType::Series
                                                                                   : iptv::ContentType::Live;
            e.id = o["id"].asString();
            e.name = o["name"].asString();
            e.icon = o["icon"].asString();
            e.extra = o["extra"].asString();
            e.watchedAt = o["watchedAt"].asInt(0);
            e.position = o["position"].asDouble(0);
            e.duration = o["duration"].asDouble(0);
            e.watched = o["watched"].asBool(false);
            e.extension = o["ext"].asString();
            e.seriesId = o["seriesId"].asString();
            e.seriesName = o["seriesName"].asString();
            e.season = (int) o["season"].asInt(0);
            e.episode = (int) o["episode"].asInt(0);
            if (e.type == iptv::ContentType::Live) {
                e.position = 0;   // resume never applies to Live TV
                e.duration = 0;
            }
            if (!e.id.empty() && list.size() < HISTORY_LIMIT) {
                list.push_back(e);
            }
        }
        hist[p.first] = list;
    }
    return true;
}

bool LibraryStore::load(std::string *warning) {
    std::string text, err;
    for (int i = 0; i < 2; i++) {
        std::string base = fs::join(dir, i == 0 ? "favorites.json" : "history.json");
        bool loaded = false;
        for (const std::string &path: {base, base + ".bak"}) {
            if (!fs::exists(path)) {
                continue;
            }
            if (readJson(path, text) && (i == 0 ? deserializeFavorites(text, &err) : deserializeHistory(text, &err))) {
                loaded = true;
                if (path != base && warning) {
                    *warning += base + " unreadable, restored the previous copy. ";
                }
                break;
            }
        }
        if (!loaded && fs::exists(base) && warning) {
            *warning += err + " ";
        }
    }
    return true;
}

bool LibraryStore::save(std::string *error) {
    if (!fs::ensureDir(dir, error)) {
        return false;
    }
    return fs::writeFileAtomic(fs::join(dir, "favorites.json"), serializeFavorites(), error)
           && fs::writeFileAtomic(fs::join(dir, "history.json"), serializeHistory(), error);
}
