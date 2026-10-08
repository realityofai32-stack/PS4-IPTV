// In-memory Live TV / Movies / Series catalogs with per-category indices, a prebuilt search index and
// sort keys (host-testable).

#ifndef PS4IPTV_IPTV_CATALOG_H
#define PS4IPTV_IPTV_CATALOG_H

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "models.h"
#include "search_index.h"
#include "sorting.h"

namespace iptv {

    // virtual category of the items whose category_id is missing or not in the category list
    extern const char *const UNCATEGORIZED_ID;
    const char *uncategorizedName();   // localized ("Uncategorized")

    class LiveCatalog {

    public:

        // takes ownership of the lists and builds the indices (incl. the search index: 573 channels, cheap)
        void assign(std::vector<Category> categories, std::vector<LiveChannel> channels);

        const std::vector<Category> &categories() const { return cats; }

        const std::vector<LiveChannel> &channels() const { return list; }

        const LiveChannel *find(const std::string &id) const;

        // display name of a category id ("" when unknown; Uncategorized in the UI language)
        std::string categoryName(const std::string &categoryId) const;

        // indices into channels() of one category, in provider order
        const std::vector<int> &inCategory(const std::string &categoryId) const;

        // indices of favorite channels, in favorite-set order of channels()
        std::vector<int> favorites(const std::set<std::string> &favoriteIds) const;

        // ranked search on channel names (see search::Index)
        search::Result searchRanked(const std::string &query, size_t limit) const { return index.find(query, limit); }

        std::vector<int> search(const std::string &query, size_t limit = 200) const;

        const search::Index &searchIndex() const { return index; }

        int countInCategory(const std::string &categoryId) const { return (int) inCategory(categoryId).size(); }

        // channels with a logo URL (playlist diagnostics)
        int withLogo() const;

        bool empty() const { return list.empty(); }

        int64_t loadedAt = 0;
        bool fromCache = false;

    private:

        std::vector<Category> cats;
        std::vector<LiveChannel> list;
        std::unordered_map<std::string, int> byId;
        std::unordered_map<std::string, std::vector<int>> byCategory;
        search::Index index;
        std::vector<int> none;
    };

    inline const std::string &itemId(const Movie &m) { return m.streamId; }

    inline const std::string &itemId(const Series &s) { return s.seriesId; }

    // provider metadata used by the sort modes
    inline int64_t addedOf(const Movie &m) { return m.added; }

    inline int64_t addedOf(const Series &s) { return s.lastModified; }

    // sorts `subset` (indices into a catalog) in place. keys: the normalized title of every item;
    // activity: last playback activity per item (RecentlyWatched; 0 = never), may be null otherwise.
    // Items without the sorted value (no year, no rating...) keep provider order after the others.
    template<typename T>
    void sortItems(std::vector<int> &subset, SortMode mode, const std::vector<T> &items,
                   const std::vector<int> &titleRank, const std::vector<int64_t> *activity) {
        auto byProvider = [](int a, int b) { return a < b; };
        auto numeric = [&](auto value, bool descending) {
            std::stable_sort(subset.begin(), subset.end(), [&](int a, int b) {
                auto va = value(items[(size_t) a]);
                auto vb = value(items[(size_t) b]);
                bool ha = va > 0;
                bool hb = vb > 0;
                if (ha != hb) {
                    return ha;   // unknown values last, both directions
                }
                if (!ha || va == vb) {
                    return byProvider(a, b);
                }
                return descending ? va > vb : va < vb;
            });
        };
        switch (mode) {
            case SortMode::TitleAZ:
            case SortMode::TitleZA: {
                bool az = mode == SortMode::TitleAZ;
                std::sort(subset.begin(), subset.end(), [&](int a, int b) {
                    int ra = titleRank[(size_t) a];
                    int rb = titleRank[(size_t) b];
                    return az ? ra < rb : ra > rb;
                });
                break;
            }
            case SortMode::AddedNewest:
            case SortMode::AddedOldest:
                numeric([](const T &t) { return addedOf(t); }, mode == SortMode::AddedNewest);
                break;
            case SortMode::YearNewest:
            case SortMode::YearOldest:
                numeric([](const T &t) { return t.year; }, mode == SortMode::YearNewest);
                break;
            case SortMode::RatingHigh:
                numeric([](const T &t) { return t.rating; }, true);
                break;
            case SortMode::RecentlyWatched:
                if (activity != nullptr && activity->size() == items.size()) {
                    std::stable_sort(subset.begin(), subset.end(), [&](int a, int b) {
                        int64_t va = (*activity)[(size_t) a];
                        int64_t vb = (*activity)[(size_t) b];
                        return va != vb ? va > vb : byProvider(a, b);
                    });
                    break;
                }
                std::sort(subset.begin(), subset.end(), byProvider);
                break;
            default:
                std::sort(subset.begin(), subset.end(), byProvider);
                break;
        }
    }

    // Movies or Series: categories (+ Uncategorized), per-category indices, id lookup, search, sorting.
    // Built on a worker thread (assign, then buildSearch / buildSortKeys), then moved into the session.
    template<typename T>
    class ItemCatalog {

    public:

        // takes ownership; identity is the item id (the parsers already dropped repeated ids)
        void assign(std::vector<Category> categories, std::vector<T> items) {
            cats = std::move(categories);
            list = std::move(items);
            byId.clear();
            byCategory.clear();
            byId.reserve(list.size());
            std::unordered_set<std::string> known;
            for (const auto &c: cats) {
                known.insert(c.id);
            }
            std::vector<int> uncategorized;
            for (int i = 0; i < (int) list.size(); i++) {
                const T &t = list[(size_t) i];
                byId.emplace(itemId(t), i);
                if (known.count(t.categoryId)) {
                    byCategory[t.categoryId].push_back(i);
                } else {
                    uncategorized.push_back(i);   // no or unknown category: still listed, never dropped
                }
            }
            // hide categories without items (panels often list empty ones)
            std::vector<Category> kept;
            for (auto &c: cats) {
                if (byCategory.count(c.id)) {
                    kept.push_back(std::move(c));
                }
            }
            cats = std::move(kept);
            if (!uncategorized.empty()) {
                cats.push_back({UNCATEGORIZED_ID, uncategorizedName(), ""});
                byCategory[UNCATEGORIZED_ID] = std::move(uncategorized);
            }
            diag.visible = (int) list.size();
            diag.uncategorized = (int) inCategory(UNCATEGORIZED_ID).size();
            diag.categories = (int) cats.size();
            support = SortSupport();
            int added = 0, year = 0, rating = 0;
            for (const T &t: list) {
                added += addedOf(t) > 0;
                year += t.year > 0;
                rating += t.rating > 0;
            }
            // a mode is offered when at least a tenth of the items carry its value (never fabricated)
            int need = std::max(1, (int) list.size() / 10);
            support.added = added >= need;
            support.year = year >= need;
            support.rating = rating >= need;
            index.clear();
            titleRank.clear();
        }

        // search by the provider name and, for "Title 2026" names, the title without the year
        void buildSearch() {
            std::vector<std::string> texts, aliases;
            texts.reserve(list.size());
            aliases.reserve(list.size());
            for (const T &t: list) {
                texts.push_back(t.name);
                aliases.push_back(t.title);
            }
            index.build(texts, aliases);
            diag.indexed = index.size();
            diag.indexBytes = index.memoryBytes();
        }

        // A -> Z order of the normalized titles (needs buildSearch first)
        void buildSortKeys() {
            std::vector<int> order = all();
            std::vector<std::string> keys(list.size());
            for (size_t i = 0; i < list.size(); i++) {
                keys[i] = search::normalize(list[i].title.empty() ? list[i].name : list[i].title);
            }
            std::stable_sort(order.begin(), order.end(), [&keys](int a, int b) {
                return keys[(size_t) a] < keys[(size_t) b];
            });
            titleRank.assign(list.size(), 0);
            for (int r = 0; r < (int) order.size(); r++) {
                titleRank[(size_t) order[(size_t) r]] = r;
            }
        }

        // assign + search index + sort keys
        void prepare(std::vector<Category> categories, std::vector<T> items) {
            assign(std::move(categories), std::move(items));
            buildSearch();
            buildSortKeys();
        }

        const std::vector<Category> &categories() const { return cats; }

        const std::vector<T> &items() const { return list; }

        size_t size() const { return list.size(); }

        bool empty() const { return list.empty(); }

        const T *find(const std::string &id) const {
            auto it = byId.find(id);
            return it == byId.end() ? nullptr : &list[(size_t) it->second];
        }

        int indexOf(const std::string &id) const {
            auto it = byId.find(id);
            return it == byId.end() ? -1 : it->second;
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

        search::Result searchRanked(const std::string &query, size_t limit) const { return index.find(query, limit); }

        // indices only, best first
        std::vector<int> search(const std::string &query, size_t limit = 200) const {
            std::vector<int> out;
            for (const auto &h: index.find(query, limit).hits) {
                out.push_back(h.item);
            }
            return out;
        }

        const SortSupport &sortSupport() const { return support; }

        // sorts indices of this catalog in place (see sortItems)
        void sort(std::vector<int> &subset, SortMode mode, const std::vector<int64_t> *activity = nullptr) const {
            if (titleRank.size() != list.size() && (mode == SortMode::TitleAZ || mode == SortMode::TitleZA)) {
                return;   // sort keys not built
            }
            sortItems(subset, mode, list, titleRank, activity);
        }

        const CatalogDiagnostics &diagnostics() const { return diag; }

        CatalogDiagnostics &diagnostics() { return diag; }

        int64_t savedAt = 0;      // when the data was fetched from the provider
        bool fromCache = false;

    private:

        std::vector<Category> cats;
        std::vector<T> list;
        std::unordered_map<std::string, int> byId;
        std::unordered_map<std::string, std::vector<int>> byCategory;
        search::Index index;
        std::vector<int> titleRank;
        SortSupport support;
        CatalogDiagnostics diag;
        std::vector<int> none;
    };

    using MovieCatalog = ItemCatalog<Movie>;
    using SeriesCatalog = ItemCatalog<Series>;
}

#endif // PS4IPTV_IPTV_CATALOG_H
