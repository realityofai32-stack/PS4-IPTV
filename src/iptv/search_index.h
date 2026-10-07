// Prebuilt title search index (host-testable).
//
// Built once per catalog (on the worker thread that parses it, and again after a refresh), never per
// keypress. A query touches only:
//   1. the token dictionary: every query word as a whole word or word prefix ("handmaid" -> "handmaids"),
//      all words required (AND), through sorted posting lists
//   2. a linear scan of the pre-normalized compact keys for a substring of at least 3 characters
//      ("spiderman" in "Spider-Man", "met your mother" anywhere in a title)
//   3. only when that finds little: a bounded fuzzy pass - dictionary words sharing trigrams with a query
//      word are compared with a cut-off Damerau-Levenshtein distance ("braking" -> "breaking")
// Results are ranked: exact title, title starts with the query, phrase / all words, all word prefixes,
// substring, fuzzy; ties go to the title closest in length to the query, then provider order.

#ifndef PS4IPTV_IPTV_SEARCH_INDEX_H
#define PS4IPTV_IPTV_SEARCH_INDEX_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace search {

    // Search key of a display text: lower case; Turkish and other Latin letters folded to ASCII
    // (ş->s ğ->g ı/İ->i ö->o ü->u ç->c é->e ...); apostrophes removed ("Handmaid's" -> "handmaids");
    // dotted acronyms joined ("S.W.A.T." -> "swat"); other punctuation, symbols and runs of white space
    // become one space. Only used for matching: displayed metadata is never changed.
    std::string normalize(const std::string &text);

    // words of a normalized key
    std::vector<std::string> tokens(const std::string &normalized);

    // Damerau-Levenshtein (optimal string alignment) distance, or cutoff + 1 when it is larger.
    // prefix: distance from `a` to the closest prefix of `b` (for the word being typed).
    int editDistance(std::string_view a, std::string_view b, int cutoff, bool prefix = false);

    // ranking scores (higher first)
    enum Score : int {
        EXACT = 1000,           // the whole title (or its alias) is the query
        STARTS_WORD = 900,      // title starts with the query, at a word boundary
        STARTS = 850,           // title starts with the query inside a word
        PHRASE = 780,           // the query is a run of whole words inside the title
        ALL_WORDS = 700,        // every query word is a word of the title (+ IN_ORDER)
        ALL_PREFIXES = 600,     // every query word starts a word of the title (+ IN_ORDER)
        IN_ORDER = 20,
        SUBSTRING_WORD = 450,   // the query (spaces ignored) starts inside the title at a word
        SUBSTRING = 400,        // the query (spaces ignored) appears anywhere in the title
        FUZZY = 350,            // every word matches, some with typos: FUZZY - FUZZY_PER_EDIT x edits
        FUZZY_PER_EDIT = 50,
        FUZZY_MIN = 200
    };

    struct Hit {
        int item;
        int score;
    };

    struct Result {
        std::vector<Hit> hits;   // best first, at most `limit`
        int total = 0;           // matches before the limit
    };

    class Index {

    public:

        // texts[i]: what item i is found by. aliases (optional, same size or empty): a second text for the
        // same item, e.g. a movie's title without its trailing year ("Primate" for "Primate 2026").
        void build(const std::vector<std::string> &texts, const std::vector<std::string> &aliases = {});

        Result find(const std::string &query, size_t limit) const;

        void clear();

        int size() const { return (int) keys.size(); }

        // heap memory of the index (approximate, for diagnostics)
        size_t memoryBytes() const;

        // the normalized key of item i (sorting by title reuses it)
        std::string_view key(int item) const { return keys[(size_t) item]; }

        // fuzzy pass threshold: runs when fewer exact/prefix/substring matches than this were found
        static const int FUZZY_WHEN_FEWER_THAN = 12;

    private:

        struct ItemKeys {
            std::string_view key;      // normalized text
            std::string_view alias;    // normalized alias ("" = none)
            std::string_view compact;  // key without spaces
            std::string_view compactAlias;
        };

        // index range [first, last) of dictionary words equal to (exact) or starting with `t`
        void wordRange(std::string_view t, bool exact, uint32_t &first, uint32_t &last) const;

        int scoreOf(int item, const std::string &q, const std::vector<std::string> &qt, const std::string &qc) const;

        std::string blob;                      // all normalized strings; the views below point into it
        std::vector<std::string_view> keys;    // per item
        std::vector<ItemKeys> items;
        std::vector<std::string_view> dict;    // sorted unique words
        std::vector<uint32_t> postStart;       // CSR: items of dict[w] = postItems[postStart[w] .. postStart[w + 1])
        std::vector<int32_t> postItems;
        std::vector<uint32_t> gramKeys;        // sorted unique trigrams of the dictionary words
        std::vector<uint32_t> gramStart;       // CSR: words of gramKeys[g]
        std::vector<uint32_t> gramWords;
    };
}

#endif // PS4IPTV_IPTV_SEARCH_INDEX_H
