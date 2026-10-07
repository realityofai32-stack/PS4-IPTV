#include <algorithm>

#include "library_store.h"
#include "../core/json.h"
#include "../platform/fs.h"

namespace {
    const int FAVORITES_VERSION = 1;
    const int HISTORY_VERSION = 2;     // 1: Checkpoint 1/2 (progress inside the history list)
    const char *TYPE_KEYS[3] = {"live", "movie", "series"};
    const std::set<std::string> EMPTY_SET;
    const std::vector<HistoryEntry> EMPTY_HISTORY;

    bool readJson(const std::string &path, std::string &text) {
        std::string err;
        return fs::readFile(path, text, 8u * 1024 * 1024, &err);
    }

    iptv::ContentType typeOf(const std::string &key) {
        return key == "movie" ? iptv::ContentType::Movie : key == "series" ? iptv::ContentType::Series
                                                                           : iptv::ContentType::Live;
    }

    json::Value entryJson(const HistoryEntry &e) {
        json::Value o = json::Value::makeObject();
        o.set("type", json::Value::makeString(TYPE_KEYS[(int) e.type]));
        o.set("id", json::Value::makeString(e.id));
        o.set("name", json::Value::makeString(e.name));
        o.set("icon", json::Value::makeString(e.icon));
        if (!e.extra.empty()) {
            o.set("extra", json::Value::makeString(e.extra));
        }
        o.set("watchedAt", json::Value::makeInt(e.watchedAt));
        o.set("activity", json::Value::makeInt(e.activity));
        if (e.type != iptv::ContentType::Live) {
            o.set("position", json::Value::makeNumber(e.position));   // %.17g: exact
            o.set("duration", json::Value::makeNumber(e.duration));
            o.set("watched", json::Value::makeBool(e.watched));
            o.set("ext", json::Value::makeString(e.extension));
        }
        if (!e.seriesId.empty()) {
            o.set("seriesId", json::Value::makeString(e.seriesId));
            o.set("seriesName", json::Value::makeString(e.seriesName));
            o.set("season", json::Value::makeInt(e.season));
            o.set("episode", json::Value::makeInt(e.episode));
        }
        return o;
    }

    HistoryEntry entryOf(const json::Value &o) {
        HistoryEntry e;
        e.type = typeOf(o["type"].asString());
        e.id = o["id"].asString();
        e.name = o["name"].asString();
        e.icon = o["icon"].asString();
        e.extra = o["extra"].asString();
        e.watchedAt = o["watchedAt"].asInt(0);
        e.activity = o["activity"].asInt(0);
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
            e.watched = false;
        }
        if (e.position < 0 || e.position != e.position) {
            e.position = 0;
        }
        return e;
    }

    bool sameItem(const HistoryEntry &a, const HistoryEntry &b) {
        return a.type == b.type && a.id == b.id;
    }
}

namespace progress {
    bool isWatched(double position, double duration) {
        return duration > 0 && position >= duration * WATCHED_FRACTION;
    }

    bool inProgress(double position, double duration, bool watched) {
        return !watched && position >= MIN_CONTINUE_SECONDS && !isWatched(position, duration);
    }

    double fraction(double position, double duration) {
        if (duration <= 0 || position <= 0) {
            return 0;
        }
        return position >= duration ? 1.0 : position / duration;
    }
}

LibraryStore::LibraryStore(std::string dataDir) : dir(std::move(dataDir)) {}

std::string LibraryStore::progressKey(iptv::ContentType type, const std::string &id) {
    return std::string(type == iptv::ContentType::Movie ? "movie:" : type == iptv::ContentType::Series ? "episode:"
                                                                                                       : "live:") + id;
}

void LibraryStore::setProfile(const std::string &profileId) {
    profile = profileId;
    gen++;
}

const LibraryStore::ProfileData *LibraryStore::current() const {
    auto it = profiles.find(profile);
    return it == profiles.end() ? nullptr : &it->second;
}

bool LibraryStore::isFavorite(iptv::ContentType type, const std::string &id) const {
    auto it = favs.find(profile);
    return it != favs.end() && it->second.byType[(int) type].count(id) > 0;
}

bool LibraryStore::toggleFavorite(iptv::ContentType type, const std::string &id) {
    gen++;
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

void LibraryStore::pushHistory(ProfileData &d, const HistoryEntry &entry) {
    auto &list = d.history;
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (sameItem(*it, entry)) {
            list.erase(it);
            break;
        }
    }
    list.insert(list.begin(), entry);
    // Live TV keeps at most LIVE_HISTORY_LIMIT entries: zapping never pushes movies/episodes out
    size_t live = 0;
    for (auto it = list.begin(); it != list.end();) {
        if (it->type == iptv::ContentType::Live && ++live > LIVE_HISTORY_LIMIT) {
            it = list.erase(it);
        } else {
            ++it;
        }
    }
    if (list.size() > HISTORY_LIMIT) {
        list.resize(HISTORY_LIMIT);
    }
}

void LibraryStore::addHistory(const HistoryEntry &entry) {
    ProfileData &d = data();
    HistoryEntry e = entry;
    e.activity = ++d.seq;
    if (e.type == iptv::ContentType::Live) {
        e.position = 0;
        e.duration = 0;
        e.watched = false;
    }
    pushHistory(d, e);
    gen++;
}

const std::vector<HistoryEntry> &LibraryStore::history() const {
    const ProfileData *d = current();
    return d ? d->history : EMPTY_HISTORY;
}

void LibraryStore::clearHistory() {
    data().history.clear();
    gen++;
}

const HistoryEntry *LibraryStore::progressOf(iptv::ContentType type, const std::string &id) const {
    const ProfileData *d = current();
    if (d == nullptr || type == iptv::ContentType::Live) {
        return nullptr;
    }
    auto it = d->progress.find(progressKey(type, id));
    return it == d->progress.end() ? nullptr : &it->second;
}

void LibraryStore::updateProgress(const HistoryEntry &entry) {
    if (entry.type == iptv::ContentType::Live || entry.id.empty()) {
        addHistory(entry);
        return;
    }
    ProfileData &d = data();
    HistoryEntry e = entry;
    e.activity = ++d.seq;
    d.progress[progressKey(e.type, e.id)] = e;
    if (d.progress.size() > PROGRESS_LIMIT) {
        // forget the least recently played position
        auto oldest = d.progress.begin();
        for (auto it = d.progress.begin(); it != d.progress.end(); ++it) {
            if (it->second.activity < oldest->second.activity) {
                oldest = it;
            }
        }
        d.progress.erase(oldest);
    }
    pushHistory(d, e);
    gen++;
}

void LibraryStore::resetProgress(iptv::ContentType type, const std::string &id) {
    ProfileData &d = data();
    auto it = d.progress.find(progressKey(type, id));
    if (it != d.progress.end()) {
        it->second.position = 0;
        it->second.watched = false;
    }
    for (auto &e: d.history) {
        if (e.type == type && e.id == id) {
            e.position = 0;
            e.watched = false;
        }
    }
    gen++;
}

std::vector<const HistoryEntry *> LibraryStore::continueWatching(size_t limit) const {
    std::vector<const HistoryEntry *> eligible;
    const ProfileData *d = current();
    if (d == nullptr) {
        return eligible;
    }
    for (const auto &p: d->progress) {
        const HistoryEntry &e = p.second;
        if (e.type != iptv::ContentType::Live && progress::inProgress(e.position, e.duration, e.watched)) {
            eligible.push_back(&e);
        }
    }
    std::sort(eligible.begin(), eligible.end(), [](const HistoryEntry *a, const HistoryEntry *b) {
        return a->activity > b->activity;
    });
    std::vector<const HistoryEntry *> out;
    std::set<std::string> seriesSeen;
    for (const HistoryEntry *e: eligible) {
        if (out.size() >= limit) {
            break;
        }
        // one card per series: its most recently played unfinished episode
        if (e->type == iptv::ContentType::Series && !e->seriesId.empty() && !seriesSeen.insert(e->seriesId).second) {
            continue;
        }
        out.push_back(e);
    }
    return out;
}

std::vector<const HistoryEntry *> LibraryStore::recentlyWatched(size_t limit, size_t maxLive) const {
    std::vector<const HistoryEntry *> candidates;
    std::set<std::string> seriesSeen;
    for (const auto &e: history()) {
        if (e.type == iptv::ContentType::Series && !e.seriesId.empty() && !seriesSeen.insert(e.seriesId).second) {
            continue;
        }
        candidates.push_back(&e);
    }
    // newest first; Live TV beyond maxLive only fills places movies/episodes leave empty
    std::vector<bool> take(candidates.size(), false);
    size_t taken = 0;
    size_t live = 0;
    for (size_t i = 0; i < candidates.size() && taken < limit; i++) {
        bool isLive = candidates[i]->type == iptv::ContentType::Live;
        if (isLive && live >= maxLive) {
            continue;
        }
        live += isLive;
        take[i] = true;
        taken++;
    }
    for (size_t i = 0; i < candidates.size() && taken < limit; i++) {
        if (!take[i]) {
            take[i] = true;
            taken++;
        }
    }
    std::vector<const HistoryEntry *> out;
    for (size_t i = 0; i < candidates.size(); i++) {
        if (take[i]) {
            out.push_back(candidates[i]);
        }
    }
    return out;
}

std::vector<const HistoryEntry *> LibraryStore::seriesProgress(const std::string &seriesId) const {
    std::vector<const HistoryEntry *> out;
    const ProfileData *d = current();
    if (d == nullptr || seriesId.empty()) {
        return out;
    }
    for (const auto &p: d->progress) {
        if (p.second.type == iptv::ContentType::Series && p.second.seriesId == seriesId) {
            out.push_back(&p.second);
        }
    }
    std::sort(out.begin(), out.end(), [](const HistoryEntry *a, const HistoryEntry *b) {
        return a->activity > b->activity;
    });
    return out;
}

std::unordered_map<std::string, int64_t> LibraryStore::lastActivity(iptv::ContentType type) const {
    std::unordered_map<std::string, int64_t> out;
    const ProfileData *d = current();
    if (d == nullptr) {
        return out;
    }
    auto note = [&out](const std::string &key, int64_t activity) {
        if (key.empty()) {
            return;
        }
        int64_t &v = out[key];
        v = std::max(v, activity);
    };
    for (const auto &p: d->progress) {
        const HistoryEntry &e = p.second;
        if (e.type == type) {
            note(type == iptv::ContentType::Series ? e.seriesId : e.id, e.activity);
        }
    }
    for (const auto &e: d->history) {
        if (e.type == type) {
            note(type == iptv::ContentType::Series ? e.seriesId : e.id, e.activity);
        }
    }
    return out;
}

void LibraryStore::removeProfile(const std::string &profileId) {
    favs.erase(profileId);
    profiles.erase(profileId);
    gen++;
}

std::string LibraryStore::serializeFavorites() const {
    json::Value root = json::Value::makeObject();
    root.set("version", json::Value::makeInt(FAVORITES_VERSION));
    json::Value profilesJson = json::Value::makeObject();
    for (const auto &p: favs) {
        json::Value o = json::Value::makeObject();
        for (int t = 0; t < 3; t++) {
            json::Value arr = json::Value::makeArray();
            for (const auto &id: p.second.byType[t]) {
                arr.push(json::Value::makeString(id));
            }
            o.set(TYPE_KEYS[t], std::move(arr));
        }
        profilesJson.set(p.first, std::move(o));
    }
    root.set("profiles", std::move(profilesJson));
    return json::write(root, false);
}

std::string LibraryStore::serializeHistory() const {
    json::Value root = json::Value::makeObject();
    root.set("version", json::Value::makeInt(HISTORY_VERSION));
    json::Value profilesJson = json::Value::makeObject();
    for (const auto &p: profiles) {
        json::Value o = json::Value::makeObject();
        o.set("seq", json::Value::makeInt(p.second.seq));
        json::Value hist = json::Value::makeArray();
        for (const auto &e: p.second.history) {
            hist.push(entryJson(e));
        }
        o.set("history", std::move(hist));
        // progress in activity order: the file is stable and diff-able
        std::vector<const HistoryEntry *> sorted;
        for (const auto &e: p.second.progress) {
            sorted.push_back(&e.second);
        }
        std::sort(sorted.begin(), sorted.end(), [](const HistoryEntry *a, const HistoryEntry *b) {
            return a->activity > b->activity;
        });
        json::Value prog = json::Value::makeArray();
        for (const HistoryEntry *e: sorted) {
            prog.push(entryJson(*e));
        }
        o.set("progress", std::move(prog));
        profilesJson.set(p.first, std::move(o));
    }
    root.set("profiles", std::move(profilesJson));
    return json::write(root, false);
}

bool LibraryStore::deserializeFavorites(const std::string &text, std::string *error) {
    json::Value root;
    std::string err;
    if (!json::parse(text, root, &err) || root["version"].asInt(0) != FAVORITES_VERSION) {
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
    gen++;
    return true;
}

bool LibraryStore::deserializeHistory(const std::string &text, std::string *error) {
    json::Value root;
    std::string err;
    int64_t version = 0;
    if (json::parse(text, root, &err)) {
        version = root["version"].asInt(0);
    }
    if (version != 1 && version != HISTORY_VERSION) {
        if (error) {
            *error = "history.json unreadable: " + (err.empty() ? std::string("bad version") : err);
        }
        return false;
    }
    std::map<std::string, ProfileData> loaded;
    for (const auto &p: root["profiles"].members()) {
        ProfileData d;
        if (version == 1) {
            // schema 1: one list, newest first, positions inside: history + progress of movies/episodes
            const auto &items = p.second.items();
            int64_t activity = (int64_t) items.size();
            for (const auto &o: items) {
                HistoryEntry e = entryOf(o);
                if (e.id.empty()) {
                    continue;
                }
                e.activity = activity--;
                if (d.history.size() < HISTORY_LIMIT) {
                    d.history.push_back(e);
                }
                if (e.type != iptv::ContentType::Live && (e.position > 0 || e.watched)) {
                    d.progress.emplace(progressKey(e.type, e.id), e);
                }
            }
            d.seq = (int64_t) items.size();
        } else {
            d.seq = p.second["seq"].asInt(0);
            for (const auto &o: p.second["history"].items()) {
                HistoryEntry e = entryOf(o);
                if (!e.id.empty() && d.history.size() < HISTORY_LIMIT) {
                    d.history.push_back(e);
                }
            }
            for (const auto &o: p.second["progress"].items()) {
                HistoryEntry e = entryOf(o);
                if (!e.id.empty() && e.type != iptv::ContentType::Live && d.progress.size() < PROGRESS_LIMIT) {
                    d.progress.emplace(progressKey(e.type, e.id), e);
                }
            }
        }
        for (const auto &e: d.history) {
            d.seq = std::max(d.seq, e.activity);
        }
        for (const auto &e: d.progress) {
            d.seq = std::max(d.seq, e.second.activity);
        }
        loaded[p.first] = std::move(d);
    }
    profiles = std::move(loaded);
    gen++;
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
