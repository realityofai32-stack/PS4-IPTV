// Home rails (v7-ux-9): Recently Watched cards identify the item only (never where playback stopped - that is Continue
// Watching), and the "more" chevron right of a rail shows exactly while items lie off-screen to the right
// (scroll::moreAfter), while scrolling and after removals.

#include <algorithm>
#include <string>
#include <vector>

#include "check.h"
#include "../../src/app/home_cards.h"
#include "../../src/i18n/i18n.h"
#include "../../src/ui/layout.h"
#include "../../src/ui/scroll_math.h"
#include "../../src/platform/input_actions.h"

using homecards::Rail;
using iptv::ContentType;

namespace {
    HistoryEntry entry(ContentType t, double pos, double dur, bool watched = false) {
        HistoryEntry h;
        h.type = t;
        h.id = "x";
        h.name = "Pilot";
        h.seriesId = t == ContentType::Series ? "s1" : "";
        h.seriesName = "The Series";
        h.season = 1;
        h.episode = 3;
        h.position = pos;
        h.duration = dur;
        h.watched = watched;
        return h;
    }

    bool mentionsPlayback(const homecards::Text &t, const HistoryEntry &h) {
        const std::string watched = i18n::tr("progress.watched");
        const std::string upNext = i18n::tr("home.up_next");
        return t.progress > 0 || t.third.find(watched) != std::string::npos || t.third.find(upNext) != std::string::npos
               || (h.duration > 0 && t.third.find(':') != std::string::npos)      // "12:34 left" / times
               || t.second.find(':') != std::string::npos;
    }
}

TEST(home_recently_watched_cards_identify_only) {
    i18n::setLanguage(i18n::Language::English);
    // an episode / a movie in progress, watched ones, a series' next episode, a channel
    std::vector<HistoryEntry> items = {entry(ContentType::Series, 1200, 2700), entry(ContentType::Movie, 3000, 6000),
                                       entry(ContentType::Movie, 6000, 6000, true), entry(ContentType::Series, 2690, 2700, true),
                                       entry(ContentType::Live, 0, 0)};
    HistoryEntry next = entry(ContentType::Series, 0, 2700);
    next.upNext = true;
    items.push_back(next);
    for (const HistoryEntry &h: items) {
        homecards::Text r = homecards::historyCard(h, Rail::Recent, h.type == ContentType::Live ? "101" : "");
        CHECK(!mentionsPlayback(r, h));            // no position, time, progress bar, Watched or Next episode
        CHECK(r.progress == 0);
    }
    // what each card says instead
    homecards::Text ep = homecards::historyCard(items[0], Rail::Recent);
    CHECK(ep.second == "S01E03 \xC2\xB7 Pilot" && ep.third == i18n::tr("home.kind_series"));
    homecards::Text movie = homecards::historyCard(items[1], Rail::Recent);
    CHECK(movie.second == i18n::tr("home.kind_movie") && movie.third.empty());
    homecards::Text ch = homecards::historyCard(items[4], Rail::Recent, "101");
    CHECK(ch.second == i18n::tr("home.kind_live") && ch.third == "101");
    CHECK(homecards::historyCard(items[4], Rail::Recent, "").third.empty());   // number unknown: nothing
    HistoryEntry untitled = items[0];
    untitled.name = "";
    CHECK(homecards::historyCard(untitled, Rail::Recent).second == "S01E03 \xC2\xB7 " + i18n::tr("episode.number", {"3"}));
}

TEST(home_continue_watching_cards_keep_their_progress) {
    i18n::setLanguage(i18n::Language::English);
    homecards::Text ep = homecards::historyCard(entry(ContentType::Series, 1200, 2700), Rail::Continue);
    CHECK(ep.progress > 0.4f && ep.progress < 0.5f && !ep.third.empty());   // time left + the bar
    HistoryEntry next = entry(ContentType::Series, 0, 2700);
    next.upNext = true;
    CHECK(homecards::historyCard(next, Rail::Continue).third == i18n::tr("home.up_next"));
    CHECK(homecards::historyCard(entry(ContentType::Movie, 3000, 6000), Rail::Continue).progress > 0);
}

namespace {
    // Home's rail scrolling (refresh()): whole cards, the focused one always among the three on screen; a rebuild
    // (a removal) clamps the first card to the new count
    struct RailSim {
        int count;
        int index = 0;
        int first = 0;
        const int visible = layout::home::CARDS_VISIBLE;

        void refresh() {
            index = std::min(index, std::max(0, count - 1));
            if (index < first) {
                first = index;
            } else if (index >= first + visible) {
                first = index - visible + 1;
            }
        }

        void move(int d) {
            index = std::min(std::max(index + d, 0), std::max(0, count - 1));
            refresh();
        }

        void remove() {   // the focused card is removed: the row is rebuilt, then refreshed
            count = std::max(0, count - 1);
            first = std::min(first, std::max(0, count - visible));
            refresh();
        }

        bool more() const { return scroll::moreAfter(first, visible, count); }
    };
}

TEST(home_rail_more_indicator) {
    CHECK(layout::home::CARDS_VISIBLE == 3);
    // fits completely: never shown
    for (int n = 0; n <= 3; n++) {
        RailSim r{n};
        r.refresh();
        CHECK(!r.more());
        r.move(1);
        r.move(1);
        CHECK(!r.more());
    }
    // ten items: shown until the end is on screen, while scrolling both ways
    RailSim r{10};
    r.refresh();
    CHECK(r.more());
    for (int i = 0; i < 9; i++) {
        r.move(1);
        CHECK(r.more() == (r.first + 3 < 10));
    }
    CHECK(r.index == 9 && r.first == 7 && !r.more());   // the end reached: hidden
    r.move(-1);
    r.move(-1);
    CHECK(!r.more());                                    // still the last three on screen
    r.move(-1);
    CHECK(r.first == 6 && r.more());                     // scrolled back: shown again
    // removals: four items, at the last one -> after removing it three fit: hidden
    RailSim rm{4};
    rm.refresh();
    CHECK(rm.more());
    rm.move(3);
    CHECK(rm.first == 1 && !rm.more());
    rm.remove();
    CHECK(rm.count == 3 && rm.first == 0 && !rm.more());
    // removing from the front of a long rail keeps it shown while more remain, hides it when they fit
    RailSim front{6};
    front.refresh();
    front.remove();
    CHECK(front.more());
    front.remove();
    front.remove();
    CHECK(front.count == 3 && !front.more());
    // edge values
    CHECK(!scroll::moreAfter(0, 3, 3) && scroll::moreAfter(0, 3, 4) && !scroll::moreAfter(1, 3, 4));
    CHECK(!scroll::moreAfter(0, 0, 10) && scroll::moreAfter(-2, 3, 4));
}

// ---- v7-ux-10: the chevron inside the TV-safe area, the Settings gear, L1 / R1 through the top navigation

#include <cmath>

#include "../../src/app/home_nav.h"
#include "../../src/ui/icon_shapes.h"

TEST(home_rail_more_chevron_inside_the_safe_area) {
    namespace L = layout::home;
    CHECK(L::RAIL_MORE_RIGHT <= layout::SAFE_RIGHT);                       // never outside the 5 % right margin
    CHECK(L::RAIL_MORE_RIGHT >= layout::SAFE_RIGHT - 1);                   // at the rail's right edge
    // the rail's right edge is the third card's right edge: three cards fill the content width
    CHECK(layout::SAFE_X + L::CARDS_VISIBLE * L::CARD_W + (L::CARDS_VISIBLE - 1) * L::CARD_GAP == layout::SAFE_RIGHT);
    // on the heading line, not over the cards (they start RAIL_HEADING_H below the rail's top)
    CHECK(L::RAIL_MORE_CENTER_Y > 0 && L::RAIL_MORE_CENTER_Y < L::RAIL_HEADING_H / 2);
}

TEST(home_settings_icon_is_a_gear_like_the_other_icons) {
    const float s = 64, c = s / 2;
    std::vector<ui::icons::Point> p = ui::icons::gear(c, c, s);
    CHECK_EQ(p.size(), (size_t) (4 * ui::icons::GEAR_TEETH));
    float maxR = 0, minR = 1e9f, prev = -1e9f;
    double turn = 0;
    for (size_t i = 0; i < p.size(); i++) {
        float r = std::hypot(p[i].x - c, p[i].y - c);
        maxR = std::max(maxR, r);
        minR = std::min(minR, r);
        // star-shaped around the centre: the angle only ever increases (the fan fill and the outline both work)
        float a = std::atan2(p[i].y - c, p[i].x - c);
        if (i > 0) {
            float d = a - prev;
            while (d < 0) {
                d += 2 * 3.14159265f;
            }
            CHECK(d > 0 && d < 3.14159265f);
            turn += d;
        }
        prev = a;
    }
    // closing the outline (last point -> first) completes exactly one turn around the centre
    float close = std::atan2(p[0].y - c, p[0].x - c) - prev;
    while (close < 0) {
        close += 2 * 3.14159265f;
    }
    CHECK(close > 0 && std::fabs(turn + close - 2 * 3.14159265) < 0.001);
    // the same size as the other icons (the favorites star's points reach 0.24 x the button): teeth stand out
    CHECK(std::fabs(maxR - 0.235f * s) < 0.01f && std::fabs(minR - 0.175f * s) < 0.01f);
    CHECK(maxR - minR > 0.05f * s && maxR <= 0.25f * s);
    CHECK(ui::icons::GEAR_HUB * s > 2 && ui::icons::GEAR_HUB < ui::icons::GEAR_INNER / 2);
    // inside the round button
    for (const auto &q: p) {
        CHECK(q.x > 0 && q.x < s && q.y > 0 && q.y < s);
    }
}

TEST(home_l1_r1_through_the_top_navigation) {
    // Home, Live TV, Movies, Series, Local Media, Search, Favorites, Downloads, Settings
    const int n = 9;
    std::vector<int> visited;
    int at = 0;
    for (int i = 0; i < 12; i++) {           // R1 from Home to Settings, then it stops
        at = homenav::shoulder(at, n, 1);
        visited.push_back(at);
    }
    CHECK((std::vector<int>(visited.begin(), visited.begin() + 8) == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8}));
    CHECK(at == 8 && homenav::shoulder(8, n, 1) == 8);     // the end: no wrap
    for (int i = 0; i < 12; i++) {           // L1 back to Home, then it stops
        at = homenav::shoulder(at, n, -1);
    }
    CHECK(at == 0 && homenav::shoulder(0, n, -1) == 0);
    // from the hero / a rail: the item after / before the navigation's last focus (Home at first)
    CHECK(homenav::shoulder(0, n, 1) == 1 && homenav::shoulder(0, n, -1) == 0);
    CHECK(homenav::shoulder(3, n, 1) == 4 && homenav::shoulder(3, n, -1) == 2);
    // playlist sources (no Movies / Series: 7 items) and odd input
    CHECK(homenav::shoulder(6, 7, 1) == 6 && homenav::shoulder(20, 7, -1) == 5 && homenav::shoulder(-3, 7, 1) == 1);
    CHECK(homenav::shoulder(0, 0, 1) == 0);
    // the keyboard follows the centralized mapping: Tab / Shift+Tab are R1 / L1 everywhere (platform/input_actions.h)
    kbd::Event tab;
    tab.key = kbd::Key::Tab;
    InputEvent out;
    CHECK(actions::routeUniversal(tab, out) && out.button == PadButton::R1);
    tab.shift = true;
    CHECK(actions::routeUniversal(tab, out) && out.button == PadButton::L1);
}
