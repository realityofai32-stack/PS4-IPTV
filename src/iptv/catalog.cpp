#include <algorithm>

#include "catalog.h"
#include "xtream.h"
#include "../i18n/i18n.h"

namespace iptv {

    const char *const UNCATEGORIZED_ID = "\x01uncategorized";
    const char *uncategorizedName() {
        return i18n::tr("catalog.uncategorized").c_str();
    }

    void LiveCatalog::assign(std::vector<Category> categories, std::vector<LiveChannel> channels) {
        cats = std::move(categories);
        list = std::move(channels);
        byId.clear();
        byCategory.clear();
        std::vector<std::string> names, aliases;
        names.reserve(list.size());
        aliases.reserve(list.size());
        for (int i = 0; i < (int) list.size(); i++) {
            byId[list[(size_t) i].id] = i;
            byCategory[list[(size_t) i].categoryId].push_back(i);
            names.push_back(list[(size_t) i].name);
            aliases.push_back(xtream::channelNameWithoutPrefix(list[(size_t) i].name));   // "TRT 1" for "TR: TRT 1"
        }
        index.build(names, aliases);
    }

    const LiveChannel *LiveCatalog::find(const std::string &id) const {
        auto it = byId.find(id);
        return it == byId.end() ? nullptr : &list[(size_t) it->second];
    }

    std::string LiveCatalog::categoryName(const std::string &categoryId) const {
        if (categoryId == UNCATEGORIZED_ID) {
            return uncategorizedName();   // localized when shown, not when the list was parsed
        }
        for (const auto &c: cats) {
            if (c.id == categoryId) {
                return c.name;
            }
        }
        return "";
    }

    int LiveCatalog::withLogo() const {
        int n = 0;
        for (const auto &c: list) {
            n += !c.icon.empty();
        }
        return n;
    }

    const std::vector<int> &LiveCatalog::inCategory(const std::string &categoryId) const {
        auto it = byCategory.find(categoryId);
        return it == byCategory.end() ? none : it->second;
    }

    std::vector<int> LiveCatalog::favorites(const std::set<std::string> &favoriteIds) const {
        std::vector<int> out;
        for (int i = 0; i < (int) list.size(); i++) {
            if (favoriteIds.count(list[(size_t) i].id)) {
                out.push_back(i);
            }
        }
        return out;
    }

    std::vector<int> LiveCatalog::search(const std::string &query, size_t limit) const {
        std::vector<int> out;
        for (const auto &h: index.find(query, limit).hits) {
            out.push_back(h.item);
        }
        return out;
    }

    // ------------------------------------------------------------------ sorting

    namespace {
        const char *const SORT_KEYS[] = {"provider", "az", "za", "added_desc", "added_asc", "year_desc",
                                         "year_asc", "rating_desc", "watched"};
    }

    const char *sortModeKey(SortMode mode) {
        int i = (int) mode;
        return i >= 0 && i < (int) SortMode::Count ? SORT_KEYS[i] : SORT_KEYS[0];
    }

    SortMode sortModeFromKey(const std::string &key) {
        for (int i = 0; i < (int) SortMode::Count; i++) {
            if (key == SORT_KEYS[i]) {
                return (SortMode) i;
            }
        }
        return SortMode::Provider;
    }

    const char *sortModeName(SortMode mode, ContentType type) {
        bool series = type == ContentType::Series;
        switch (mode) {
            case SortMode::TitleAZ:
                return i18n::tr("sort.az").c_str();
            case SortMode::TitleZA:
                return i18n::tr("sort.za").c_str();
            case SortMode::AddedNewest:
                return i18n::tr(series ? "sort.updated_newest" : "sort.added_newest").c_str();
            case SortMode::AddedOldest:
                return i18n::tr(series ? "sort.updated_oldest" : "sort.added_oldest").c_str();
            case SortMode::YearNewest:
                return i18n::tr("sort.year_newest").c_str();
            case SortMode::YearOldest:
                return i18n::tr("sort.year_oldest").c_str();
            case SortMode::RatingHigh:
                return i18n::tr("sort.rating").c_str();
            case SortMode::RecentlyWatched:
                return i18n::tr("sort.recently_watched").c_str();
            default:
                return i18n::tr("sort.provider").c_str();
        }
    }

    bool SortSupport::supports(SortMode mode) const {
        switch (mode) {
            case SortMode::AddedNewest:
            case SortMode::AddedOldest:
                return added;
            case SortMode::YearNewest:
            case SortMode::YearOldest:
                return year;
            case SortMode::RatingHigh:
                return rating;
            case SortMode::Count:
                return false;
            default:
                return true;
        }
    }
}
