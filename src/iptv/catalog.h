// In-memory Live TV / Movies / Series catalogs with per-category indices and local search (host-testable).

#ifndef PS4IPTV_IPTV_CATALOG_H
#define PS4IPTV_IPTV_CATALOG_H

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "models.h"
#include "../core/utf8.h"

namespace iptv {

    // accent/case-insensitive substring search over pre-folded names; prefix matches first, then word starts,
    // then any substring
    std::vector<int> rankedSearch(const std::vector<std::u32string> &folded, const std::string &query,
                                  size_t limit);

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

    inline const std::string &itemId(const Movie &m) { return m.streamId; }

    inline const std::string &itemId(const Series &s) { return s.seriesId; }

    // Movies or Series: categories, per-category indices, id lookup, search, "recently added".
    // Built on a worker thread (assign), then moved into the session.
    template<typename T>
    class ItemCatalog {

    public:

        void assign(std::vector<Category> categories, std::vector<T> items) {
            cats = std::move(categories);
            list = std::move(items);
            byId.clear();
            byCategory.clear();
            folded.clear();
            folded.reserve(list.size());
            byId.reserve(list.size());
            for (int i = 0; i < (int) list.size(); i++) {
                const T &t = list[(size_t) i];
                byId[itemId(t)] = i;
                byCategory[t.categoryId].push_back(i);
                folded.push_back(utf8::foldForSearch(t.name));
            }
            // hide categories without items (panels often list empty ones)
            std::vector<Category> kept;
            for (auto &c: cats) {
                if (byCategory.count(c.id)) {
                    kept.push_back(std::move(c));
                }
            }
            cats = std::move(kept);
        }

        const std::vector<Category> &categories() const { return cats; }

        const std::vector<T> &items() const { return list; }

        size_t size() const { return list.size(); }

        bool empty() const { return list.empty(); }

        const T *find(const std::string &id) const {
            auto it = byId.find(id);
            return it == byId.end() ? nullptr : &list[(size_t) it->second];
        }

        const std::vector<int> &inCategory(const std::string &categoryId) const {
            auto it = byCategory.find(categoryId);
            return it == byCategory.end() ? none : it->second;
        }

        std::vector<int> all() const {
            std::vector<int> out(list.size());
            for (size_t i = 0; i < out.size(); i++) {
                out[i] = (int) i;
            }
            return out;
        }

        std::vector<int> favorites(const std::set<std::string> &ids) const {
            std::vector<int> out;
            for (const auto &id: ids) {
                auto it = byId.find(id);
                if (it != byId.end()) {
                    out.push_back(it->second);
                }
            }
            std::sort(out.begin(), out.end());
            return out;
        }

        // newest first by the `added` / `last_modified` time
        std::vector<int> recent(size_t limit) const {
            std::vector<int> out = all();
            std::stable_sort(out.begin(), out.end(), [this](int a, int b) {
                return addedOf(list[(size_t) a]) > addedOf(list[(size_t) b]);
            });
            if (out.size() > limit) {
                out.resize(limit);
            }
            return out;
        }

        std::vector<int> search(const std::string &query, size_t limit = 200) const {
            return rankedSearch(folded, query, limit);
        }

        int64_t savedAt = 0;      // when the data was fetched from the provider
        bool fromCache = false;

    private:

        static int64_t addedOf(const Movie &m) { return m.added; }

        static int64_t addedOf(const Series &s) { return s.lastModified; }

        std::vector<Category> cats;
        std::vector<T> list;
        std::unordered_map<std::string, int> byId;
        std::unordered_map<std::string, std::vector<int>> byCategory;
        std::vector<std::u32string> folded;
        std::vector<int> none;
    };

    using MovieCatalog = ItemCatalog<Movie>;
    using SeriesCatalog = ItemCatalog<Series>;
}

#endif // PS4IPTV_IPTV_CATALOG_H
