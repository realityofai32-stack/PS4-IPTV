// Xtream profiles persisted in <dataDir>/profiles.json (versioned, crash-safe writes, .bak fallback).
// Credentials have to be stored on the console to log in; they are registered with the log redactor
// as soon as they are loaded or edited so they never reach a log line.

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

private:

    std::string dir;
    std::vector<iptv::Profile> items;
    std::string activeProfileId;
    int nextIdHint = 1;

    std::string newId();
};

#endif // PS4IPTV_STORAGE_PROFILE_STORE_H
