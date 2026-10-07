#include <algorithm>

#include "catalog.h"
#include "xtream.h"

namespace iptv {

    const char *const UNCATEGORIZED_ID = "\x01uncategorized";
    const char *const UNCATEGORIZED_NAME = "Uncategorized";

    void LiveCatalog::assign(std::vector<Category> categories, std::vector<LiveChannel> channels) {
        cats = std::move(categories);
        list = std::move(channels);
        byId.clear();
        byCategory.clear();
        std::vector<std::string> names, aliases;
        names.reserve(list.size());
        aliases.reserve(list.size());
        for (int i = 0; i < (int) list.size(); i++) {
            byId[list[(size_t) i].streamId] = i;
            byCategory[list[(size_t) i].categoryId].push_back(i);
            names.push_back(list[(size_t) i].name);
            aliases.push_back(xtream::channelNameWithoutPrefix(list[(size_t) i].name));   // "TRT 1" for "TR: TRT 1"
        }
        index.build(names, aliases);
    }

    const LiveChannel *LiveCatalog::find(const std::string &streamId) const {
        auto it = byId.find(streamId);
        return it == byId.end() ? nullptr : &list[(size_t) it->second];
    }

    const std::vector<int> &LiveCatalog::inCategory(const std::string &categoryId) const {
        auto it = byCategory.find(categoryId);
        return it == byCategory.end() ? none : it->second;
    }

    std::vector<int> LiveCatalog::favorites(const std::set<std::string> &favoriteIds) const {
        std::vector<int> out;
        for (int i = 0; i < (int) list.size(); i++) {
            if (favoriteIds.count(list[(size_t) i].streamId)) {
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
                return "A \xE2\x86\x92 Z";
            case SortMode::TitleZA:
                return "Z \xE2\x86\x92 A";
            case SortMode::AddedNewest:
                return series ? "Recently updated" : "Newest added";
            case SortMode::AddedOldest:
                return series ? "Least recently updated" : "Oldest added";
            case SortMode::YearNewest:
                return "Year: Newest";
            case SortMode::YearOldest:
                return "Year: Oldest";
            case SortMode::RatingHigh:
                return "Rating: High to low";
            case SortMode::RecentlyWatched:
                return "Recently watched";
            default:
                return "Provider order";
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
