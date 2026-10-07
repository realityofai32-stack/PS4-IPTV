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
    std::string name;         // channel / movie title / episode title
    std::string icon;         // logo / poster / series cover
    std::string extra;        // free-form (unused by the app today)
    int64_t watchedAt = 0;
    double position = 0;      // seconds (movies and episodes only, never Live TV)
    double duration = 0;
    bool watched = false;     // reached the end (see progress::isWatched)
    std::string extension;    // container extension for the playback URL
    std::string seriesId;     // episodes
    std::string seriesName;
    int season = 0;
    int episode = 0;
};

// resume / watched rules (movies and episodes)
namespace progress {
    const double WATCHED_FRACTION = 0.93;   // ~93 % counts as watched (credits)
    const double MIN_RESUME_SECONDS = 30;   // less than this: start from the beginning

    bool isWatched(double position, double duration);

    // true when playback should offer to continue from `position`
    bool canResume(double position, double duration, bool watched);

    // where to resume: a few seconds before the saved position, for context
    double resumeFrom(double position);

    // 0..1 for progress bars (0 when unknown)
    double fraction(double position, double duration);
}

class LibraryStore {

public:

    static const size_t HISTORY_LIMIT = 200;

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

    // movie / episode progress of the active profile (nullptr when never played)
    const HistoryEntry *progressOf(iptv::ContentType type, const std::string &id) const;

    // records a progress update (moves the entry to the front, like addHistory)
    void updateProgress(const HistoryEntry &entry);

    // forget the position and watched state, keep the history entry
    void resetProgress(iptv::ContentType type, const std::string &id);

    // started, not finished movies and episodes, newest first; one entry (the latest episode) per series
    std::vector<const HistoryEntry *> continueWatching(size_t limit) const;

    // everything played recently, newest first; one entry per series
    std::vector<const HistoryEntry *> recentlyWatched(size_t limit) const;

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
