// Per-profile favorites, watch history and playback progress: <dataDir>/favorites.json, <dataDir>/history.json.
//
// history.json schema 2 keeps two separate things per profile:
//   - progress: the exact position of every movie / episode played (key: type + stream id / episode id),
//     the source of Continue Watching and of every resume. Live TV never has progress.
//   - history: what was played recently (Live, movies, episodes) for Recently Watched. Live TV zapping can
//     only push out older Live entries, never movie/episode progress.
// Schema 1 files (history with positions inside) are migrated on load.

#ifndef PS4IPTV_STORAGE_LIBRARY_STORE_H
#define PS4IPTV_STORAGE_LIBRARY_STORE_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "../iptv/models.h"

struct HistoryEntry {
    iptv::ContentType type = iptv::ContentType::Live;   // Series = an episode
    std::string id;           // stream id (live/movie) or episode id (series)
    std::string name;         // channel / movie title / episode title
    std::string icon;         // logo / poster / series cover
    std::string extra;        // free-form (unused by the app today)
    int64_t watchedAt = 0;    // unix time of the last playback activity
    int64_t activity = 0;     // ordering of playback activity in this profile (higher = more recent)
    double position = 0;      // seconds, millisecond precision (movies and episodes only, never Live TV)
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
    const double WATCHED_FRACTION = 0.93;    // ~93 % counts as watched (end credits)
    const double MIN_CONTINUE_SECONDS = 5;   // playback has genuinely started

    bool isWatched(double position, double duration);

    // started (>= 5 s) and not finished: listed in Continue Watching, resumed at exactly `position`
    bool inProgress(double position, double duration, bool watched);

    // 0..1 for progress bars (0 when unknown)
    double fraction(double position, double duration);
}

class LibraryStore {

public:

    static const size_t HISTORY_LIMIT = 200;        // Recently Watched entries per profile, all types
    static const size_t LIVE_HISTORY_LIMIT = 100;   // of which Live TV channels at most
    static const size_t PROGRESS_LIMIT = 2000;      // remembered movie / episode positions per profile

    explicit LibraryStore(std::string dataDir);

    bool load(std::string *warning = nullptr);

    // switches the active profile (favorites/history/progress are per profile)
    void setProfile(const std::string &profileId);

    const std::string &profileId() const { return profile; }

    bool isFavorite(iptv::ContentType type, const std::string &id) const;

    // returns the new state
    bool toggleFavorite(iptv::ContentType type, const std::string &id);

    const std::set<std::string> &favorites(iptv::ContentType type) const;

    // Recently Watched only (Live TV); most recent first, an entry with the same type+id moves to the front
    void addHistory(const HistoryEntry &entry);

    const std::vector<HistoryEntry> &history() const;

    void clearHistory();

    // movie / episode progress of the active profile (nullptr when never played)
    const HistoryEntry *progressOf(iptv::ContentType type, const std::string &id) const;

    // records a movie / episode position: progress + Recently Watched, newest activity
    void updateProgress(const HistoryEntry &entry);

    // forget the position and watched state, keep the history entry
    void resetProgress(iptv::ContentType type, const std::string &id);

    // in-progress movies and episodes, most recent playback activity first; per series only its most
    // recently played unfinished episode
    std::vector<const HistoryEntry *> continueWatching(size_t limit) const;

    // everything played recently, newest first, one entry per series. At most maxLive Live TV channels
    // while movies/episodes are available to fill the row.
    std::vector<const HistoryEntry *> recentlyWatched(size_t limit, size_t maxLive = (size_t) -1) const;

    // progress entries of the episodes of one series
    std::vector<const HistoryEntry *> seriesProgress(const std::string &seriesId) const;

    // last playback activity per movie id (Movie) or per series id (Series), for "Recently watched" sorting
    std::unordered_map<std::string, int64_t> lastActivity(iptv::ContentType type) const;

    void removeProfile(const std::string &profileId);

    bool save(std::string *error = nullptr);

    // changes whenever favorites, history or progress change (screens rebuild their rows)
    unsigned generation() const { return gen; }

    std::string serializeFavorites() const;

    std::string serializeHistory() const;

    bool deserializeFavorites(const std::string &text, std::string *error);

    bool deserializeHistory(const std::string &text, std::string *error);

    static std::string progressKey(iptv::ContentType type, const std::string &id);

private:

    struct Favorites {
        std::set<std::string> byType[3];
    };

    struct ProfileData {
        std::vector<HistoryEntry> history;
        std::unordered_map<std::string, HistoryEntry> progress;
        int64_t seq = 0;   // last activity number handed out
    };

    ProfileData &data() { return profiles[profile]; }

    const ProfileData *current() const;

    void pushHistory(ProfileData &d, const HistoryEntry &entry);

    std::string dir;
    std::string profile;
    std::map<std::string, Favorites> favs;
    std::map<std::string, ProfileData> profiles;
    unsigned gen = 0;
};

#endif // PS4IPTV_STORAGE_LIBRARY_STORE_H
