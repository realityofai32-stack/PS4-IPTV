// Application settings persisted in <dataDir>/settings.json.

#ifndef PS4IPTV_STORAGE_SETTINGS_STORE_H
#define PS4IPTV_STORAGE_SETTINGS_STORE_H

#include <string>

#include "../player/stability.h"

// Live stream format (see stability::planFormats)
enum class StreamFormat {
    Auto,   // TS first (or what worked last this session), the other format once
    Ts,     // Prefer TS: TS first, HLS once if TS cannot be opened
    Hls     // Prefer HLS: HLS first, TS once if HLS cannot be opened
};

struct Settings {
    StreamFormat streamFormat = StreamFormat::Auto;
    StabilityPreset stability = StabilityPreset::Balanced;
    bool retryOnStall = true;
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

    static const char *stabilityKey(StabilityPreset p);

private:

    std::string dir;
    Settings settings;
};

#endif // PS4IPTV_STORAGE_SETTINGS_STORE_H
