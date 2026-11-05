// Sources (profiles) persisted in <dataDir>/profiles.json (versioned, crash-safe writes, .bak fallback).
// Credentials have to be stored on the console to log in; they are registered with the log redactor
// as soon as they are loaded or edited so they never reach a log line.
//
// Schema 2 adds "sourceType" ("xtream" / "m3u") and the playlist fields. Version 1 files (Xtream only) are
// migrated on load: every profile becomes sourceType xtream with all its fields unchanged (lossless); loading
// and saving again changes nothing (idempotent). The original version 1 file is kept once as
// profiles.v1.json.
//
// Stalker / MAG sources are an additive extension of schema 2: sourceType "stalker" with "server" (portal address)
// and "mac". The version stays 2 on purpose: Xtream and M3U entries are written exactly as before, and an older
// build reading the file skips the entries whose source type it does not know (v0.3.0 does: its own test reads a
// "stalker" entry and skips it) instead of rejecting the whole file. A version 3 file would make v0.3.0 reject the
// file and its .bak copy and start without any source (its next save would then overwrite them all).
//
// Downgrade round trip: v0.3.0 saves profiles.json at every sign-in and would drop the Stalker entries. This build
// therefore also writes them to profiles.stalker.json and marks profiles.json ("stalkerSidecar"); a profiles.json
// without the marker was written by an older build, and the Stalker sources it lost come back from the sidecar
// (an id the older build reused for a new source gets a new id).

#ifndef PS4IPTV_STORAGE_PROFILE_STORE_H
#define PS4IPTV_STORAGE_PROFILE_STORE_H

#include <string>
#include <vector>

#include "../iptv/models.h"

class ProfileStore {

public:

    explicit ProfileStore(std::string dataDir);

    // Loads profiles.json (or profiles.json.bak if the main file is unreadable/corrupt).
    // A missing file is not an error. `warning` describes any recovery that happened.
    bool load(std::string *warning = nullptr);

    bool save(std::string *error = nullptr);

    const std::vector<iptv::Profile> &profiles() const { return items; }

    const iptv::Profile *find(const std::string &id) const;

    const iptv::Profile *active() const;

    const std::string &activeId() const { return activeProfileId; }

    void setActive(const std::string &id);

    // Inserts (empty id -> new id) or replaces by id. Returns the stored id.
    std::string upsert(iptv::Profile profile, int64_t now);

    bool remove(const std::string &id);

    void setLastStatus(const std::string &id, const std::string &status, int64_t now);

    std::string path() const;

    // serialization (exposed for tests)
    std::string serialize() const;

    bool deserialize(const std::string &text, std::string *error);

    // schema version of the file last loaded (0 = none)
    int loadedFormat() const { return loadedVersion; }

    std::string sidecarPath() const;

    std::string serializeSidecar() const;

    // adds the sidecar's Stalker sources that are missing; returns how many (exposed for tests)
    int restoreFromSidecar(const std::string &text);

private:

    void afterLoad(std::string *warning);

    void keepOriginal(const std::string &text);

    std::string dir;
    std::vector<iptv::Profile> items;
    std::string activeProfileId;
    int nextIdHint = 1;
    int loadedVersion = 0;
    bool writtenWithMarker = false;   // the file loaded last was written by a Stalker-aware build

    std::string newId();
};

#endif // PS4IPTV_STORAGE_PROFILE_STORE_H
