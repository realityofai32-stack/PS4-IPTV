#include <cstdlib>

#include "check.h"
#include "../../src/platform/fs.h"
#include "../../src/storage/profile_store.h"
#include "../../src/storage/settings_store.h"
#include "../../src/ui/keyboard_model.h"
#include "../../src/ui/palette.h"

static std::string tempDir(const char *name) {
    const char *base = std::getenv("PS4IPTV_TEST_TMP");
    std::string dir = fs::join(base ? base : ".", name);
    fs::ensureDir(dir);
    for (const auto &e: fs::listDir(dir)) {
        fs::removeFile(fs::join(dir, e.name));
    }
    return dir;
}

TEST(profile_store_roundtrip_and_active) {
    std::string dir = tempDir("profiles1");
    ProfileStore s(dir);
    CHECK(s.load());
    CHECK(s.profiles().empty());
    iptv::Profile p;
    p.name = "Home \xC5\x9F";
    p.server = "http://example.com:8080";
    p.username = "u1";
    p.password = "pw\"1";
    std::string id1 = s.upsert(p, 100);
    p.name = "Second";
    std::string id2 = s.upsert(p, 200);
    CHECK(id1 != id2);
    CHECK_EQ(s.activeId(), id1);
    s.setActive(id2);
    s.setLastStatus(id2, "Connected", 300);
    CHECK(s.save());

    ProfileStore t(dir);
    CHECK(t.load());
    CHECK_EQ(t.profiles().size(), (size_t) 2);
    CHECK_EQ(t.activeId(), id2);
    CHECK(t.find(id1) && t.find(id1)->password == "pw\"1" && t.find(id1)->name == "Home \xC5\x9F");
    CHECK(t.find(id2)->lastStatus == "Connected" && t.find(id2)->lastUsedAt == 300);

    // edit keeps createdAt, remove moves the active profile
    iptv::Profile edit = *t.find(id1);
    edit.name = "Renamed";
    CHECK_EQ(t.upsert(edit, 999), id1);
    CHECK(t.find(id1)->name == "Renamed" && t.find(id1)->createdAt == 100);
    CHECK(t.remove(id2));
    CHECK_EQ(t.activeId(), id1);
    CHECK(!t.remove("nope"));
}

TEST(profile_store_recovers_from_corrupt_file) {
    std::string dir = tempDir("profiles2");
    ProfileStore s(dir);
    iptv::Profile p;
    p.server = "http://a.com";
    p.name = "A";
    s.upsert(p, 1);
    CHECK(s.save());
    p.name = "B";
    s.upsert(p, 2);
    CHECK(s.save());  // first version is now profiles.json.bak
    CHECK(fs::exists(fs::join(dir, "profiles.json.bak")));
    // simulate a damaged profiles.json: writeFileAtomic keeps the last good file ("B") as .bak
    CHECK(fs::writeFileAtomic(fs::join(dir, "profiles.json"), "{\"version\":1,\"profiles\":[{\"id\":"));
    ProfileStore good(dir);
    std::string warning;
    CHECK(good.load(&warning));
    CHECK(!warning.empty());
    CHECK(!good.profiles().empty());

    // both unreadable -> empty store, no crash
    CHECK(fs::writeFileAtomic(fs::join(dir, "profiles.json"), "garbage"));
    CHECK(fs::writeFileAtomic(fs::join(dir, "profiles.json.bak"), "garbage"));
    ProfileStore bad(dir);
    warning.clear();
    CHECK(!bad.load(&warning));
    CHECK(bad.profiles().empty());
    CHECK(!warning.empty());
}

TEST(profile_store_rejects_future_version) {
    ProfileStore s(".");
    std::string err;
    CHECK(!s.deserialize("{\"version\":99,\"profiles\":[]}", &err));
    CHECK(!err.empty());
}

TEST(settings_roundtrip_and_defaults) {
    std::string dir = tempDir("settings1");
    SettingsStore s(dir);
    CHECK(s.load());
    CHECK(s.get().streamFormat == StreamFormat::Auto);
    s.get().streamFormat = StreamFormat::Hls;
    s.get().showTechnicalInfo = true;
    CHECK(s.save());
    SettingsStore t(dir);
    CHECK(t.load());
    CHECK(t.get().streamFormat == StreamFormat::Hls);
    CHECK(t.get().showTechnicalInfo);
    CHECK(t.get().resumeVod);
    SettingsStore u(dir);
    CHECK(u.deserialize("{\"version\":1,\"unknownKey\":5}", nullptr));
    CHECK(u.get().streamFormat == StreamFormat::Auto);
}

// Settings > Appearance > Accent color: persisted, migrated from older files, invalid values fall back
TEST(settings_accent_color_persistence_and_migration) {
    std::string dir = tempDir("settings-accent");
    SettingsStore s(dir);
    CHECK(s.load());
    CHECK_EQ(s.get().accentColor, std::string("blue"));   // fresh install: V7 Blue
    s.get().accentColor = "teal";
    s.get().language = "tr";
    s.get().subtitleSize = 2;
    CHECK(s.save());
    CHECK(s.serialize().find("\"accentColor\": \"teal\"") != std::string::npos);
    SettingsStore t(dir);
    CHECK(t.load());
    CHECK_EQ(t.get().accentColor, std::string("teal"));
    CHECK_EQ(t.get().language, std::string("tr"));
    CHECK_EQ(t.get().subtitleSize, 2);
    // every preset survives a round trip
    for (int i = 0; i < palette::ACCENT_COUNT; i++) {
        SettingsStore r(dir);
        r.get().accentColor = palette::ACCENTS[i].key;
        SettingsStore back(dir);
        CHECK(back.deserialize(r.serialize(), nullptr));
        CHECK_EQ(back.get().accentColor, std::string(palette::ACCENTS[i].key));
    }

    // a v0.2.0 settings file (no accentColor): everything kept, the accent defaults to V7 Blue
    const char *v020 = "{\"version\":2,\"streamFormat\":\"hls\",\"playbackStability\":\"max\",\"retryOnStall\":false,"
                       "\"audioLanguage\":\"tur\",\"subtitleMode\":\"always\",\"subtitleSize\":\"large\","
                       "\"resumeVod\":true,\"smoothScrolling\":false,\"videoAspect\":\"16:9\",\"zoom\":110,"
                       "\"language\":\"tr\",\"sortOrders\":{\"p1/movies\":\"az\"}}";
    SettingsStore old(dir);
    CHECK(old.deserialize(v020, nullptr));
    CHECK_EQ(old.get().accentColor, std::string("blue"));
    CHECK(old.get().streamFormat == StreamFormat::Hls);
    CHECK(old.get().stability == StabilityPreset::MaxStability);
    CHECK(!old.get().retryOnStall);
    CHECK_EQ(old.get().audioLanguage, std::string("tur"));
    CHECK(old.get().subtitleMode == tracks::SubtitleMode::Always);
    CHECK_EQ(old.get().subtitleSize, 2);
    CHECK(!old.get().smoothScrolling);
    CHECK_EQ(old.get().zoomPercent, 110);
    CHECK_EQ(old.get().language, std::string("tr"));
    CHECK_EQ(old.get().sortOrders.size(), (size_t) 1);
    // ... and the next save adds the field without changing anything else
    SettingsStore again(dir);
    CHECK(again.deserialize(old.serialize(), nullptr));
    CHECK_EQ(again.get().accentColor, std::string("blue"));
    CHECK_EQ(again.get().zoomPercent, 110);
    CHECK_EQ(again.get().language, std::string("tr"));

    // invalid values: unknown key, wrong type, empty -> V7 Blue (never a crash, never an unreadable accent)
    for (const char *bad: {"{\"version\":2,\"accentColor\":\"chartreuse\"}", "{\"version\":2,\"accentColor\":7}",
                           "{\"version\":2,\"accentColor\":\"\"}", "{\"version\":2,\"accentColor\":null}",
                           "{\"version\":2,\"accentColor\":\"BLUE\"}"}) {
        SettingsStore b(dir);
        CHECK(b.deserialize(bad, nullptr));
        CHECK_EQ(b.get().accentColor, std::string("blue"));
    }
    // a value set in memory that is not a preset is written as the default
    SettingsStore m(dir);
    m.get().accentColor = "nonsense";
    CHECK(m.serialize().find("\"accentColor\": \"blue\"") != std::string::npos);
}

TEST(atomic_write_replaces_content) {
    std::string dir = tempDir("atomic");
    std::string f = fs::join(dir, "x.json");
    CHECK(fs::writeFileAtomic(f, "one"));
    CHECK(fs::writeFileAtomic(f, "two"));
    std::string out;
    CHECK(fs::readFile(f, out, 100));
    CHECK_EQ(out, std::string("two"));
    CHECK(!fs::exists(f + ".tmp"));
    CHECK(!fs::readFile(f, out, 2));  // size limit
}

TEST(keyboard_layout_contains_required_characters) {
    KeyboardModel k;
    std::string all;
    int specials = 0;
    for (const auto &row: k.rows()) {
        int width = 0;
        for (const auto &key: row) {
            width += key.span;
            if (key.action == KeyAction::Char) {
                all += key.lower;
            } else {
                specials++;
            }
        }
        CHECK_EQ(width, KeyboardModel::COLUMNS);
    }
    for (char c: std::string("abcdefghijklmnopqrstuvwxyz0123456789:/.-_@?=&%#+!")) {
        CHECK(all.find(c) != std::string::npos);
    }
    CHECK(specials >= 6);
}

TEST(keyboard_typing_shift_and_navigation) {
    KeyboardModel k("ab", 5);
    // focus starts on 'q' (row 1, col 0)
    CHECK(k.focused().lower == "q");
    k.press();
    CHECK_EQ(k.text(), std::string("abq"));
    k.toggleShift();
    k.move(1, 0);  // 'w'
    k.press();
    CHECK_EQ(k.text(), std::string("abqW"));
    k.press();
    k.press();     // max length 5
    CHECK_EQ(k.text(), std::string("abqWW"));
    k.backspace();
    CHECK_EQ(k.text(), std::string("abqW"));
    k.move(-1, 0);
    k.move(-1, 0);  // wraps to the last key of the row
    CHECK(k.focused().lower == "_");
    // vertical moves keep the column: from '_' (unit 11) down to row 2 unit 11 ':'
    k.move(0, 1);
    CHECK(k.focused().lower == ":");
    // down to the bottom row and onto OK, which ends the edit
    k.move(0, 1);
    k.move(0, 1);
    k.move(0, 1);
    CHECK(k.focused().action == KeyAction::Ok);
    CHECK(k.press() == KeyboardModel::Result::Ok);
    k.clear();
    CHECK(k.text().empty());
    k.move(0, 1);  // wraps to the top row
    CHECK(k.focusRow() == 0);
}
