// Application settings persisted in <dataDir>/settings.json.

#ifndef PS4IPTV_STORAGE_SETTINGS_STORE_H
#define PS4IPTV_STORAGE_SETTINGS_STORE_H

#include <map>
#include <string>

#include "../iptv/sorting.h"
#include "../player/display.h"
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
    // video geometry defaults (the Options panel changes only the current playback). Position is never a
    // default: every playback starts centred.
    display::Aspect videoAspect = display::Aspect::Source;
    display::Crop videoCrop = display::Crop::None;
    int zoomPercent = 100;
    // offline downloads
    bool retryDownloads = true;          // network loss: retry automatically with backoff
    bool resumeDownloadsOnStart = true;  // interrupted downloads continue when the app starts
    // application UI language: "en" (default for every installation) or "tr". Never derived from the account,
    // provider or region. Files written before format version 2 had no language choice: they get English.
    std::string language = "en";
    // Movies / Series grid order, per profile: "<profileId>/movies" -> sort key ("az", "added_desc"...)
    std::map<std::string, std::string> sortOrders;

    // the geometry every playback starts with (centred)
    display::Geometry defaultGeometry() const {
        display::Geometry g;
        g.aspect = videoAspect;
        g.crop = videoCrop;
        g.zoom = display::clampZoom(zoomPercent);
        return g;
    }
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

    // grid order of Movies (ContentType::Movie) or Series for a profile (Provider order when never chosen)
    iptv::SortMode sortMode(const std::string &profileId, iptv::ContentType type) const;

    void setSortMode(const std::string &profileId, iptv::ContentType type, iptv::SortMode mode);

    static const char *formatName(StreamFormat f);

    static const char *stabilityKey(StabilityPreset p);

private:

    std::string dir;
    Settings settings;
};

#endif // PS4IPTV_STORAGE_SETTINGS_STORE_H
