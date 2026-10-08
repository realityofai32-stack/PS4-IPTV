#include <atomic>
#include <mutex>
#include <set>
#include <unordered_map>

#include "i18n.h"
#include "../platform/log.h"

namespace i18n {

    // defined in strings_en.cpp / strings_tr.cpp
    extern const Entry STRINGS_EN[];
    extern const size_t STRINGS_EN_COUNT;
    extern const Entry STRINGS_TR[];
    extern const size_t STRINGS_TR_COUNT;

    namespace {
        std::atomic<int> g_language{(int) Language::English};

        struct Table {
            std::vector<Entry> entries;
            std::unordered_map<std::string, std::string> map;
        };

        Table build(const Entry *e, size_t n) {
            Table t;
            t.entries.assign(e, e + n);
            t.map.reserve(n);
            for (size_t i = 0; i < n; i++) {
                t.map.emplace(e[i].key, e[i].text);
            }
            return t;
        }

        const Table &tableOf(Language l) {
            static const Table en = build(STRINGS_EN, STRINGS_EN_COUNT);
            static const Table tr = build(STRINGS_TR, STRINGS_TR_COUNT);
            return l == Language::Turkish ? tr : en;
        }

        // keys missing from every table: kept alive so tr() can return a reference
        std::mutex g_missingMutex;
        std::set<std::string> g_missing;
    }

    const std::vector<Entry> &table(Language l) {
        return tableOf(l).entries;
    }

    void setLanguage(Language l) {
        g_language.store((int) l);
    }

    Language language() {
        return (Language) g_language.load();
    }

    const char *code(Language l) {
        return l == Language::Turkish ? "tr" : "en";
    }

    Language fromCode(const std::string &c) {
        return c == "tr" ? Language::Turkish : Language::English;
    }

    const char *nativeName(Language l) {
        return l == Language::Turkish ? "T\xC3\xBCrk\xC3\xA7" "e" : "English";
    }

    const std::string &tr(const char *key) {
        const Table &t = tableOf(language());
        auto it = t.map.find(key);
        if (it != t.map.end()) {
            return it->second;
        }
        const Table &en = tableOf(Language::English);
        it = en.map.find(key);
        if (it != en.map.end()) {
            return it->second;
        }
        std::lock_guard<std::mutex> lock(g_missingMutex);
        auto ins = g_missing.insert(key);
        if (ins.second) {
            LOG_E("i18n", "missing localization key '%s'", key);
        }
        return *ins.first;
    }

    std::string substitute(const std::string &text, std::initializer_list<std::string> args) {
        std::string out;
        out.reserve(text.size() + 16);
        for (size_t i = 0; i < text.size(); i++) {
            if (text[i] == '{' && i + 2 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '9' && text[i + 2] == '}') {
                size_t n = (size_t) (text[i + 1] - '0');
                if (n < args.size()) {
                    out += *(args.begin() + n);
                }
                i += 2;
                continue;
            }
            out += text[i];
        }
        return out;
    }

    std::string tr(const char *key, std::initializer_list<std::string> args) {
        return substitute(tr(key), args);
    }

    std::string count(const char *key, long long n) {
        std::string k = std::string(key) + (n == 1 ? ".one" : ".other");
        return substitute(tr(k.c_str()), {std::to_string(n)});
    }

    std::string lookup(Language l, const std::string &key) {
        const Table &t = tableOf(l);
        auto it = t.map.find(key);
        return it == t.map.end() ? std::string() : it->second;
    }
}
