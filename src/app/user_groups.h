// Live TV groups from the user's playlists (media/user_playlists.h) - host-testable.
//
// Every playlist shown in Live TV (My Playlists: the order, and "Show in Live TV" / "Hide from Live TV"; kept in the
// settings as playlistOrder / hiddenPlaylists) becomes a group in the Live TV group list - and in the live player's
// group list - before the provider's categories, after Favorites / All Channels, in the user's order
// (iptv::LiveCatalog::setUserGroups). Its entries become:
//   - the source's own channel when the entry was added from this source and the channel still exists
//     (v7-source = the signed-in profile, v7-channel = its id), or when a playlist source has a channel with the same
//     address: an index into the source's list, nothing copied - favorites, history, the guide and zapping are the
//     channel's own;
//   - otherwise one extra channel (name, address, logo, User-Agent, tvg-id) after the provider's channels: an
//     address added by hand, a channel added from another source (its own address plays).
// The same channel twice in one playlist is listed once, and one address is one extra channel whichever playlists
// list it; an entry with no address the source does not have is skipped. All Channels stays the provider's list.
// Ids: groups "userpl:<file>", extra channels "u" + a hash of the address (stable across launches and playlist
// edits: favorites / history keep them).

#ifndef PS4IPTV_APP_USER_GROUPS_H
#define PS4IPTV_APP_USER_GROUPS_H

#include <set>
#include <string>
#include <vector>

#include "../iptv/catalog.h"
#include "../media/user_playlists.h"

namespace usergroups {

    struct Playlist {
        std::string file;      // "sports.m3u"
        std::string title;
        std::vector<userpl::Entry> entries;
    };

    struct Overlay {
        std::vector<iptv::LiveChannel> extra;
        std::vector<iptv::LiveCatalog::UserGroup> groups;
        int referenced = 0;    // entries that are the source's own channels
        int added = 0;         // extra channels
        int skipped = 0;       // no address and not a channel of this source
    };

    // "userpl:<file>"
    std::string groupId(const std::string &file);

    // live: the signed-in source's catalog (its provider channels are used; earlier user groups are ignored);
    // profileId: that source
    Overlay build(const iptv::LiveCatalog &live, const std::string &profileId, const std::vector<Playlist> &playlists);

    // My Playlists in the user's order: the files of `order` that exist, then the others (by title)
    std::vector<userpl::Summary> ordered(const std::vector<std::string> &order, std::vector<userpl::Summary> onDisk);

    // the playlists shown as Live TV groups, in order: editable ones that are not hidden
    std::vector<std::string> shownFiles(const std::vector<std::string> &order, const std::set<std::string> &hidden,
                                        const std::vector<userpl::Summary> &onDisk);
}

#endif // PS4IPTV_APP_USER_GROUPS_H
