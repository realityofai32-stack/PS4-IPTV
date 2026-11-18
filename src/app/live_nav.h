// In-player Live TV navigation (host-testable): what the live player's channel list, group list and guide panel show
// and do. screens/player_screen.cpp draws them and plays what this decides.
//
//   video  --Left-->  the current group's channels  --Left-->  the groups (the channel column follows the highlight)
//   video  <--Circle--                              <--Circle-- (back to the group shown before)
//   video  --Right--> the playing channel's guide   --Circle / Right / Cross--> video   (Left: the channel list)
//
// Groups are the Live TV screen's: Favorites, All channels, the user's playlists shown in Live TV (app/user_groups.h),
// then the source's categories; a player opened from search results lists them first. In a list Up / Down move and L2 / R2 page; in the channel list L1 / R1 step to the
// previous / next group. Cross (or Right) on a group opens its channels; Cross on a channel switches playback to it
// without leaving the player - its group then becomes the list Up / Down zap through - and closes the list (Cross on
// the playing channel just closes it). Up / Down outside the lists zap as before, also with the guide open (the
// guide follows the channel). A panel left alone closes by itself.
//
// Everything is indices into the session's LiveCatalog and the favorite ids the app already holds: no copy of the
// provider data, no request.

#ifndef PS4IPTV_APP_LIVE_NAV_H
#define PS4IPTV_APP_LIVE_NAV_H

#include <set>
#include <string>
#include <vector>

#include "../iptv/catalog.h"
#include "../platform/input.h"

namespace livenav {

    enum class Panel {
        None,       // the video alone (the player's own controls)
        Channels,   // the channel list of the shown group
        Groups,     // the group list, next to the channel list
        Guide       // the playing channel's programme guide
    };

    struct Group {
        enum class Kind {
            Auto,       // from the list the player was opened with: one channel -> its category, more -> Results
            Results,    // the list the player was opened with (search results)
            Favorites,
            All,
            Category
        };
        Kind kind = Kind::Auto;
        std::string categoryId;   // Category

        static Group of(Kind k, const std::string &category = "") {
            Group g;
            g.kind = k;
            g.categoryId = category;
            return g;
        }

        bool operator==(const Group &o) const { return kind == o.kind && categoryId == o.categoryId; }
    };

    // what the player does after an input
    struct Outcome {
        bool handled = false;   // false: not for the navigation (the player's own controls: zap, favorite, retry...)
        bool changed = false;   // something to redraw
        bool play = false;      // switch playback to list[index]: the group's channels become the zap list
        std::vector<int> list;
        int index = 0;
    };

    class Navigator {

    public:

        static constexpr double IDLE_CLOSE_LIST = 12.0;   // seconds without input before a list closes by itself
        static constexpr double IDLE_CLOSE_GUIDE = 20.0;  // the guide: time to read a description

        // live / favoriteIds: the session's catalog and the favorite channel ids (both outlive the navigator);
        // opener / openerGroup: the list the player was opened with and what it is; playing: a catalog index
        Navigator(const iptv::LiveCatalog &live, const std::set<std::string> &favoriteIds, std::vector<int> opener,
                  Group openerGroup, int playing);

        Outcome press(PadButton button, bool repeat, double now);

        // true: an open panel closed by itself (idle)
        bool tick(double now);

        // the player switched channel itself (zap): the lists mark it, the guide follows it
        void setPlaying(int catalogIndex);

        // rows a page jump (L2 / R2) moves (the list's visible rows minus one)
        void setPageSize(int rows);

        // the favorites changed (call after every change): the lists are built again (the selection stays on its
        // channel when it can)
        void refresh();

        void close();

        Panel panel() const { return open; }

        const std::vector<Group> &groups() const { return groupList; }

        // the highlighted group in the group list, otherwise the group whose channels are shown
        int groupSelected() const { return open == Panel::Groups ? highlight : shown; }

        int groupShown() const { return open == Panel::Groups ? highlight : shown; }

        // the channels in the channel column (catalog indices)
        const std::vector<int> &channels() const { return list; }

        int channelSelected() const { return sel; }

        int playing() const { return playingIndex; }

        int count(int group) const;

        // localized group name
        std::string name(int group) const;

    private:

        std::vector<int> channelsOf(const Group &g) const;

        // the channel column shows `group`, the selection on the playing channel (else the first channel)
        void show(int group);

        void openChannels();

        int findGroup(const Group &g) const;

        // the favorite channels: built by scanning the catalog, so built once and again only after refresh()
        const std::vector<int> &favoriteList() const;

        const iptv::LiveCatalog &live;
        const std::set<std::string> &favorites;
        std::vector<int> opener;
        std::vector<Group> groupList;
        int shown = 0;        // the group of the channel column (committed)
        int highlight = 0;    // the group list's highlight
        int before = 0;       // the group shown when the group list opened (Circle goes back to it)
        std::vector<int> list;
        int sel = 0;
        int playingIndex = -1;
        int page = 8;
        Panel open = Panel::None;
        double lastInput = 0;
        mutable std::vector<int> favoriteCache;
        mutable bool favoritesValid = false;
    };
}

#endif // PS4IPTV_APP_LIVE_NAV_H
