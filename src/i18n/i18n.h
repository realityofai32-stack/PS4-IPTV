// Application UI localization (host-testable).
//
// Every user-facing text of the app comes from a table of stable keys ("home.live_tv", "download.pause")
// per UI language: strings_en.cpp (English, the default and the fallback) and strings_tr.cpp (Türkçe).
// Provider metadata (titles, channel names, categories, plots) is never translated.
//
//   i18n::tr("player.buffering")                    -> "Buffering…" / "Arabelleğe alınıyor…"
//   i18n::tr("download.size_of", {done, total})     -> "{0} of {1}" with the arguments substituted
//   i18n::count("home.channels", 573)               -> key "home.channels.one" / ".other", {0} = the number
//
// The UI language is global to the app (Settings > Language), never chosen from the account, provider or
// region. tr() may be called from any thread: the tables are immutable and the language is atomic.
// tests/host/test_i18n.cpp checks that both tables have the same keys and placeholders, that no text is
// blank, that every key used in src/ exists, and that screens do not bypass the tables.

#ifndef PS4IPTV_I18N_I18N_H
#define PS4IPTV_I18N_I18N_H

#include <initializer_list>
#include <string>
#include <vector>

namespace i18n {

    enum class Language {
        English,
        Turkish
    };

    struct Entry {
        const char *key;
        const char *text;
    };

    // the string tables (strings_en.cpp / strings_tr.cpp), in source order
    const std::vector<Entry> &table(Language language);

    void setLanguage(Language language);

    Language language();

    // settings.json value: "en" / "tr"
    const char *code(Language language);

    // unknown or empty codes -> English (the default of every installation)
    Language fromCode(const std::string &code);

    // the language's own name for the Language setting: "English", "Türkçe"
    const char *nativeName(Language language);

    // text of a key in the current language; English when the language lacks it; the key itself when no
    // table has it (logged once - the host tests make sure this never happens in a release)
    const std::string &tr(const char *key);

    // with {0}, {1} ... replaced by the arguments
    std::string tr(const char *key, std::initializer_list<std::string> args);

    // key + ".one" when n == 1, else key + ".other"; {0} is the number
    std::string count(const char *key, long long n);

    // {0} {1} substitution (exposed for tests)
    std::string substitute(const std::string &text, std::initializer_list<std::string> args);

    // lookup in a specific table ("" when absent)
    std::string lookup(Language language, const std::string &key);
}

#endif // PS4IPTV_I18N_I18N_H
