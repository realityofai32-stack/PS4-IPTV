// In-memory Live TV catalog with per-category indices and local search (host-testable).

#ifndef PS4IPTV_IPTV_CATALOG_H
#define PS4IPTV_IPTV_CATALOG_H

#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "models.h"

namespace iptv {

    class LiveCatalog {

    public:

        // takes ownership of the lists and builds the indices
        void assign(std::vector<Category> categories, std::vector<LiveChannel> channels);

        const std::vector<Category> &categories() const { return cats; }

        const std::vector<LiveChannel> &channels() const { return list; }

        const LiveChannel *find(const std::string &streamId) const;

        // indices into channels() of one category, in provider order
        const std::vector<int> &inCategory(const std::string &categoryId) const;

        // indices of favorite channels, in favorite-set order of channels()
        std::vector<int> favorites(const std::set<std::string> &favoriteIds) const;

        // accent/case-insensitive substring search on channel names; best matches (prefix) first
        std::vector<int> search(const std::string &query, size_t limit = 200) const;

        int countInCategory(const std::string &categoryId) const { return (int) inCategory(categoryId).size(); }

        bool empty() const { return list.empty(); }

        int64_t loadedAt = 0;
        bool fromCache = false;

    private:

        std::vector<Category> cats;
        std::vector<LiveChannel> list;
        std::unordered_map<std::string, int> byId;
        std::unordered_map<std::string, std::vector<int>> byCategory;
        std::vector<std::u32string> folded;   // search keys
        std::vector<int> none;
    };
}

#endif // PS4IPTV_IPTV_CATALOG_H
