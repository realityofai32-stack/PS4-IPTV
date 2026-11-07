// Home's L1 / R1 (keyboard: Shift+Tab / Tab, the centralized mapping): straight through the top navigation -
// Home, Live TV, Movies, Series, Local Media, Search, Favorites, Downloads, Settings - from anywhere on Home
// (host-testable). From the hero or a rail the focus goes up to the navigation: to the item before / after the one the
// navigation last had (Home at first). The ends stop (no wrap). The D-pad is unchanged; L1 / R1 never scroll a rail.

#ifndef PS4IPTV_APP_HOME_NAV_H
#define PS4IPTV_APP_HOME_NAV_H

#include <algorithm>

namespace homenav {

    // the navigation item L1 (dir -1) / R1 (dir +1) focuses; `navIndex`: the navigation's last focus, `count`: items
    inline int shoulder(int navIndex, int count, int dir) {
        if (count <= 0) {
            return 0;
        }
        int at = std::min(std::max(navIndex, 0), count - 1);
        return std::min(std::max(at + (dir < 0 ? -1 : 1), 0), count - 1);
    }
}

#endif // PS4IPTV_APP_HOME_NAV_H
