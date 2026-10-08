// Localization checks (run in every host test build):
//   - English and Türkçe have exactly the same keys, no duplicates, no blank text, the same {n} placeholders,
//     valid UTF-8; plural keys come in .one / .other pairs
//   - every key the source code refers to exists (string literals of the key namespaces, i18n::count keys and
//     the few keys built at run time)
//   - screens do not bypass the tables: string literals in src/screens/*.cpp, app.cpp and widgets.cpp must be
//     localization keys, technical tokens, or sit in a block marked i18n-exempt (fixed specimen text, proper
//     names). PS4IPTV_SOURCE_DIR points at src/ (scripts/run-host-tests.ps1).

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include "check.h"
#include "../../src/i18n/i18n.h"
#include "../../src/player/display.h"

using i18n::Language;

namespace {

    // strict UTF-8 (no overlong forms, no surrogates, at most U+10FFFF)
    bool validUtf8(const std::string &s) {
        size_t i = 0;
        while (i < s.size()) {
            unsigned char c = (unsigned char) s[i];
            int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
            if (n < 0) {
                return false;
            }
            unsigned cp = n == 0 ? c : n == 1 ? (c & 0x1F) : n == 2 ? (c & 0x0F) : (c & 0x07);
            for (int k = 1; k <= n; k++) {
                if (i + (size_t) k >= s.size() || ((unsigned char) s[i + (size_t) k] >> 6) != 2) {
                    return false;
                }
                cp = (cp << 6) | ((unsigned char) s[i + (size_t) k] & 0x3F);
            }
            if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || cp > 0x10FFFF
                || (cp >= 0xD800 && cp <= 0xDFFF)) {
                return false;
            }
            i += (size_t) n + 1;
        }
        return true;
    }

    std::set<std::string> placeholders(const std::string &text) {
        std::set<std::string> out;
        for (size_t i = 0; i + 2 < text.size(); i++) {
            if (text[i] == '{' && text[i + 1] >= '0' && text[i + 1] <= '9' && text[i + 2] == '}') {
                out.insert(text.substr(i, 3));
            }
        }
        return out;
    }

    std::map<std::string, std::string> asMap(Language l, int &duplicates) {
        std::map<std::string, std::string> m;
        duplicates = 0;
        for (const auto &e: i18n::table(l)) {
            if (!m.emplace(e.key, e.text).second) {
                std::printf("  duplicate key %s in %s\n", e.key, i18n::code(l));
                duplicates++;
            }
        }
        return m;
    }

    std::string sourceDir() {
        const char *d = std::getenv("PS4IPTV_SOURCE_DIR");
        return d ? d : "";
    }

    std::string readAll(const std::filesystem::path &p) {
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    // string literals of one line ("" when inside a comment); escapes are kept as written
    std::vector<std::string> literals(const std::string &line) {
        std::vector<std::string> out;
        bool in = false;
        std::string cur;
        for (size_t i = 0; i < line.size(); i++) {
            char c = line[i];
            if (!in) {
                if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
                    break;   // comment
                }
                if (c == '\'' && i + 2 < line.size()) {
                    i += line[i + 1] == '\\' ? 3 : 2;   // a char literal ('"', '\'')
                    continue;
                }
                if (c == '"') {
                    in = true;
                    cur.clear();
                }
            } else if (c == '\\' && i + 1 < line.size()) {
                cur += c;
                cur += line[++i];
            } else if (c == '"') {
                in = false;
                out.push_back(cur);
            } else {
                cur += c;
            }
        }
        return out;
    }

    const std::regex KEY_SHAPE("^[a-z][a-z0-9_]*(\\.[a-z0-9_]+)+$");

    // the first segments of every table key ("home", "download" ...)
    std::set<std::string> namespaces() {
        std::set<std::string> out;
        for (const auto &e: i18n::table(Language::English)) {
            std::string k = e.key;
            out.insert(k.substr(0, k.find('.')));
        }
        return out;
    }
}

TEST(i18n_tables_complete_and_consistent) {
    int dupEn = 0, dupTr = 0;
    auto en = asMap(Language::English, dupEn);
    auto tr = asMap(Language::Turkish, dupTr);
    CHECK_EQ(dupEn, 0);
    CHECK_EQ(dupTr, 0);
    CHECK(en.size() > 400);
    int missing = 0, extra = 0, blank = 0, badPlaceholders = 0, badUtf8 = 0;
    for (const auto &e: en) {
        auto it = tr.find(e.first);
        if (it == tr.end()) {
            std::printf("  missing in Turkish: %s\n", e.first.c_str());
            missing++;
            continue;
        }
        if (placeholders(e.second) != placeholders(it->second)) {
            std::printf("  placeholders differ: %s\n", e.first.c_str());
            badPlaceholders++;
        }
    }
    for (const auto &t: tr) {
        if (en.find(t.first) == en.end()) {
            std::printf("  only in Turkish: %s\n", t.first.c_str());
            extra++;
        }
    }
    for (const auto *m: {&en, &tr}) {
        for (const auto &e: *m) {
            if (e.second.find_first_not_of(" \t\n") == std::string::npos) {
                std::printf("  blank text: %s\n", e.first.c_str());
                blank++;
            }
            if (!validUtf8(e.second)) {
                std::printf("  invalid UTF-8: %s\n", e.first.c_str());
                badUtf8++;
            }
        }
    }
    CHECK_EQ(missing, 0);
    CHECK_EQ(extra, 0);
    CHECK_EQ(blank, 0);
    CHECK_EQ(badPlaceholders, 0);
    CHECK_EQ(badUtf8, 0);
    // plural forms come in pairs
    int unpaired = 0;
    for (const auto &e: en) {
        const std::string &k = e.first;
        auto ends = [&k](const char *s) {
            std::string suf = s;
            return k.size() > suf.size() && k.compare(k.size() - suf.size(), suf.size(), suf) == 0;
        };
        if ((ends(".one") && !en.count(k.substr(0, k.size() - 4) + ".other"))
            || (ends(".other") && !en.count(k.substr(0, k.size() - 6) + ".one"))) {
            std::printf("  plural form without its pair: %s\n", k.c_str());
            unpaired++;
        }
    }
    CHECK_EQ(unpaired, 0);
}

TEST(i18n_turkish_uses_turkish_letters) {
    // a sample that must be written with the real letters (no ASCII fallbacks such as "Iptal" or "Sifre")
    CHECK_EQ(i18n::lookup(Language::Turkish, "common.cancel"), std::string("\xC4\xB0ptal"));
    CHECK_EQ(i18n::lookup(Language::Turkish, "profile.password"), std::string("\xC5\x9E" "ifre"));
    CHECK_EQ(i18n::lookup(Language::Turkish, "home.live_tv"), std::string("Canl\xC4\xB1 TV"));
    CHECK_EQ(i18n::lookup(Language::Turkish, "common.exit"), std::string("\xC3\x87\xC4\xB1k\xC4\xB1\xC5\x9F"));
    CHECK_EQ(i18n::lookup(Language::Turkish, "home.downloads"), std::string("\xC4\xB0ndirmeler"));
    // the Turkish letters beyond ASCII are used (capital Ğ never starts a Turkish word, so it does not occur)
    std::string all;
    for (const auto &e: i18n::table(Language::Turkish)) {
        all += e.text;
    }
    for (const char *letter: {"\xC3\x87", "\xC3\xA7", "\xC4\x9F", "\xC4\xB0", "\xC4\xB1", "\xC3\x96", "\xC3\xB6",
                              "\xC5\x9E", "\xC5\x9F", "\xC3\x9C", "\xC3\xBC"}) {
        if (all.find(letter) == std::string::npos) {
            std::printf("  Turkish letter %s never used\n", letter);
        }
        CHECK(all.find(letter) != std::string::npos);
    }
    // and no ASCII stand-ins for words that need them
    for (const char *ascii: {"Iptal", "Sifre", "Cikis", "Turkce", "Indir"}) {
        CHECK(all.find(ascii) == std::string::npos);
    }
}

TEST(i18n_runtime_behaviour) {
    i18n::setLanguage(Language::English);
    CHECK_EQ(i18n::tr("home.downloads"), std::string("Downloads"));
    CHECK_EQ(i18n::tr("download.size_of", {"4.2 GB", "7.8 GB"}), std::string("4.2 GB / 7.8 GB"));
    CHECK_EQ(i18n::count("home.channels", 1), std::string("1 channel"));
    CHECK_EQ(i18n::count("home.channels", 573), std::string("573 channels"));
    CHECK_EQ(i18n::substitute("{1}-{0}-{9}", {"a", "b"}), std::string("b-a-"));
    i18n::setLanguage(Language::Turkish);
    CHECK_EQ(i18n::tr("home.downloads"), std::string("\xC4\xB0ndirmeler"));
    CHECK_EQ(i18n::count("home.channels", 573), std::string("573 kanal"));
    CHECK_EQ(i18n::tr("duration.left", {"42 dk"}), std::string("42 dk kald\xC4\xB1"));
    i18n::setLanguage(Language::English);
    // unknown keys never crash and are visible as themselves (the source scan below keeps them out of releases)
    CHECK_EQ(i18n::tr("no.such_key"), std::string("no.such_key"));
    // fresh installs / unknown codes are English
    CHECK(i18n::fromCode("") == Language::English);
    CHECK(i18n::fromCode("de") == Language::English);
    CHECK(i18n::fromCode("tr") == Language::Turkish);
    CHECK_EQ(std::string(i18n::nativeName(Language::Turkish)), std::string("T\xC3\xBCrk\xC3\xA7" "e"));
}

TEST(i18n_every_used_key_exists) {
    std::string src = sourceDir();
    if (src.empty()) {
        std::printf("  (skipped: PS4IPTV_SOURCE_DIR not set)\n");
        return;
    }
    std::set<std::string> ns = namespaces();
    int dupEn = 0;
    auto en = asMap(Language::English, dupEn);
    int missing = 0, files = 0, refs = 0;
    const std::regex countCall("count\\(\"([a-z0-9_.]+)\"");
    for (const auto &entry: std::filesystem::recursive_directory_iterator(src)) {
        std::string name = entry.path().filename().string();
        if (!entry.is_regular_file() || name.rfind("strings_", 0) == 0
            || (entry.path().extension() != ".cpp" && entry.path().extension() != ".h")) {
            continue;
        }
        files++;
        std::string text = readAll(entry.path());
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            for (const std::string &lit: literals(line)) {
                if (!std::regex_match(lit, KEY_SHAPE) || !ns.count(lit.substr(0, lit.find('.')))) {
                    continue;
                }
                // file names ("settings.json") are not keys
                std::string ext = lit.substr(lit.rfind('.') + 1);
                if (ext == "json" || ext == "h" || ext == "cpp" || ext == "txt" || ext == "ttf" || ext == "pem") {
                    continue;
                }
                refs++;
                bool isCount = std::regex_search(line, std::regex("count\\(\"" + std::regex_replace(lit, std::regex("\\."), "\\.") + "\""));
                if (isCount) {
                    if (!en.count(lit + ".one") || !en.count(lit + ".other")) {
                        std::printf("  %s: plural key %s(.one/.other) missing\n", name.c_str(), lit.c_str());
                        missing++;
                    }
                } else if (!en.count(lit)) {
                    // a prefix of keys built at run time ("search.movies" -> "search.movies.count") is fine
                    bool prefix = false;
                    for (const auto &e: en) {
                        prefix |= e.first.compare(0, lit.size() + 1, lit + ".") == 0;
                    }
                    if (!prefix) {
                        std::printf("  %s: key %s missing\n", name.c_str(), lit.c_str());
                        missing++;
                    }
                }
            }
        }
    }
    // keys built at run time
    for (const char *prefix: {"search.movies", "search.series"}) {
        for (const char *suffix: {".count", ".unavailable", ".none", ".loading"}) {
            CHECK(en.count(std::string(prefix) + suffix) == 1);
        }
    }
    for (const char *k: {"geometry.left", "geometry.right", "geometry.up", "geometry.down"}) {
        CHECK(en.count(k) == 1);
    }
    for (int m = 1; m <= 12; m++) {
        CHECK(en.count("date.month." + std::to_string(m)) == 1);
    }
    CHECK(files > 50);
    CHECK(refs > 400);
    CHECK_EQ(missing, 0);
}

TEST(i18n_screens_do_not_hardcode_text) {
    std::string src = sourceDir();
    if (src.empty()) {
        std::printf("  (skipped: PS4IPTV_SOURCE_DIR not set)\n");
        return;
    }
    // technical tokens that are the same in every language
    const std::set<std::string> allowed = {
            "MPEG-TS", "HLS", "ts", "m3u8", "mkv", "VOD", "%.2f fps", "%d Hz, ", "%.1f s", "%lld KB/s", "%.1f",
            "(HLS)", "  (HLS)", "http://example.com:8080", "PS4IPTV", "h264", "H.264", "hevc", "HEVC",
            ".count", ".unavailable", ".none", ".loading",
            // controller button names printed on the button glyphs (as on the DualShock 4 itself)
            "D-PAD", "OPTIONS",
            // file paths and log-only reasons
            "cache/images", "app exit", "pause", "stop", "episode change",
            "assets/fonts/", "assets/fonts/Inter-SemiBold.ttf", "assets/cacert.pem", "PS4IPTV/", "downloads/", "mpv",
            // settings / mpv values: language codes, option names and choices
            "tr", "en", "off", "default", "hr-seek", "clear-metadata", "force_redraw"};
    // "\xC2\xB7" escapes are symbols, not words: drop escapes before looking for letters
    const std::regex escapes("\\\\(x[0-9A-Fa-f]{2}|.)");
    std::vector<std::filesystem::path> files;
    for (const auto &entry: std::filesystem::directory_iterator(std::filesystem::path(src) / "screens")) {
        if (entry.path().extension() == ".cpp") {
            files.push_back(entry.path());
        }
    }
    files.push_back(std::filesystem::path(src) / "app" / "app.cpp");
    files.push_back(std::filesystem::path(src) / "ui" / "widgets.cpp");
    const std::regex letters("[A-Za-z]{2,}");
    int hits = 0;
    for (const auto &path: files) {
        std::string text = readAll(path);
        if (text.find("i18n-exempt-file") != std::string::npos) {
            continue;
        }
        std::istringstream lines(text);
        std::string line;
        bool exempt = false;
        bool inLog = false;   // a log statement (English on purpose) can span several lines
        int n = 0;
        while (std::getline(lines, line)) {
            n++;
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();   // CRLF working copies: the end-of-statement check below needs the bare line
            }
            if (line.find("i18n-exempt-begin") != std::string::npos) {
                exempt = true;
            }
            if (line.find("i18n-exempt-end") != std::string::npos) {
                exempt = false;
                continue;
            }
            std::string trimmed = line.substr(std::min(line.size(), line.find_first_not_of(" \t")));
            if (line.find("LOG_") != std::string::npos) {
                inLog = true;
            }
            if (inLog) {
                std::string end = line.substr(0, line.find_last_not_of(" \t") + 1);
                if (end.size() >= 2 && end.compare(end.size() - 2, 2, ");") == 0) {
                    inLog = false;
                }
                continue;
            }
            if (exempt || trimmed.rfind("//", 0) == 0 || trimmed.rfind("#include", 0) == 0
                || line.find("name() const") != std::string::npos || line.find("diag::write") != std::string::npos
                || line.find("i18n-exempt") != std::string::npos) {
                continue;
            }
            for (const std::string &lit: literals(line)) {
                std::string words = std::regex_replace(lit, escapes, " ");
                if (!std::regex_search(words, letters) || std::regex_match(lit, KEY_SHAPE) || allowed.count(lit)) {
                    continue;
                }
                std::printf("  %s:%d hardcoded UI text \"%s\"\n", path.filename().string().c_str(), n, lit.c_str());
                hits++;
            }
        }
    }
    CHECK_EQ(hits, 0);
}
