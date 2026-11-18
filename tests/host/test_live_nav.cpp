// In-player Live TV navigation (app/live_nav.h): Left opens the current group's channels, Left again the groups,
// Right the guide, Circle steps back; Cross on a channel switches playback without leaving the player and makes its
// group the zap list. Everything is indices into the session's catalog: no request, no copy of provider data.

#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "../../src/app/live_nav.h"
#include "../../src/i18n/i18n.h"

using livenav::Group;
using livenav::Navigator;
using livenav::Outcome;
using livenav::Panel;

namespace {
    // News: n1 n2 n3 (catalog 0..2), Sports: s1 s2 (3..4), Music: none, m1 without a known category (5)
    iptv::LiveCatalog catalog() {
        std::vector<iptv::Category> cats = {{"news", "News", ""}, {"sports", "Sports", ""}, {"music", "Music", ""}};
        std::vector<iptv::LiveChannel> chans;
        auto add = [&](const std::string &id, const std::string &cat) {
            iptv::LiveChannel c;
            c.id = id;
            c.name = "Channel " + id;
            c.categoryId = cat;
            chans.push_back(c);
        };
        add("n1", "news");
        add("n2", "news");
        add("n3", "news");
        add("s1", "sports");
        add("s2", "sports");
        add("m1", "gone");
        iptv::LiveCatalog live;
        live.assign(cats, chans);
        return live;
    }

    Outcome press(Navigator &nav, PadButton b, double &t, bool repeat = false) {
        t += 0.5;
        return nav.press(b, repeat, t);
    }

    int groupIndex(const Navigator &nav, const Group &g) {
        for (size_t i = 0; i < nav.groups().size(); i++) {
            if (nav.groups()[i] == g) {
                return (int) i;
            }
        }
        return -1;
    }
}

TEST(livenav_left_channels_left_groups_circle_back) {
    iptv::LiveCatalog live = catalog();
    std::set<std::string> favs = {"s2", "n2"};
    double t = 0;
    // opened from the Live TV screen on News, playing n2
    Navigator nav(live, favs, live.inCategory("news"), Group::of(Group::Kind::Category, "news"), 1);
    CHECK(nav.panel() == Panel::None);
    // the Live TV screen's groups: Favorites, All channels, the categories (no search results here)
    CHECK_EQ(nav.groups().size(), (size_t) 5);
    CHECK(nav.groups()[0] == Group::of(Group::Kind::Favorites));
    CHECK(nav.groups()[1] == Group::of(Group::Kind::All));
    CHECK(nav.groups()[2] == Group::of(Group::Kind::Category, "news"));
    CHECK_EQ(nav.name(0), i18n::tr("home.favorites"));
    CHECK_EQ(nav.name(1), i18n::tr("live.all_channels"));
    CHECK_EQ(nav.name(3), std::string("Sports"));
    CHECK(nav.count(0) == 2 && nav.count(1) == 6 && nav.count(2) == 3 && nav.count(4) == 0);

    // Left: the current group's channels, the playing channel selected
    Outcome o = press(nav, PadButton::Left, t);
    CHECK(o.handled && o.changed && !o.play);
    CHECK(nav.panel() == Panel::Channels);
    CHECK_EQ(nav.groupShown(), 2);
    CHECK((nav.channels() == std::vector<int>{0, 1, 2}));
    CHECK_EQ(nav.channelSelected(), 1);
    // Up / Down move (no wrap); the video is not touched
    CHECK(press(nav, PadButton::Down, t).changed && nav.channelSelected() == 2);
    o = press(nav, PadButton::Down, t, true);
    CHECK(o.handled && !o.changed && nav.channelSelected() == 2);
    press(nav, PadButton::Up, t);
    press(nav, PadButton::Up, t);
    CHECK_EQ(nav.channelSelected(), 0);

    // Left again: the groups, the open one highlighted
    o = press(nav, PadButton::Left, t);
    CHECK(o.handled && nav.panel() == Panel::Groups && nav.groupSelected() == 2);
    // the channel column follows the highlight
    press(nav, PadButton::Down, t);
    CHECK(nav.groupSelected() == 3 && (nav.channels() == std::vector<int>{3, 4}) && nav.channelSelected() == 0);
    press(nav, PadButton::Down, t);
    CHECK(nav.groupSelected() == 4 && nav.channels().empty());   // Music: no channels
    // Circle: back to the channels of the group shown before (News), on the playing channel
    o = press(nav, PadButton::Circle, t);
    CHECK(o.handled && nav.panel() == Panel::Channels && nav.groupShown() == 2);
    CHECK((nav.channels() == std::vector<int>{0, 1, 2}) && nav.channelSelected() == 1);
    // Circle: the picture again
    o = press(nav, PadButton::Circle, t);
    CHECK(o.handled && o.changed && nav.panel() == Panel::None);
    // Circle with nothing open: the player's (it leaves)
    CHECK(!press(nav, PadButton::Circle, t).handled);
}

TEST(livenav_picking_a_channel_switches_without_leaving) {
    iptv::LiveCatalog live = catalog();
    std::set<std::string> favs;
    double t = 0;
    Navigator nav(live, favs, live.inCategory("news"), Group::of(Group::Kind::Category, "news"), 1);
    press(nav, PadButton::Left, t);
    press(nav, PadButton::Down, t);
    Outcome o = press(nav, PadButton::Cross, t);
    CHECK(o.handled && o.play && o.index == 2 && (o.list == std::vector<int>{0, 1, 2}));
    CHECK(nav.panel() == Panel::None && nav.playing() == 2);   // the list closes, the new channel is marked
    // Cross on the playing channel only closes the list
    press(nav, PadButton::Left, t);
    CHECK_EQ(nav.channelSelected(), 2);
    o = press(nav, PadButton::Cross, t);
    CHECK(o.handled && !o.play && nav.panel() == Panel::None);
    // a held Cross never plays a second channel
    press(nav, PadButton::Left, t);
    press(nav, PadButton::Up, t);
    CHECK(!press(nav, PadButton::Cross, t, true).play && nav.panel() == Panel::Channels);

    // another group: Left, Down to Sports, Cross opens it, Cross plays s2 - Sports is now the zap list
    press(nav, PadButton::Left, t);
    press(nav, PadButton::Down, t);
    o = press(nav, PadButton::Cross, t);
    CHECK(o.handled && !o.play && nav.panel() == Panel::Channels && nav.groupShown() == 3);
    press(nav, PadButton::Down, t);
    o = press(nav, PadButton::Cross, t);
    CHECK(o.play && o.index == 1 && (o.list == std::vector<int>{3, 4}) && nav.playing() == 4);
    // next time the list opens on that group and channel
    press(nav, PadButton::Left, t);
    CHECK(nav.groupShown() == 3 && nav.channelSelected() == 1);
    // Right in the group list opens the highlighted group like Cross
    press(nav, PadButton::Left, t);
    press(nav, PadButton::Up, t);
    o = press(nav, PadButton::Right, t);
    CHECK(o.handled && nav.panel() == Panel::Channels && nav.groupShown() == 2);
    // an empty group: nothing to play
    press(nav, PadButton::Left, t);
    press(nav, PadButton::R2, t);
    CHECK_EQ(nav.groupSelected(), 4);
    press(nav, PadButton::Cross, t);
    CHECK(nav.channels().empty());
    o = press(nav, PadButton::Cross, t);
    CHECK(o.handled && !o.play && nav.panel() == Panel::Channels);
}

TEST(livenav_guide_panel) {
    iptv::LiveCatalog live = catalog();
    std::set<std::string> favs;
    double t = 0;
    Navigator nav(live, favs, live.inCategory("news"), Group::of(Group::Kind::Category, "news"), 0);
    Outcome o = press(nav, PadButton::Right, t);
    CHECK(o.handled && nav.panel() == Panel::Guide);
    // zapping goes on with the guide open (the guide follows the channel); Square: the playing channel's favorite
    for (PadButton b: {PadButton::Up, PadButton::Down, PadButton::L1, PadButton::R1, PadButton::Square}) {
        CHECK(!press(nav, b, t).handled);
    }
    CHECK(nav.panel() == Panel::Guide);
    nav.setPlaying(1);
    CHECK(press(nav, PadButton::Triangle, t).handled);   // no technical info over the guide
    // Left: the channel list instead; Circle: back to the picture
    o = press(nav, PadButton::Left, t);
    CHECK(o.handled && nav.panel() == Panel::Channels && nav.channelSelected() == 1);
    press(nav, PadButton::Circle, t);
    CHECK(nav.panel() == Panel::None);
    // Right, Cross and Circle close the guide; a held Right does not reopen it
    for (PadButton closer: {PadButton::Right, PadButton::Cross, PadButton::Circle}) {
        press(nav, PadButton::Right, t);
        CHECK(nav.panel() == Panel::Guide);
        CHECK(press(nav, closer, t).handled && nav.panel() == Panel::None);
    }
    CHECK(!press(nav, PadButton::Right, t, true).handled && nav.panel() == Panel::None);
    // Right in the channel list: back to the picture
    press(nav, PadButton::Left, t);
    press(nav, PadButton::Right, t);
    CHECK(nav.panel() == Panel::None);
}

TEST(livenav_groups_from_every_opener) {
    iptv::LiveCatalog live = catalog();
    std::set<std::string> favs = {"s2", "n2"};
    double t = 0;
    // search results: listed first, as the list the player was opened with
    {
        Navigator nav(live, favs, {4, 2, 5}, Group::of(Group::Kind::Results), 2);
        CHECK(nav.groups()[0] == Group::of(Group::Kind::Results));
        CHECK_EQ(nav.name(0), i18n::tr("live.search_results"));
        press(nav, PadButton::Left, t);
        CHECK(nav.groupShown() == 0 && (nav.channels() == std::vector<int>{4, 2, 5}) && nav.channelSelected() == 1);
        CHECK_EQ(nav.count(0), 3);
    }
    // favorites (Favorites screen, Home): the favorite channels in catalog order
    {
        Navigator nav(live, favs, {4, 1}, Group::of(Group::Kind::Favorites), 4);
        CHECK_EQ(nav.groups().size(), (size_t) 5);
        press(nav, PadButton::Left, t);
        CHECK(nav.groupShown() == 0 && (nav.channels() == std::vector<int>{1, 4}) && nav.channelSelected() == 1);
    }
    // All channels
    {
        Navigator nav(live, favs, {0, 1, 2, 3, 4, 5}, Group::of(Group::Kind::All), 5);
        press(nav, PadButton::Left, t);
        CHECK(nav.groupShown() == 1 && nav.channels().size() == 6 && nav.channelSelected() == 5);
    }
    // one channel from the history: its own category, so the list shows its neighbours
    {
        Navigator nav(live, favs, {3}, Group(), 3);
        CHECK_EQ(groupIndex(nav, Group::of(Group::Kind::Results)), -1);
        press(nav, PadButton::Left, t);
        CHECK(nav.groupShown() == groupIndex(nav, Group::of(Group::Kind::Category, "sports")));
        CHECK((nav.channels() == std::vector<int>{3, 4}) && nav.channelSelected() == 0);
    }
    // ... a channel whose category is not in the list: All channels
    {
        Navigator nav(live, favs, {5}, Group(), 5);
        press(nav, PadButton::Left, t);
        CHECK(nav.groupShown() == 1 && nav.channelSelected() == 5);
    }
    // an unknown category given by a caller: All channels too
    {
        Navigator nav(live, favs, {0}, Group::of(Group::Kind::Category, "missing"), 0);
        press(nav, PadButton::Left, t);
        CHECK_EQ(nav.groupShown(), 1);
    }
    // a longer list without a kind: shown as the player's own list
    {
        Navigator nav(live, favs, {0, 3}, Group(), 0);
        CHECK(nav.groups()[0] == Group::of(Group::Kind::Results));
    }
}

TEST(livenav_tabs_pages_favorites_and_idle) {
    iptv::LiveCatalog live = catalog();
    std::set<std::string> favs = {"n3"};
    double t = 0;
    Navigator nav(live, favs, live.inCategory("news"), Group::of(Group::Kind::Category, "news"), 0);
    nav.setPageSize(2);
    press(nav, PadButton::Left, t);
    // L1 / R1 in the channel list: the previous / next group (no wrap), selection on the playing channel or first
    Outcome o = press(nav, PadButton::R1, t);
    CHECK(o.changed && nav.groupShown() == 3 && (nav.channels() == std::vector<int>{3, 4}) && nav.channelSelected() == 0);
    press(nav, PadButton::L1, t);
    press(nav, PadButton::L1, t);
    press(nav, PadButton::L1, t);
    CHECK(nav.groupShown() == 0 && (nav.channels() == std::vector<int>{2}));
    CHECK(!press(nav, PadButton::L1, t).changed);
    // L2 / R2: a page
    press(nav, PadButton::R1, t);   // All channels
    press(nav, PadButton::R2, t);
    CHECK_EQ(nav.channelSelected(), 2);
    press(nav, PadButton::R2, t);
    press(nav, PadButton::R2, t);
    CHECK_EQ(nav.channelSelected(), 5);   // clamped at the end
    press(nav, PadButton::L2, t);
    CHECK_EQ(nav.channelSelected(), 3);
    // the favorites changed (Square in the player): the lists follow, the selection stays on its channel
    press(nav, PadButton::L1, t);   // Favorites: n3
    favs.insert("n1");
    nav.refresh();
    CHECK((nav.channels() == std::vector<int>{0, 2}) && nav.channelSelected() == 1);
    // nothing hidden behind the list: Square / Triangle / Options do nothing there
    for (PadButton b: {PadButton::Square, PadButton::Triangle, PadButton::Options}) {
        o = press(nav, b, t);
        CHECK(o.handled && !o.play);
    }
    // a list left alone closes by itself (12 s); from the group list it keeps the group that was open
    double at = t;
    CHECK(!nav.tick(at + Navigator::IDLE_CLOSE_LIST - 0.1));
    CHECK(nav.tick(at + Navigator::IDLE_CLOSE_LIST));
    CHECK(nav.panel() == Panel::None);
    press(nav, PadButton::Left, t);
    press(nav, PadButton::Left, t);
    press(nav, PadButton::Down, t);
    CHECK(nav.tick(t + Navigator::IDLE_CLOSE_LIST) && nav.panel() == Panel::None);
    press(nav, PadButton::Left, t);
    CHECK_EQ(nav.groupShown(), 0);   // Favorites again, not the highlighted group
    press(nav, PadButton::Circle, t);
    // the guide stays longer (time to read)
    press(nav, PadButton::Right, t);
    CHECK(!nav.tick(t + Navigator::IDLE_CLOSE_LIST + 1));
    CHECK(nav.tick(t + Navigator::IDLE_CLOSE_GUIDE));
    CHECK(!nav.tick(t + 100));   // nothing open: nothing to close
    // held Left / Right never open a panel by themselves
    CHECK(!press(nav, PadButton::Left, t, true).handled && nav.panel() == Panel::None);
}
