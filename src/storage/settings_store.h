// Application settings persisted in <dataDir>/settings.json.

#ifndef PS4IPTV_STORAGE_SETTINGS_STORE_H
#define PS4IPTV_STORAGE_SETTINGS_STORE_H

#include <string>

#include "../player/stability.h"
#include "../player/tracks.h"

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
    // movies / episodes
    std::string audioLanguage;                    // ISO 639-2 code, "" = Auto (the file's default track)
    tracks::SubtitleMode subtitleMode = tracks::SubtitleMode::Auto;
    std::string subtitleLanguage;                 // "" = same as the audio preference
    int subtitleSize = 1;                         // 0 small, 1 medium, 2 large
    int subtitlePosition = 0;                     // 0 bottom, 1 raised
    bool subtitleShadow = false;
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
