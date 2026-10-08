#include "settings_store.h"
#include "../core/json.h"
#include "../platform/fs.h"

namespace {
    const int FORMAT_VERSION = 2;   // 2: language choice, display mode / zoom, downloads

    // language codes come back from the file: letters only, short
    std::string cleanCode(const std::string &code) {
        std::string out;
        for (char c: code) {
            if (c >= 'a' && c <= 'z') {
                out += c;
            } else if (c >= 'A' && c <= 'Z') {
                out += (char) (c - 'A' + 'a');
            }
        }
        return out.size() <= 8 ? out : "";
    }

    const char *SUB_MODES[] = {"off", "auto", "always"};
    const char *SUB_SIZES[] = {"small", "medium", "large"};
    const char *SUB_POSITIONS[] = {"bottom", "raised"};

    int indexOf(const std::string &value, const char *const *names, int n, int def) {
        for (int i = 0; i < n; i++) {
            if (value == names[i]) {
                return i;
            }
        }
        return def;
    }
}

SettingsStore::SettingsStore(std::string dataDir) : dir(std::move(dataDir)) {}

namespace {
    std::string sortSlot(const std::string &profileId, iptv::ContentType type) {
        return profileId + (type == iptv::ContentType::Series ? "/series" : "/movies");
    }
}

iptv::SortMode SettingsStore::sortMode(const std::string &profileId, iptv::ContentType type) const {
    auto it = settings.sortOrders.find(sortSlot(profileId, type));
    return it == settings.sortOrders.end() ? iptv::SortMode::Provider : iptv::sortModeFromKey(it->second);
}

void SettingsStore::setSortMode(const std::string &profileId, iptv::ContentType type, iptv::SortMode mode) {
    if (mode == iptv::SortMode::Provider) {
        settings.sortOrders.erase(sortSlot(profileId, type));
    } else {
        settings.sortOrders[sortSlot(profileId, type)] = iptv::sortModeKey(mode);
    }
}

const char *SettingsStore::formatName(StreamFormat f) {
    switch (f) {
        case StreamFormat::Ts:
            return "ts";
        case StreamFormat::Hls:
            return "hls";
        default:
            return "auto";
    }
}

const char *SettingsStore::stabilityKey(StabilityPreset p) {
    switch (p) {
        case StabilityPreset::Fast:
            return "fast";
        case StabilityPreset::MaxStability:
            return "max";
        default:
            return "balanced";
    }
}

std::string SettingsStore::serialize() const {
    json::Value root = json::Value::makeObject();
    root.set("version", json::Value::makeInt(FORMAT_VERSION));
    root.set("streamFormat", json::Value::makeString(formatName(settings.streamFormat)));
    root.set("playbackStability", json::Value::makeString(stabilityKey(settings.stability)));
    root.set("retryOnStall", json::Value::makeBool(settings.retryOnStall));
    root.set("audioLanguage", json::Value::makeString(settings.audioLanguage));
    root.set("subtitleMode", json::Value::makeString(SUB_MODES[(int) settings.subtitleMode]));
    root.set("subtitleLanguage", json::Value::makeString(settings.subtitleLanguage));
    root.set("subtitleSize", json::Value::makeString(SUB_SIZES[settings.subtitleSize < 0 ? 0 : settings.subtitleSize > 2 ? 2
                                                                                        : settings.subtitleSize]));
    root.set("subtitlePosition", json::Value::makeString(SUB_POSITIONS[settings.subtitlePosition == 1 ? 1 : 0]));
    root.set("subtitleShadow", json::Value::makeBool(settings.subtitleShadow));
    root.set("resumeVod", json::Value::makeBool(settings.resumeVod));
    root.set("autoPlayNextEpisode", json::Value::makeBool(settings.autoPlayNextEpisode));
    root.set("showTechnicalInfo", json::Value::makeBool(settings.showTechnicalInfo));
    root.set("loadImages", json::Value::makeBool(settings.loadImages));
    root.set("videoAspect", json::Value::makeString(display::aspectKey(settings.videoAspect)));
    root.set("videoCrop", json::Value::makeString(display::cropKey(settings.videoCrop)));
    root.set("zoom", json::Value::makeInt(display::clampZoom(settings.zoomPercent)));
    root.set("retryDownloads", json::Value::makeBool(settings.retryDownloads));
    root.set("resumeDownloadsOnStart", json::Value::makeBool(settings.resumeDownloadsOnStart));
    root.set("language", json::Value::makeString(settings.language == "tr" ? "tr" : "en"));
    json::Value orders = json::Value::makeObject();
    for (const auto &o: settings.sortOrders) {
        orders.set(o.first, json::Value::makeString(o.second));
    }
    root.set("sortOrders", std::move(orders));
    return json::write(root);
}

bool SettingsStore::deserialize(const std::string &text, std::string *error) {
    json::Value root;
    std::string err;
    if (!json::parse(text, root, &err) || !root.isObject()) {
        if (error) {
            *error = "settings.json is corrupt: " + err;
        }
        return false;
    }
    Settings s;  // unknown/missing keys keep defaults
    std::string fmt = root["streamFormat"].asString("auto");
    s.streamFormat = fmt == "ts" ? StreamFormat::Ts : fmt == "hls" ? StreamFormat::Hls : StreamFormat::Auto;
    std::string stab = root["playbackStability"].asString("balanced");
    s.stability = stab == "fast" ? StabilityPreset::Fast : stab == "max" ? StabilityPreset::MaxStability
                                                                          : StabilityPreset::Balanced;
    s.retryOnStall = root["retryOnStall"].asBool(s.retryOnStall);
    s.resumeVod = root["resumeVod"].asBool(s.resumeVod);
    s.audioLanguage = cleanCode(root["audioLanguage"].asString());
    s.subtitleMode = (tracks::SubtitleMode) indexOf(root["subtitleMode"].asString(), SUB_MODES, 3, 1);
    s.subtitleLanguage = cleanCode(root["subtitleLanguage"].asString());
    s.subtitleSize = indexOf(root["subtitleSize"].asString(), SUB_SIZES, 3, 1);
    s.subtitlePosition = indexOf(root["subtitlePosition"].asString(), SUB_POSITIONS, 2, 0);
    s.subtitleShadow = root["subtitleShadow"].asBool(false);
    s.autoPlayNextEpisode = root["autoPlayNextEpisode"].asBool(s.autoPlayNextEpisode);
    s.showTechnicalInfo = root["showTechnicalInfo"].asBool(s.showTechnicalInfo);
    s.loadImages = root["loadImages"].asBool(s.loadImages);
    if (root["videoAspect"].isString() || root["videoCrop"].isString()) {
        s.videoAspect = display::aspectFromKey(root["videoAspect"].asString("source"));
        s.videoCrop = display::cropFromKey(root["videoCrop"].asString("none"));
    } else {
        // written before Aspect Ratio and Crop were separate: the old display mode's meaning
        display::migrateDisplayMode(root["displayMode"].asString("auto"), s.videoAspect, s.videoCrop);
    }
    s.zoomPercent = display::clampZoom((int) root["zoom"].asInt(100));
    s.retryDownloads = root["retryDownloads"].asBool(s.retryDownloads);
    s.resumeDownloadsOnStart = root["resumeDownloadsOnStart"].asBool(s.resumeDownloadsOnStart);
    // the language is only honoured from files written since the Language setting exists (version 2)
    std::string lang = root["version"].asInt(1) >= 2 ? root["language"].asString("en") : "en";
    s.language = lang == "tr" ? "tr" : "en";
    for (const auto &o: root["sortOrders"].members()) {
        std::string key = o.second.asString();
        if (o.first.size() <= 80 && iptv::sortModeFromKey(key) != iptv::SortMode::Provider) {
            s.sortOrders[o.first] = key;
        }
    }
    settings = s;
    return true;
}

bool SettingsStore::load(std::string *warning) {
    std::string path = fs::join(dir, "settings.json");
    if (!fs::exists(path) && !fs::exists(path + ".bak")) {
        return true;
    }
    std::string text, err;
    if (fs::readFile(path, text, 256 * 1024, &err) && deserialize(text, &err)) {
        return true;
    }
    std::string first = err;
    if (fs::readFile(path + ".bak", text, 256 * 1024, &err) && deserialize(text, &err)) {
        if (warning) {
            *warning = "settings.json unreadable (" + first + "), restored the previous copy";
        }
        return true;
    }
    if (warning) {
        *warning = "settings reset to defaults: " + first;
    }
    settings = Settings();
    return false;
}

bool SettingsStore::save(std::string *error) {
    if (!fs::ensureDir(dir, error)) {
        return false;
    }
    return fs::writeFileAtomic(fs::join(dir, "settings.json"), serialize(), error);
}
