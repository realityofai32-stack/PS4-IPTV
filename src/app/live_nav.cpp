#include <algorithm>

#include "live_nav.h"
#include "../i18n/i18n.h"

namespace livenav {

    Navigator::Navigator(const iptv::LiveCatalog &liveCatalog, const std::set<std::string> &favoriteIds,
                         std::vector<int> openerList, Group openerGroup, int playing)
            : live(liveCatalog), favorites(favoriteIds), opener(std::move(openerList)), playingIndex(playing) {
        Group start = openerGroup;
        if (start.kind == Group::Kind::Auto) {
            // a single channel (from the history): its own category, so the list shows its neighbours
            const auto &channels = live.channels();
            if (opener.size() == 1 && playing >= 0 && playing < (int) channels.size()) {
                start = Group::of(Group::Kind::Category, channels[(size_t) playing].categoryId);
            } else {
                start = Group::of(Group::Kind::Results);
            }
        }
        if (start.kind == Group::Kind::Results) {
            groupList.push_back(Group::of(Group::Kind::Results));
        }
        groupList.push_back(Group::of(Group::Kind::Favorites));
        groupList.push_back(Group::of(Group::Kind::All));
        for (const auto &c: live.categories()) {
            groupList.push_back(Group::of(Group::Kind::Category, c.id));
        }
        int found = findGroup(start);
        shown = found >= 0 ? found : findGroup(Group::of(Group::Kind::All));   // an unknown category: All channels
        highlight = before = shown;
        show(shown);
    }

    int Navigator::findGroup(const Group &g) const {
        for (int i = 0; i < (int) groupList.size(); i++) {
            if (groupList[(size_t) i] == g) {
                return i;
            }
        }
        return -1;
    }

    const std::vector<int> &Navigator::favoriteList() const {
        if (!favoritesValid) {
            favoriteCache = live.favorites(favorites);
            favoritesValid = true;
        }
        return favoriteCache;
    }

    std::vector<int> Navigator::channelsOf(const Group &g) const {
        switch (g.kind) {
            case Group::Kind::Results:
                return opener;
            case Group::Kind::Favorites:
                return favoriteList();
            case Group::Kind::Category:
                return live.inCategory(g.categoryId);
            default: {
                std::vector<int> all(live.providerCount());   // the source's channels (user groups have their own)
                for (size_t i = 0; i < all.size(); i++) {
                    all[i] = (int) i;
                }
                return all;
            }
        }
    }

    int Navigator::count(int group) const {
        if (group < 0 || group >= (int) groupList.size()) {
            return 0;
        }
        const Group &g = groupList[(size_t) group];
        switch (g.kind) {
            case Group::Kind::Results:
                return (int) opener.size();
            case Group::Kind::Favorites:
                return (int) favoriteList().size();
            case Group::Kind::Category:
                return live.countInCategory(g.categoryId);
            default:
                return (int) live.providerCount();
        }
    }

    std::string Navigator::name(int group) const {
        if (group < 0 || group >= (int) groupList.size()) {
            return "";
        }
        const Group &g = groupList[(size_t) group];
        switch (g.kind) {
            case Group::Kind::Results:
                return i18n::tr("live.search_results");
            case Group::Kind::Favorites:
                return i18n::tr("home.favorites");
            case Group::Kind::Category:
                return live.categoryName(g.categoryId);
            default:
                return i18n::tr("live.all_channels");
        }
    }

    void Navigator::show(int group) {
        list = channelsOf(groupList[(size_t) group]);
        sel = 0;
        for (size_t i = 0; i < list.size(); i++) {
            if (list[i] == playingIndex) {
                sel = (int) i;
                break;
            }
        }
    }

    void Navigator::openChannels() {
        open = Panel::Channels;
        highlight = before = shown;
        show(shown);
    }

    void Navigator::refresh() {
        favoritesValid = false;
        int keep = sel >= 0 && sel < (int) list.size() ? list[(size_t) sel] : -1;
        list = channelsOf(groupList[(size_t) (open == Panel::Groups ? highlight : shown)]);
        sel = 0;
        for (size_t i = 0; i < list.size(); i++) {
            if (list[i] == keep) {
                sel = (int) i;
                break;
            }
        }
    }

    void Navigator::setPlaying(int catalogIndex) {
        playingIndex = catalogIndex;
    }

    void Navigator::setPageSize(int rows) {
        page = std::max(1, rows);
    }

    void Navigator::close() {
        if (open == Panel::Groups) {
            shown = before;   // closing from the group list keeps the group that was open
        }
        open = Panel::None;
    }

    bool Navigator::tick(double now) {
        if (open == Panel::None) {
            return false;
        }
        double idle = open == Panel::Guide ? IDLE_CLOSE_GUIDE : IDLE_CLOSE_LIST;
        if (now - lastInput < idle) {
            return false;
        }
        close();
        return true;
    }

    Outcome Navigator::press(PadButton b, bool repeat, double now) {
        Outcome o;
        lastInput = now;
        auto moveIn = [&](int &value, int size, int delta) {
            int target = std::min(std::max(value + delta, 0), std::max(size - 1, 0));
            o.changed = target != value;
            value = target;
        };
        switch (open) {
            case Panel::None:
                if (b == PadButton::Left && !repeat) {
                    openChannels();
                    o.handled = o.changed = true;
                } else if (b == PadButton::Right && !repeat) {
                    open = Panel::Guide;
                    o.handled = o.changed = true;
                }
                return o;   // everything else: the player's own controls

            case Panel::Guide:
                switch (b) {
                    case PadButton::Left:
                        if (!repeat) {
                            openChannels();
                            o.changed = true;
                        }
                        o.handled = true;
                        return o;
                    case PadButton::Circle:
                    case PadButton::Right:
                    case PadButton::Cross:
                        if (!repeat) {
                            open = Panel::None;
                            o.changed = true;
                        }
                        o.handled = true;
                        return o;
                    case PadButton::Triangle:
                        o.handled = true;   // the technical info would cover the guide
                        return o;
                    default:
                        return o;   // Up / Down / L1 / R1 zap and the guide follows; Square: the playing channel
                }

            case Panel::Channels:
                o.handled = true;
                switch (b) {
                    case PadButton::Up:
                        moveIn(sel, (int) list.size(), -1);
                        return o;
                    case PadButton::Down:
                        moveIn(sel, (int) list.size(), 1);
                        return o;
                    case PadButton::L2:
                        moveIn(sel, (int) list.size(), -page);
                        return o;
                    case PadButton::R2:
                        moveIn(sel, (int) list.size(), page);
                        return o;
                    case PadButton::L1:
                    case PadButton::R1: {
                        int target = shown;
                        moveIn(target, (int) groupList.size(), b == PadButton::L1 ? -1 : 1);
                        if (o.changed) {
                            shown = highlight = before = target;
                            show(shown);
                        }
                        return o;
                    }
                    case PadButton::Left:
                        if (!repeat) {
                            open = Panel::Groups;
                            highlight = before = shown;
                            o.changed = true;
                        }
                        return o;
                    case PadButton::Right:
                    case PadButton::Circle:
                        if (!repeat) {
                            open = Panel::None;
                            o.changed = true;
                        }
                        return o;
                    case PadButton::Cross:
                        if (repeat || list.empty()) {
                            return o;
                        }
                        open = Panel::None;
                        o.changed = true;
                        if (list[(size_t) sel] != playingIndex) {
                            o.play = true;
                            o.list = list;
                            o.index = sel;
                            playingIndex = list[(size_t) sel];
                        }
                        return o;
                    default:
                        return o;   // Square / Triangle / Options: nothing hidden behind the list
                }

            case Panel::Groups:
                o.handled = true;
                switch (b) {
                    case PadButton::Up:
                    case PadButton::L1:
                        moveIn(highlight, (int) groupList.size(), -1);
                        break;
                    case PadButton::Down:
                    case PadButton::R1:
                        moveIn(highlight, (int) groupList.size(), 1);
                        break;
                    case PadButton::L2:
                        moveIn(highlight, (int) groupList.size(), -page);
                        break;
                    case PadButton::R2:
                        moveIn(highlight, (int) groupList.size(), page);
                        break;
                    case PadButton::Cross:
                    case PadButton::Right:
                        if (!repeat) {
                            shown = before = highlight;   // the previewed channels stay; the focus moves to them
                            open = Panel::Channels;
                            o.changed = true;
                        }
                        return o;
                    case PadButton::Circle:
                        if (!repeat) {
                            shown = highlight = before;
                            open = Panel::Channels;
                            show(shown);
                            o.changed = true;
                        }
                        return o;
                    default:
                        return o;
                }
                if (o.changed) {
                    show(highlight);   // the channel column previews the highlighted group
                }
                return o;
        }
        return o;
    }
}
