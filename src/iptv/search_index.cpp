#include <algorithm>
#include <cstddef>
#include <cstring>

#include "search_index.h"
#include "../core/utf8.h"

namespace search {

    namespace {
        bool isApostrophe(char32_t c) {
            return c == U'\'' || c == U'`' || c == 0xB4 || c == 0x2018 || c == 0x2019 || c == 0x2BC || c == 0x2032;
        }

        // letters and digits of any script; punctuation and symbol blocks are separators
        bool isWordChar(char32_t c) {
            if (c < 0x80) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            }
            if (c <= 0xBF || c == 0xD7 || c == 0xF7) {
                return false;   // Latin-1 punctuation and symbols
            }
            if ((c >= 0x2000 && c <= 0x2BFF) || (c >= 0x3000 && c <= 0x303F) || (c >= 0xFE00 && c <= 0xFE6F)
                || (c >= 0xFF00 && c <= 0xFF0F) || c == 0xFFFD || c >= 0x1F000) {
                return false;   // general punctuation, symbols, arrows, shapes, emoji
            }
            return true;
        }

        bool isLetter(char32_t c) {
            return isWordChar(c) && !(c >= '0' && c <= '9');
        }

        std::vector<std::string_view> words(std::string_view s) {
            std::vector<std::string_view> out;
            size_t start = 0;
            for (size_t i = 0; i <= s.size(); i++) {
                if (i == s.size() || s[i] == ' ') {
                    if (i > start) {
                        out.push_back(s.substr(start, i - start));
                    }
                    start = i + 1;
                }
            }
            return out;
        }

        bool startsWith(std::string_view s, std::string_view p) {
            return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
        }

        // the query occurs in s as a run of whole words
        bool hasPhrase(std::string_view s, std::string_view q) {
            for (size_t pos = s.find(q); pos != std::string_view::npos; pos = s.find(q, pos + 1)) {
                bool before = pos == 0 || s[pos - 1] == ' ';
                bool after = pos + q.size() == s.size() || s[pos + q.size()] == ' ';
                if (before && after) {
                    return true;
                }
            }
            return false;
        }

        uint32_t gram(unsigned char a, unsigned char b, unsigned char c) {
            return ((uint32_t) a << 16) | ((uint32_t) b << 8) | c;
        }

        // byte trigrams of "\1word\2" (no end marker for a word still being typed)
        void gramsOf(std::string_view w, bool endMarker, std::vector<uint32_t> &out) {
            out.clear();
            std::string padded;
            padded.reserve(w.size() + 2);
            padded += '\x01';
            padded.append(w.data(), w.size());
            if (endMarker) {
                padded += '\x02';
            }
            for (size_t i = 0; i + 3 <= padded.size(); i++) {
                out.push_back(gram((unsigned char) padded[i], (unsigned char) padded[i + 1],
                                   (unsigned char) padded[i + 2]));
            }
            std::sort(out.begin(), out.end());
            out.erase(std::unique(out.begin(), out.end()), out.end());
        }

        int maxEdits(size_t len) {
            return len >= 8 ? 2 : len >= 4 ? 1 : 0;
        }
    }

    std::string normalize(const std::string &text) {
        std::u32string s = utf8::foldForSearch(text);   // lower case, ş ğ ı İ ö ü ç é ... -> ASCII
        std::string out;
        out.reserve(text.size());
        bool space = false;
        size_t n = s.size();
        for (size_t i = 0; i < n; i++) {
            char32_t c = s[i];
            if (isApostrophe(c)) {
                continue;   // "handmaid's" -> "handmaids", "bizans'ın" -> "bizansin"
            }
            if (c == U'.' && i > 0 && i + 1 < n && isLetter(s[i - 1]) && isLetter(s[i + 1])
                && (i < 2 || s[i - 2] == U'.' || !isWordChar(s[i - 2]))
                && (i + 2 >= n || s[i + 2] == U'.' || !isWordChar(s[i + 2]))) {
                continue;   // single letters between dots: "s.w.a.t." -> "swat"
            }
            if (!isWordChar(c)) {
                space = true;
                continue;
            }
            if (space && !out.empty()) {
                out += ' ';
            }
            space = false;
            if (c < 0x80) {
                out += (char) c;
            } else {
                out += utf8::encode(c);
            }
        }
        return out;
    }

    std::vector<std::string> tokens(const std::string &normalized) {
        std::vector<std::string> out;
        for (std::string_view w: words(normalized)) {
            out.emplace_back(w);
        }
        return out;
    }

    int editDistance(std::string_view a, std::string_view b, int cutoff, bool prefix) {
        const size_t MAX = 63;
        if (a.size() > MAX || b.size() > MAX) {
            return a == b ? 0 : cutoff + 1;
        }
        if (!prefix && (a.size() > b.size() + (size_t) cutoff || b.size() > a.size() + (size_t) cutoff)) {
            return cutoff + 1;
        }
        // rows over b; d[i][j] = distance(a[0..i), b[0..j))
        int prev2[MAX + 1], prev[MAX + 1], cur[MAX + 1];
        size_t m = b.size();
        for (size_t j = 0; j <= m; j++) {
            prev[j] = (int) j;
        }
        int best;
        for (size_t i = 1; i <= a.size(); i++) {
            cur[0] = (int) i;
            int rowMin = cur[0];
            for (size_t j = 1; j <= m; j++) {
                int cost = a[i - 1] == b[j - 1] ? 0 : 1;
                int v = std::min(std::min(prev[j] + 1, cur[j - 1] + 1), prev[j - 1] + cost);
                if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                    v = std::min(v, prev2[j - 2] + 1);   // transposition
                }
                cur[j] = v;
                rowMin = std::min(rowMin, v);
            }
            if (rowMin > cutoff) {
                return cutoff + 1;
            }
            std::memcpy(prev2, prev, sizeof(int) * (m + 1));
            std::memcpy(prev, cur, sizeof(int) * (m + 1));
        }
        if (prefix) {
            // best prefix of b: any column of the last row
            best = prev[0];
            for (size_t j = 1; j <= m; j++) {
                best = std::min(best, prev[j]);
            }
        } else {
            best = prev[m];
        }
        return best > cutoff ? cutoff + 1 : best;
    }

    void Index::clear() {
        blob.clear();
        blob.shrink_to_fit();
        keys.clear();
        items.clear();
        dict.clear();
        postStart.clear();
        postItems.clear();
        gramKeys.clear();
        gramStart.clear();
        gramWords.clear();
    }

    void Index::build(const std::vector<std::string> &texts, const std::vector<std::string> &aliases) {
        clear();
        size_t n = texts.size();
        std::vector<std::string> key(n), alias(n);
        size_t total = 0;
        for (size_t i = 0; i < n; i++) {
            key[i] = normalize(texts[i]);
            if (i < aliases.size() && !aliases[i].empty()) {
                alias[i] = normalize(aliases[i]);
                if (alias[i] == key[i]) {
                    alias[i].clear();
                }
            }
            total += 2 * (key[i].size() + alias[i].size());
        }
        // one allocation for every string, so the views stay valid
        struct Span {
            uint32_t at;
            uint32_t len;
        };
        std::vector<Span> spans(n * 4);
        blob.reserve(total);
        auto put = [this](const std::string &s, bool compact) {
            Span sp{(uint32_t) blob.size(), 0};
            for (char c: s) {
                if (!compact || c != ' ') {
                    blob += c;
                }
            }
            sp.len = (uint32_t) blob.size() - sp.at;
            return sp;
        };
        for (size_t i = 0; i < n; i++) {
            spans[i * 4] = put(key[i], false);
            spans[i * 4 + 1] = put(alias[i], false);
            spans[i * 4 + 2] = put(key[i], true);
            spans[i * 4 + 3] = put(alias[i], true);
        }
        auto view = [this](const Span &s) { return std::string_view(blob.data() + s.at, s.len); };
        items.resize(n);
        keys.resize(n);
        std::vector<std::pair<std::string_view, int32_t>> pairs;
        pairs.reserve(n * 5);
        for (size_t i = 0; i < n; i++) {
            ItemKeys &k = items[i];
            k.key = view(spans[i * 4]);
            k.alias = view(spans[i * 4 + 1]);
            k.compact = view(spans[i * 4 + 2]);
            k.compactAlias = view(spans[i * 4 + 3]);
            keys[i] = k.key;
            for (std::string_view w: words(k.key)) {
                pairs.emplace_back(w, (int32_t) i);
            }
            for (std::string_view w: words(k.alias)) {
                pairs.emplace_back(w, (int32_t) i);
            }
        }
        std::sort(pairs.begin(), pairs.end());
        pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
        postItems.reserve(pairs.size());
        for (size_t i = 0; i < pairs.size(); i++) {
            if (i == 0 || pairs[i].first != pairs[i - 1].first) {
                dict.push_back(pairs[i].first);
                postStart.push_back((uint32_t) postItems.size());
            }
            postItems.push_back(pairs[i].second);
        }
        postStart.push_back((uint32_t) postItems.size());
        pairs.clear();
        pairs.shrink_to_fit();

        // trigram -> dictionary words (fuzzy candidates)
        std::vector<std::pair<uint32_t, uint32_t>> grams;
        std::vector<uint32_t> g;
        for (uint32_t w = 0; w < (uint32_t) dict.size(); w++) {
            if (dict[w].size() < 3) {
                continue;   // too short to be corrected
            }
            gramsOf(dict[w], true, g);
            for (uint32_t x: g) {
                grams.emplace_back(x, w);
            }
        }
        std::sort(grams.begin(), grams.end());
        gramWords.reserve(grams.size());
        for (size_t i = 0; i < grams.size(); i++) {
            if (i == 0 || grams[i].first != grams[i - 1].first) {
                gramKeys.push_back(grams[i].first);
                gramStart.push_back((uint32_t) gramWords.size());
            }
            gramWords.push_back(grams[i].second);
        }
        gramStart.push_back((uint32_t) gramWords.size());
    }

    size_t Index::memoryBytes() const {
        return blob.capacity() + keys.capacity() * sizeof(std::string_view) + items.capacity() * sizeof(ItemKeys)
               + dict.capacity() * sizeof(std::string_view) + postStart.capacity() * sizeof(uint32_t)
               + postItems.capacity() * sizeof(int32_t) + gramKeys.capacity() * sizeof(uint32_t)
               + gramStart.capacity() * sizeof(uint32_t) + gramWords.capacity() * sizeof(uint32_t);
    }

    void Index::wordRange(std::string_view t, bool exact, uint32_t &first, uint32_t &last) const {
        auto lo = std::lower_bound(dict.begin(), dict.end(), t);
        first = (uint32_t) (lo - dict.begin());
        if (exact) {
            last = first + (lo != dict.end() && *lo == t ? 1 : 0);
            return;
        }
        auto hi = std::partition_point(lo, dict.end(), [t](std::string_view w) { return startsWith(w, t); });
        last = (uint32_t) (hi - dict.begin());
    }

    int Index::scoreOf(int item, const std::string &q, const std::vector<std::string> &qt, const std::string &qc) const {
        const ItemKeys &k = items[(size_t) item];
        int best = 0;
        for (int which = 0; which < 2; which++) {
            std::string_view s = which == 0 ? k.key : k.alias;
            std::string_view compact = which == 0 ? k.compact : k.compactAlias;
            if (s.empty()) {
                continue;
            }
            if (s == q) {
                return EXACT;
            }
            if (startsWith(s, q)) {
                best = std::max(best, s[q.size()] == ' ' ? (int) STARTS_WORD : (int) STARTS);
                continue;
            }
            if (qt.size() > 1 && hasPhrase(s, q)) {
                best = std::max(best, (int) PHRASE);
                continue;
            }
            std::vector<std::string_view> w = words(s);
            bool whole = true;
            bool prefix = true;
            bool ordered = true;
            int lastAt = -1;
            for (const std::string &t: qt) {
                int at = -1;
                int prefixAt = -1;
                for (int i = 0; i < (int) w.size(); i++) {
                    if (w[(size_t) i] == t && at < 0) {
                        at = i;
                    }
                    if (prefixAt < 0 && startsWith(w[(size_t) i], t)) {
                        prefixAt = i;
                    }
                }
                whole = whole && at >= 0;
                prefix = prefix && prefixAt >= 0;
                int pos = at >= 0 ? at : prefixAt;
                ordered = ordered && pos > lastAt;
                lastAt = pos;
            }
            int score = whole ? (int) ALL_WORDS : prefix ? (int) ALL_PREFIXES : 0;
            if (score > 0 && ordered) {
                score += IN_ORDER;
            }
            if (qc.size() >= 3) {
                size_t at = compact.find(qc);
                if (at != std::string_view::npos) {
                    score = std::max(score, at == 0 ? (int) SUBSTRING_WORD : (int) SUBSTRING);
                }
            }
            best = std::max(best, score);
        }
        return best;
    }

    Result Index::find(const std::string &query, size_t limit) const {
        Result r;
        std::string q = normalize(query);
        int n = size();
        if (q.empty() || n == 0) {
            return r;
        }
        std::vector<std::string> qt = tokens(q);
        if (qt.size() > 16) {
            qt.resize(16);   // counts below are 8-bit; nobody types more words than this
        }
        std::string qc;
        for (char c: q) {
            if (c != ' ') {
                qc += c;
            }
        }
        const size_t K = qt.size();
        std::vector<int16_t> best((size_t) n, 0);
        std::vector<int32_t> found;

        // 1. every query word as a dictionary word or word prefix (the last word is being typed; a single
        //    letter elsewhere must be a whole word: "how i met")
        std::vector<uint8_t> count((size_t) n, 0);
        std::vector<int32_t> candidates;
        for (size_t k = 0; k < K; k++) {
            uint32_t a, b;
            wordRange(qt[k], qt[k].size() == 1 && k + 1 < K, a, b);
            int advanced = 0;
            for (uint32_t w = a; w < b; w++) {
                for (uint32_t p = postStart[w]; p < postStart[w + 1]; p++) {
                    int32_t item = postItems[p];
                    if ((size_t) count[(size_t) item] == k) {
                        count[(size_t) item] = (uint8_t) (k + 1);
                        advanced++;
                        if (k == 0) {
                            candidates.push_back(item);
                        }
                    }
                }
            }
            if (advanced == 0) {
                break;
            }
        }
        for (int32_t item: candidates) {
            if ((size_t) count[(size_t) item] == K) {
                int s = scoreOf(item, q, qt, qc);
                best[(size_t) item] = (int16_t) (s > 0 ? s : (int) ALL_PREFIXES);   // words split over title and alias
                found.push_back(item);
            }
        }

        // 2. substring of the compact key ("spiderman" in "spider man", "metyourmother")
        if (qc.size() >= 3) {
            for (int i = 0; i < n; i++) {
                if (best[(size_t) i] >= SUBSTRING) {
                    continue;
                }
                const ItemKeys &k = items[(size_t) i];
                size_t at = k.compact.find(qc);
                if (at == std::string_view::npos && !k.compactAlias.empty()) {
                    at = k.compactAlias.find(qc);
                }
                if (at != std::string_view::npos) {
                    if (best[(size_t) i] == 0) {
                        found.push_back(i);
                    }
                    best[(size_t) i] = (int16_t) (at == 0 ? SUBSTRING_WORD : SUBSTRING);
                }
            }
        }

        // 3. typos, only when little was found: bounded candidate words per query word, then all words again
        bool longWord = false;
        for (const auto &t: qt) {
            longWord = longWord || maxEdits(t.size()) > 0;
        }
        if ((int) found.size() < FUZZY_WHEN_FEWER_THAN && longWord && K <= 8) {
            std::vector<std::vector<std::pair<uint32_t, int>>> alternatives(K);
            std::vector<uint16_t> shared;
            std::vector<uint32_t> touched;
            std::vector<uint32_t> g;
            for (size_t k = 0; k < K; k++) {
                const std::string &t = qt[k];
                bool typing = k + 1 == K;
                uint32_t a, b;
                wordRange(t, t.size() == 1 && !typing, a, b);
                for (uint32_t w = a; w < b; w++) {
                    alternatives[k].emplace_back(w, 0);
                }
                int maxE = maxEdits(t.size());
                if (maxE == 0) {
                    continue;
                }
                if (shared.empty()) {
                    shared.assign(dict.size(), 0);
                }
                gramsOf(t, !typing, g);
                for (uint32_t x: g) {
                    auto it = std::lower_bound(gramKeys.begin(), gramKeys.end(), x);
                    if (it == gramKeys.end() || *it != x) {
                        continue;
                    }
                    size_t gi = (size_t) (it - gramKeys.begin());
                    for (uint32_t p = gramStart[gi]; p < gramStart[gi + 1]; p++) {
                        uint32_t w = gramWords[p];
                        if (shared[w]++ == 0) {
                            touched.push_back(w);
                        }
                    }
                }
                // an edit changes at most 3 trigrams
                int need = std::max(1, (int) g.size() - 3 * maxE);
                std::vector<std::pair<int, uint32_t>> ranked;
                for (uint32_t w: touched) {
                    if (shared[w] >= need && !(w >= a && w < b)) {
                        ranked.emplace_back(-(int) shared[w], w);
                    }
                    shared[w] = 0;
                }
                touched.clear();
                std::sort(ranked.begin(), ranked.end());
                if (ranked.size() > 300) {
                    ranked.resize(300);   // bounded work per word
                }
                std::vector<uint32_t> pool;
                for (const auto &c: ranked) {
                    pool.push_back(c.second);
                    shared[c.second] = 1;   // reused as "already a candidate"
                }
                // a swap near the start of a short word leaves no common trigram ("dnue" / "dune"): words with
                // the same first letter and a compatible length are candidates too (bounded)
                uint32_t fa, fb;
                wordRange(std::string_view(t).substr(0, 1), false, fa, fb);
                if (fb - fa <= 6000) {
                    for (uint32_t w = fa; w < fb; w++) {
                        size_t len = dict[w].size();
                        bool lengthOk = typing ? len + (size_t) maxE >= t.size()
                                               : len + (size_t) maxE >= t.size() && len <= t.size() + (size_t) maxE;
                        if (lengthOk && shared[w] == 0 && !(w >= a && w < b)) {
                            pool.push_back(w);
                        }
                    }
                }
                for (uint32_t w: pool) {
                    shared[w] = 0;
                    int d = editDistance(t, dict[w], maxE, typing);
                    if (d <= maxE) {
                        alternatives[k].emplace_back(w, d);
                    }
                }
                std::stable_sort(alternatives[k].begin(), alternatives[k].end(),
                                 [](const std::pair<uint32_t, int> &x, const std::pair<uint32_t, int> &y) {
                                     return x.second < y.second;
                                 });
            }
            std::fill(count.begin(), count.end(), (uint8_t) 0);
            std::vector<uint8_t> edits((size_t) n, 0);
            std::vector<int32_t> fuzzyCandidates;
            for (size_t k = 0; k < K; k++) {
                for (const auto &alt: alternatives[k]) {
                    for (uint32_t p = postStart[alt.first]; p < postStart[alt.first + 1]; p++) {
                        int32_t item = postItems[p];
                        if ((size_t) count[(size_t) item] == k) {
                            count[(size_t) item] = (uint8_t) (k + 1);
                            edits[(size_t) item] = (uint8_t) (edits[(size_t) item] + alt.second);
                            if (k == 0) {
                                fuzzyCandidates.push_back(item);
                            }
                        }
                    }
                }
            }
            for (int32_t item: fuzzyCandidates) {
                if ((size_t) count[(size_t) item] == K && edits[(size_t) item] > 0 && best[(size_t) item] == 0) {
                    best[(size_t) item] = (int16_t) std::max((int) FUZZY_MIN,
                                                             (int) FUZZY - (int) FUZZY_PER_EDIT * edits[(size_t) item]);
                    found.push_back(item);
                }
            }
        }

        // rank: score, then the title closest in length to the query, then provider order
        auto closeness = [this, &q](int item) {
            const ItemKeys &k = items[(size_t) item];
            size_t d = k.key.size() > q.size() ? k.key.size() - q.size() : q.size() - k.key.size();
            if (!k.alias.empty()) {
                size_t da = k.alias.size() > q.size() ? k.alias.size() - q.size() : q.size() - k.alias.size();
                d = std::min(d, da);
            }
            return d;
        };
        r.total = (int) found.size();
        std::vector<std::pair<std::pair<int, size_t>, int32_t>> order;
        order.reserve(found.size());
        for (int32_t item: found) {
            order.push_back({{-(int) best[(size_t) item], closeness(item)}, item});
        }
        size_t keep = std::min(limit, order.size());
        std::partial_sort(order.begin(), order.begin() + (std::ptrdiff_t) keep, order.end());
        r.hits.reserve(keep);
        for (size_t i = 0; i < keep; i++) {
            r.hits.push_back({order[i].second, -order[i].first.first});
        }
        return r;
    }
}
