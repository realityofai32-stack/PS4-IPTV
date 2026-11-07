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
