#include "settings_store.h"
#include "../core/json.h"
#include "../platform/fs.h"

namespace {
    const int FORMAT_VERSION = 1;
}

SettingsStore::SettingsStore(std::string dataDir) : dir(std::move(dataDir)) {}

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

std::string SettingsStore::serialize() const {
    json::Value root = json::Value::makeObject();
    root.set("version", json::Value::makeInt(FORMAT_VERSION));
    root.set("streamFormat", json::Value::makeString(formatName(settings.streamFormat)));
    root.set("resumeVod", json::Value::makeBool(settings.resumeVod));
    root.set("autoPlayNextEpisode", json::Value::makeBool(settings.autoPlayNextEpisode));
    root.set("showTechnicalInfo", json::Value::makeBool(settings.showTechnicalInfo));
    root.set("loadImages", json::Value::makeBool(settings.loadImages));
    root.set("language", json::Value::makeString(settings.language));
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
    s.resumeVod = root["resumeVod"].asBool(s.resumeVod);
    s.autoPlayNextEpisode = root["autoPlayNextEpisode"].asBool(s.autoPlayNextEpisode);
    s.showTechnicalInfo = root["showTechnicalInfo"].asBool(s.showTechnicalInfo);
    s.loadImages = root["loadImages"].asBool(s.loadImages);
    s.language = root["language"].asString(s.language);
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
