#include <cstdlib>

#include "check.h"
#include "../../src/platform/fs.h"
#include "../../src/storage/profile_store.h"
#include "../../src/storage/settings_store.h"
#include "../../src/ui/keyboard_model.h"

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
