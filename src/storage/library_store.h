// Per-profile favorites and watch history: <dataDir>/favorites.json, <dataDir>/history.json.

#ifndef PS4IPTV_STORAGE_LIBRARY_STORE_H
#define PS4IPTV_STORAGE_LIBRARY_STORE_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../iptv/models.h"

struct HistoryEntry {
    iptv::ContentType type = iptv::ContentType::Live;
    std::string id;           // stream id (live/movie) or episode id (series)
    std::string name;
    std::string icon;
    std::string extra;        // e.g. series id or container extension
    int64_t watchedAt = 0;
    double position = 0;      // seconds (VOD only)
    double duration = 0;
};

class LibraryStore {

public:

    static const size_t HISTORY_LIMIT = 100;

    explicit LibraryStore(std::string dataDir);

    bool load(std::string *warning = nullptr);

    // switches the active profile (favorites/history are per profile)
    void setProfile(const std::string &profileId);

    bool isFavorite(iptv::ContentType type, const std::string &id) const;

    // returns the new state
    bool toggleFavorite(iptv::ContentType type, const std::string &id);

    const std::set<std::string> &favorites(iptv::ContentType type) const;

    // most recent first; an existing entry with the same type+id moves to the front
    void addHistory(const HistoryEntry &entry);

    const std::vector<HistoryEntry> &history() const;

    void clearHistory();

    void removeProfile(const std::string &profileId);

    bool save(std::string *error = nullptr);

    std::string serializeFavorites() const;

    std::string serializeHistory() const;

    bool deserializeFavorites(const std::string &text, std::string *error);

    bool deserializeHistory(const std::string &text, std::string *error);

private:

    struct Favorites {
        std::set<std::string> byType[3];
    };

    std::string dir;
    std::string profile;
    std::map<std::string, Favorites> favs;
    std::map<std::string, std::vector<HistoryEntry>> hist;
};

#endif // PS4IPTV_STORAGE_LIBRARY_STORE_H
