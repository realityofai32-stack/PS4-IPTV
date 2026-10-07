// Application settings persisted in <dataDir>/settings.json.

#ifndef PS4IPTV_STORAGE_SETTINGS_STORE_H
#define PS4IPTV_STORAGE_SETTINGS_STORE_H

#include <string>

enum class StreamFormat {
    Auto,   // TS first, HLS fallback
    Ts,
    Hls
};

struct Settings {
    StreamFormat streamFormat = StreamFormat::Auto;
    bool resumeVod = true;
    bool autoPlayNextEpisode = false;
    bool showTechnicalInfo = false;
    bool loadImages = true;
    std::string language = "en";
};

class SettingsStore {

public:

    explicit SettingsStore(std::string dataDir);

    bool load(std::string *warning = nullptr);

    bool save(std::string *error = nullptr);

    Settings &get() { return settings; }

    const Settings &get() const { return settings; }

    std::string serialize() const;

    bool deserialize(const std::string &text, std::string *error);

    static const char *formatName(StreamFormat f);

private:

    std::string dir;
    Settings settings;
};

#endif // PS4IPTV_STORAGE_SETTINGS_STORE_H
