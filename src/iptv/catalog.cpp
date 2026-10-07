#include <algorithm>

#include "catalog.h"
#include "../core/utf8.h"

namespace iptv {

    void LiveCatalog::assign(std::vector<Category> categories, std::vector<LiveChannel> channels) {
        cats = std::move(categories);
        list = std::move(channels);
        byId.clear();
        byCategory.clear();
        folded.clear();
        folded.reserve(list.size());
        for (int i = 0; i < (int) list.size(); i++) {
            byId[list[(size_t) i].streamId] = i;
            byCategory[list[(size_t) i].categoryId].push_back(i);
            folded.push_back(utf8::foldForSearch(list[(size_t) i].name));
        }
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
        return rankedSearch(folded, query, limit);
    }

    std::vector<int> rankedSearch(const std::vector<std::u32string> &folded, const std::string &query, size_t limit) {
        std::u32string q = utf8::foldForSearch(query);
        while (!q.empty() && q.back() == U' ') {
            q.pop_back();
        }
        while (!q.empty() && q.front() == U' ') {
            q.erase(q.begin());
        }
        // ranking: name prefix, then word start ("trt" in "TR: TRT 1"), then any substring
        std::vector<int> prefix;
        std::vector<int> wordStart;
        std::vector<int> contains;
        if (q.empty()) {
            return prefix;
        }
        for (int i = 0; i < (int) folded.size() && prefix.size() < limit; i++) {
            const std::u32string &name = folded[(size_t) i];
            size_t pos = name.find(q);
            if (pos == std::u32string::npos) {
                continue;
            }
            if (pos == 0) {
                prefix.push_back(i);
                continue;
            }
            bool atWord = false;
            for (size_t p = pos; p != std::u32string::npos; p = name.find(q, p + 1)) {
                char32_t before = name[p - 1];
                if (before == U' ' || before == U':' || before == U'|' || before == U'-' || before == U'[' ||
                    before == U'(' || before == U'.' || before == U'/') {
                    atWord = true;
                    break;
                }
            }
            (atWord ? wordStart : contains).push_back(i);
        }
        for (const auto *bucket: {&wordStart, &contains}) {
            for (int i: *bucket) {
                if (prefix.size() >= limit) {
                    return prefix;
                }
                prefix.push_back(i);
            }
        }
        return prefix;
    }
}
