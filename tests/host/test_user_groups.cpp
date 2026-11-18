// The user's playlists as Live TV groups (app/user_groups.h, iptv::LiveCatalog::setUserGroups): entries that are the
// source's own channels are indices (nothing copied), the others extra channels after the provider's; groups come
// before the provider's categories in the user's order; All Channels stays the provider's; rebuilding replaces the
// previous groups; the live player's group list has them; order / visibility settings; the v7 references in M3U.

#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "../../src/app/live_nav.h"
#include "../../src/app/user_groups.h"
#include "../../src/iptv/catalog.h"
#include "../../src/media/user_playlists.h"
#include "../../src/storage/settings_store.h"

namespace {
    iptv::LiveChannel channel(const std::string &id, const std::string &name, const std::string &category,
                              const std::string &url = "") {
        iptv::LiveChannel c;
        c.id = id;
        c.name = name;
        c.categoryId = category;
        c.url = url;
        return c;
    }

    userpl::Entry entry(const std::string &name, const std::string &url, const std::string &source = "",
                        const std::string &channelId = "") {
        userpl::Entry e;
        e.name = name;
        e.url = url;
        e.sourceId = source;
        e.channelId = channelId;
        return e;
    }

    // an Xtream-like source: channels without addresses
    iptv::LiveCatalog xtreamCatalog() {
        iptv::LiveCatalog live;
        live.assign({{"10", "News", ""}, {"20", "Sport", ""}},
                    {channel("101", "TRT 1", "10"), channel("102", "TRT Haber", "10"), channel("201", "Spor", "20")});
        return live;
    }
}

TEST(user_groups_overlay) {
    iptv::LiveCatalog live = xtreamCatalog();
    usergroups::Playlist mine;
    mine.file = "mine.m3u";
    mine.title = "Mine";
    mine.entries = {entry("TRT 1", "http://xtream.example.com/live/u/p/101.ts", "p1", "101"),    // this source
                    entry("Other", "http://other.example.com/live/u/p/5.ts", "p2", "5"),         // another source
                    entry("Radio", "http://radio.example.com/stream.mp3"),                     // by hand
                    entry("TRT 1 again", "http://xtream.example.com/live/u/p/101.ts", "p1", "101"),
                    entry("Gone", "", "p1", "999"),                                            // nothing to play
                    entry("Spor", "http://xtream.example.com/live/u/p/201.ts", "p1", "201")};
    usergroups::Playlist empty;
    empty.file = "empty.m3u";
    empty.title = "Empty";
    usergroups::Overlay o = usergroups::build(live, "p1", {mine, empty});
    CHECK(o.referenced == 3 && o.added == 2 && o.skipped == 1);
    CHECK_EQ(o.extra.size(), (size_t) 2);
    CHECK_EQ(o.groups.size(), (size_t) 2);
    if (o.groups.size() == 2 && o.extra.size() == 2) {
        CHECK_EQ(o.groups[0].category.id, std::string("userpl:mine.m3u"));
        CHECK_EQ(o.groups[0].category.name, std::string("Mine"));
        // TRT 1 (index 0), Other (extra 3), Radio (extra 4), Spor (index 2); "TRT 1 again" is the same channel
        CHECK((o.groups[0].channels == std::vector<int>{0, 3, 4, 2}));
        CHECK(o.groups[1].channels.empty());
        CHECK(o.extra[0].name == "Other" && o.extra[0].url == "http://other.example.com/live/u/p/5.ts");
        CHECK(o.extra[0].id[0] == 'u' && o.extra[0].categoryId == "userpl:mine.m3u");
        // stable ids: the same playlist gives the same ids
        usergroups::Overlay again = usergroups::build(live, "p1", {mine, empty});
        CHECK(!again.extra.empty() && again.extra[0].id == o.extra[0].id);
    }
    // in another source the same entries are all its own addresses
    usergroups::Overlay elsewhere = usergroups::build(live, "p2", {mine});
    CHECK(elsewhere.referenced == 0 && elsewhere.added == 4 && elsewhere.skipped == 1);   // (TRT 1 twice: one address)

    // a playlist source: an entry with the same address as one of its channels is that channel
    iptv::LiveCatalog m3uLive;
    m3uLive.assign({{"g", "Group", ""}}, {channel("m1", "A", "g", "http://cdn.example.com/a.ts"),
                                          channel("m2", "B", "g", "http://cdn.example.com/b.ts")});
    usergroups::Playlist byUrl;
    byUrl.file = "u.m3u";
    byUrl.entries = {entry("B", "http://cdn.example.com/b.ts"), entry("C", "http://cdn.example.com/c.ts")};
    usergroups::Overlay u = usergroups::build(m3uLive, "p9", {byUrl});
    CHECK(u.referenced == 1 && u.added == 1 && !u.groups.empty() && u.groups[0].category.name == "u.m3u");
    CHECK(!u.groups.empty() && (u.groups[0].channels == std::vector<int>{1, 2}));
}

TEST(user_groups_in_the_catalog) {
    iptv::LiveCatalog live = xtreamCatalog();
    usergroups::Playlist mine;
    mine.file = "mine.m3u";
    mine.title = "Mine";
    mine.entries = {entry("Spor", "", "p1", "201"), entry("Radio", "http://radio.example.com/stream.mp3")};
    usergroups::Overlay o = usergroups::build(live, "p1", {mine});
    std::string radioId = o.extra.empty() ? "" : o.extra[0].id;
    live.setUserGroups(std::move(o.extra), std::move(o.groups));
    CHECK_EQ(live.providerCount(), (size_t) 3);
    CHECK_EQ(live.channels().size(), (size_t) 4);
    CHECK_EQ(live.categories().size(), (size_t) 3);   // the user group first, then News, Sport
    if (live.categories().size() == 3) {
        CHECK(live.categories()[0].id == "userpl:mine.m3u" && live.categories()[1].id == "10");
    }
    CHECK_EQ(live.providerCategories().size(), (size_t) 2);
    CHECK(live.isUserGroup("userpl:mine.m3u") && !live.isUserGroup("10"));
    CHECK((live.inCategory("userpl:mine.m3u") == std::vector<int>{2, 3}));
    CHECK_EQ(live.categoryName("userpl:mine.m3u"), std::string("Mine"));
    CHECK_EQ(live.countInCategory("10"), 2);   // the provider's categories as before
    CHECK(live.find(radioId) != nullptr && live.find(radioId)->url == "http://radio.example.com/stream.mp3");
    CHECK(live.find("201") == &live.channels()[2]);
    CHECK(live.search("Radio").empty());       // search: the source's channels only
    CHECK_EQ(live.favorites({radioId, "101"}).size(), (size_t) 2);

    // rebuilt (a playlist changed): the previous extras and groups are gone, the provider's indices unchanged
    usergroups::Playlist other;
    other.file = "other.m3u";
    other.title = "Other";
    other.entries = {entry("TRT 1", "", "p1", "101")};
    usergroups::Overlay o2 = usergroups::build(live, "p1", {other});
    live.setUserGroups(std::move(o2.extra), std::move(o2.groups));
    CHECK(live.channels().size() == 3 && live.find(radioId) == nullptr && live.find("101") == &live.channels()[0]);
    CHECK(live.categories().size() == 3 && live.categories()[0].id == "userpl:other.m3u");
    CHECK(live.inCategory("userpl:mine.m3u").empty());
    // no groups at all
    live.setUserGroups({}, {});
    CHECK(live.categories().size() == 2 && live.channels().size() == 3);

    // the live player's group list: Favorites, All (the source's channels), the user groups, then the categories
    usergroups::Overlay o3 = usergroups::build(live, "p1", {mine});
    live.setUserGroups(std::move(o3.extra), std::move(o3.groups));
    std::set<std::string> favorites;
    livenav::Navigator nav(live, favorites, {2, 3}, livenav::Group::of(livenav::Group::Kind::Category, "userpl:mine.m3u"), 3);
    const auto &groups = nav.groups();
    CHECK_EQ(groups.size(), (size_t) 5);
    if (groups.size() == 5) {
        CHECK(groups[0].kind == livenav::Group::Kind::Favorites && groups[1].kind == livenav::Group::Kind::All);
        CHECK(groups[2].categoryId == "userpl:mine.m3u" && groups[3].categoryId == "10");
    }
    CHECK_EQ(nav.groupShown(), 2);   // opened from the user group: its list
    CHECK((nav.channels() == std::vector<int>{2, 3}) && nav.channelSelected() == 1);
    CHECK_EQ(nav.count(1), 3);       // All Channels: the source's channels
    CHECK_EQ(nav.name(2), std::string("Mine"));
}

TEST(user_groups_order_and_settings) {
    std::vector<userpl::Summary> disk(4);
    const char *files[] = {"a.m3u", "b.m3u", "c.m3u", "big.m3u"};
    for (int i = 0; i < 4; i++) {
        disk[(size_t) i].file = files[i];
        disk[(size_t) i].title = files[i];
    }
    disk[3].editable = false;   // a provider's whole playlist: never a group
    std::vector<userpl::Summary> o = usergroups::ordered({"c.m3u", "gone.m3u", "a.m3u"}, disk);
    CHECK(o.size() == 4 && o[0].file == "c.m3u" && o[1].file == "a.m3u" && o[2].file == "b.m3u");
    std::vector<std::string> shown = usergroups::shownFiles({"c.m3u", "a.m3u"}, {"a.m3u"}, disk);
    CHECK((shown == std::vector<std::string>{"c.m3u", "b.m3u"}));

    SettingsStore s(".");
    CHECK(s.get().playlistOrder.empty() && s.get().hiddenPlaylists.empty());
    s.get().playlistOrder = {"c.m3u", "a.m3u"};
    s.get().hiddenPlaylists = {"b.m3u"};
    SettingsStore t(".");
    CHECK(t.deserialize(s.serialize(), nullptr));
    CHECK((t.get().playlistOrder == std::vector<std::string>{"c.m3u", "a.m3u"}));
    CHECK(t.get().hiddenPlaylists.count("b.m3u") == 1);
    // only plain file names are taken back (never a path)
    SettingsStore u(".");
    CHECK(u.deserialize("{\"version\":2,\"playlistOrder\":[\"../x.m3u\",\"ok.m3u\",\".hidden.m3u\",\"ok.m3u\"],"
                        "\"hiddenPlaylists\":[\"/data/y.m3u\"]}", nullptr));
    CHECK((u.get().playlistOrder == std::vector<std::string>{"ok.m3u"}) && u.get().hiddenPlaylists.empty());

    // the references survive the M3U file (and other players ignore them)
    userpl::Playlist p;
    p.title = "Refs";
    p.entries = {entry("TRT 1", "http://xtream.example.com/live/u/p/101.ts", "p1", "101")};
    userpl::Playlist back;
    std::string err;
    CHECK(userpl::fromM3u(userpl::toM3u(p), "x", back, err));
    CHECK(back.entries.size() == 1 && back.entries[0].sourceId == "p1" && back.entries[0].channelId == "101");
    CHECK(userpl::toM3u(p).find("v7-source=\"p1\" v7-channel=\"101\"") != std::string::npos);
}
