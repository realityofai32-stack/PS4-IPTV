// Video display mode / zoom -> mpv options, and the settings defaults / migration (English, Auto, 100 %).

#include <cmath>
#include <map>

#include "check.h"
#include "../../src/player/display.h"
#include "../../src/storage/settings_store.h"

using namespace display;

namespace {
    std::map<std::string, std::string> asMap(const std::vector<std::pair<std::string, std::string>> &v) {
        std::map<std::string, std::string> m;
        for (const auto &p: v) {
            m[p.first] = p.second;
        }
        return m;
    }
}

TEST(display_modes_map_to_mpv_vo_options) {
    auto a = asMap(mpvOptions(Mode::Auto, 100));
    // every option is always set: nothing of a previous mode survives a change
    CHECK_EQ(a.size(), (size_t) 4);
    CHECK_EQ(a["keepaspect"], std::string("yes"));
    CHECK_EQ(a["panscan"], std::string("0.0"));
    CHECK_EQ(a["video-aspect-override"], std::string("-1"));   // the file's aspect ("no" would mean square pixels)
    CHECK_EQ(a["video-zoom"], std::string("0.000000"));
    CHECK(asMap(mpvOptions(Mode::Fit, 100)) == a);           // Fit = the whole picture with bars, like Auto
    auto fill = asMap(mpvOptions(Mode::Fill, 100));
    CHECK_EQ(fill["panscan"], std::string("1.0"));
    CHECK_EQ(fill["keepaspect"], std::string("yes"));
    auto stretch = asMap(mpvOptions(Mode::Stretch, 120));
    CHECK_EQ(stretch["keepaspect"], std::string("no"));
    CHECK_EQ(stretch["video-zoom"], std::string("0.000000")); // zoom has no effect without keepaspect
    CHECK_EQ(asMap(mpvOptions(Mode::Aspect16x9, 100))["video-aspect-override"], std::string("16:9"));
    CHECK_EQ(asMap(mpvOptions(Mode::Aspect4x3, 100))["video-aspect-override"], std::string("4:3"));
    // zoom: log2 scale
    double z = std::atof(asMap(mpvOptions(Mode::Auto, 110))["video-zoom"].c_str());
    CHECK(std::fabs(std::pow(2.0, z) - 1.10) < 1e-4);
    z = std::atof(asMap(mpvOptions(Mode::Fill, 125))["video-zoom"].c_str());
    CHECK(std::fabs(std::pow(2.0, z) - 1.25) < 1e-4);
    CHECK(!zoomAvailable(Mode::Stretch) && zoomAvailable(Mode::Fill));
    CHECK_EQ(clampZoom(0), 100);
    CHECK_EQ(clampZoom(111), 110);
    CHECK_EQ(clampZoom(999), 125);
    for (int i = 0; i < MODE_COUNT; i++) {
        CHECK(modeFromKey(modeKey((Mode) i)) == (Mode) i);
    }
    CHECK(modeFromKey("bogus") == Mode::Auto);
}

TEST(settings_defaults_and_language_migration) {
    Settings fresh;
    CHECK_EQ(fresh.language, std::string("en"));             // a fresh installation is English
    CHECK(fresh.displayMode == Mode::Auto);                  // never cropped by default
    CHECK_EQ(fresh.zoomPercent, 100);
    CHECK(fresh.retryDownloads && fresh.resumeDownloadsOnStart);

    SettingsStore s(".");
    std::string err;
    // a settings.json from before the Language setting existed: English, whatever it contained
    CHECK(s.deserialize("{\"version\":1,\"language\":\"tr\",\"resumeVod\":false}", &err));
    CHECK_EQ(s.get().language, std::string("en"));
    CHECK(!s.get().resumeVod);
    CHECK(s.get().displayMode == Mode::Auto);
    // no language at all: English
    CHECK(s.deserialize("{\"version\":2}", &err));
    CHECK_EQ(s.get().language, std::string("en"));
    // the user's choice is kept from version 2 on
    s.get().language = "tr";
    s.get().displayMode = Mode::Fill;
    s.get().zoomPercent = 115;
    s.get().retryDownloads = false;
    std::string text = s.serialize();
    SettingsStore t(".");
    CHECK(t.deserialize(text, &err));
    CHECK_EQ(t.get().language, std::string("tr"));
    CHECK(t.get().displayMode == Mode::Fill);
    CHECK_EQ(t.get().zoomPercent, 115);
    CHECK(!t.get().retryDownloads);
    // unknown values fall back safely
    CHECK(t.deserialize("{\"version\":2,\"language\":\"xx\",\"displayMode\":\"weird\",\"zoom\":400}", &err));
    CHECK_EQ(t.get().language, std::string("en"));
    CHECK(t.get().displayMode == Mode::Auto);
    CHECK_EQ(t.get().zoomPercent, 125);
}
