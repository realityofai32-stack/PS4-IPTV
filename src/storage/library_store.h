// Per-profile favorites, watch history and playback progress: <dataDir>/favorites.json, <dataDir>/history.json.
//
// history.json schema 2 keeps two separate things per profile:
//   - progress: the exact position of every movie / episode played (key: type + stream id / episode id),
//     the source of Continue Watching and of every resume. Live TV never has progress.
//   - history: what was played recently (Live, movies, episodes) for Recently Watched. Live TV zapping can
//     only push out older Live entries, never movie/episode progress.
//   - dismissed: movies / episodes the user removed from Continue Watching (same key as progress, with the
//     position at that moment). Removing hides the card only: the position, history, favorites and downloads
//     stay. The item returns once playback moves NEW_PROGRESS_SECONDS away from that position; it is
//     forgotten when the progress is reset, completed or evicted. Optional in the file (older versions ignore it).
//   - upNext (on a progress entry, optional in the file): the series' Continue Watching pointer to the episode that
//     comes next. Set (position 0) when the episode before it entered its last COMPLETION_REMAINING seconds
//     (markUpNext), so the series stays in the row after the user exits during the credits, at the end of file,
//     with auto-play off or after "Watch credits". Opening that episode keeps the pointer until real progress
//     (MIN_CONTINUE_SECONDS) or the end replaces it. Older versions ignore the field (a position-0 entry).
// Schema 1 files (history with positions inside) are migrated on load.

#ifndef PS4IPTV_STORAGE_LIBRARY_STORE_H
#define PS4IPTV_STORAGE_LIBRARY_STORE_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
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
    bool upNext = false;      // episodes: Continue Watching points here next (position 0), see markUpNext
};

// resume / watched rules (movies and episodes)
namespace progress {
    const double WATCHED_FRACTION = 0.93;    // ~93 % counts as watched (end credits)
    const double MIN_CONTINUE_SECONDS = 5;   // playback has genuinely started
    // a removed Continue Watching item comes back once playback moved this far from where it was removed
    const double NEW_PROGRESS_SECONDS = 60;
    // Series: an episode is complete for Continue Watching once this little of it remains (credits), whatever
    // happens afterwards; the next episode then becomes the series' Continue Watching entry (markUpNext). Independent
    // of the visible Next Episode card (app/auto_next.h CARD_SECONDS).
    const double COMPLETION_REMAINING = 45;

    // movies: ~93 % (end credits)
    bool isWatched(double position, double duration);

    // by content type: a series episode is complete only in its last COMPLETION_REMAINING seconds (exiting with more
    // left keeps it in progress, resumed at that exact position); movies at WATCHED_FRACTION
    bool isWatched(double position, double duration, iptv::ContentType type);

    bool isWatched(const HistoryEntry &e);

    // started (>= 5 s) and not finished: listed in Continue Watching, resumed at exactly `position` (movie rule)
    bool inProgress(double position, double duration, bool watched);

    // the same with the entry's type (episodes: the 45 s rule)
    bool inProgress(const HistoryEntry &e);

    // 0..1 for progress bars (0 when unknown)
    double fraction(double position, double duration);

    // listed in Continue Watching: in progress, or a series' next episode waiting to be started (upNext)
    bool continueEligible(const HistoryEntry &e);
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

    // Continue Watching moves on to the series' next episode: `next` (an episode with seriesId) gets a progress
    // entry at position 0 marked upNext, the newest activity of the profile; every other upNext pointer of that
    // series is cleared. It is not added to Recently Watched (nothing was played) and ends a removal of it.
    void markUpNext(const HistoryEntry &next);

    // the series' current Continue Watching entry: its most recently active eligible episode (nullptr: none)
    const HistoryEntry *seriesContinueEntry(const std::string &seriesId) const;

    // forget the position and watched state, keep the history entry (also ends a Continue Watching removal)
    void resetProgress(iptv::ContentType type, const std::string &id);

    // "Remove from Continue Watching": hides the movie / episode from the row, keeps its position (Resume still
    // works), history, favorite and download. False when it is not in progress (nothing to hide).
    bool dismissFromContinueWatching(iptv::ContentType type, const std::string &id);

    // removed from Continue Watching and not played on since
    bool isDismissed(iptv::ContentType type, const std::string &id) const;

    // in-progress movies and episodes (and series' upNext episodes) that were not removed from the row, most recent
    // playback activity first; per series only its most recent eligible (and not removed) episode
    std::vector<const HistoryEntry *> continueWatching(size_t limit) const;

    // everything played recently, newest first, one entry per series. At most maxLive Live TV channels
    // while movies/episodes are available to fill the row.
    std::vector<const HistoryEntry *> recentlyWatched(size_t limit, size_t maxLive = (size_t) -1) const;

    // progress entries of the episodes of one series
    std::vector<const HistoryEntry *> seriesProgress(const std::string &seriesId) const;

    // last playback activity per movie id (Movie) or per series id (Series), for "Recently watched" sorting
    std::unordered_map<std::string, int64_t> lastActivity(iptv::ContentType type) const;

    void removeProfile(const std::string &profileId);

    // writes what changed since the last write (a file whose content is unchanged is not rewritten), at once
    bool save(std::string *error = nullptr);

    // Saving in two steps, so periodic progress saves during playback do not write + fsync on the main thread:
    // snapshot() (main thread) serializes what changed since the last snapshot; write() (any thread) writes it.
    // Writes are serialized, and a file is never overwritten by an older snapshot than the one already written.
    struct Snapshot {
        std::string favorites;            // content to write when writeFavorites
        std::string history;              // content to write when writeHistory
        bool writeFavorites = false;
        bool writeHistory = false;
        unsigned long serial = 0;
    };

    // false: nothing changed since the last snapshot
    bool snapshot(Snapshot &out);

    bool write(const Snapshot &snapshot, std::string *error = nullptr);

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
        std::map<std::string, double> dismissed;   // progress key -> position when removed from Continue Watching
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

    // save bookkeeping: what the last snapshot contained (main thread), what was written (writer, under the lock)
    std::string lastFavorites;
    std::string lastHistory;
    bool snapshotTaken = false;
    unsigned long nextSerial = 0;
    struct WriteState {
        std::mutex lock;
        unsigned long favoritesSerial = 0;
        unsigned long historySerial = 0;
        std::atomic<bool> favoritesFailed{false};   // the next snapshot writes the file again
        std::atomic<bool> historyFailed{false};
    };
    std::shared_ptr<WriteState> writes = std::make_shared<WriteState>();
};

#endif // PS4IPTV_STORAGE_LIBRARY_STORE_H
