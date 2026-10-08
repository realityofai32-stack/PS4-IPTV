#include <cstring>

#include "tracks.h"
#include "../i18n/i18n.h"

namespace tracks {

    namespace {
        struct Language {
            const char *code;       // ISO 639-2/T
            const char *aliases;    // space separated, lowercase (639-1, 639-2/B, English name)
            const char *name;       // native name, shown in every UI language (the UI font has the script)
            const char *key;        // else: localization key of the name in the UI language ("Japanese")
        };

        // The UI font (Inter) covers Latin, Greek and Cyrillic: those languages show their native name. Scripts it
        // cannot draw (and Arabic / Hebrew, which would also need shaping and right-to-left layout) show the
        // language's name in the UI language instead of empty boxes.
        const Language LANGUAGES[] = {
                {"tur", "tr tur turkish t\xC3\xBCrk\xC3\xA7" "e", "T\xC3\xBCrk\xC3\xA7" "e", nullptr},
                {"eng", "en eng english \xC4\xB0ngilizce ingilizce", "English", nullptr},
                {"deu", "de deu ger german almanca deutsch", "Deutsch", nullptr},
                {"fra", "fr fra fre french frans\xC4\xB1zca français francais", "Fran\xC3\xA7" "ais", nullptr},
                {"spa", "es spa spanish \xC4\xB0spanyolca ispanyolca español", "Espa\xC3\xB1ol", nullptr},
                {"ita", "it ita italian \xC4\xB0talyanca italyanca italiano", "Italiano", nullptr},
                {"por", "pt por portuguese portekizce português", "Portugu\xC3\xAAs", nullptr},
                {"rus", "ru rus russian rus\xC3\xA7" "a", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9", nullptr},
                {"ara", "ar ara arabic arap\xC3\xA7" "a", nullptr, "lang.ara"},
                {"nld", "nl nld dut dutch hollandaca felemenkce nederlands", "Nederlands", nullptr},
                {"pol", "pl pol polish lehce polski", "Polski", nullptr},
                {"swe", "sv swe swedish", "Svenska", nullptr},
                {"nor", "no nor nob nb nno nn norwegian", "Norsk", nullptr},
                {"dan", "da dan danish", "Dansk", nullptr},
                {"fin", "fi fin finnish", "Suomi", nullptr},
                {"ell", "el ell gre greek yunanca", "\xCE\x95\xCE\xBB\xCE\xBB\xCE\xB7\xCE\xBD\xCE\xB9\xCE\xBA\xCE\xAC", nullptr},
                {"jpn", "ja jpn japanese japonca 日本語", nullptr, "lang.jpn"},
                {"kor", "ko kor korean korece 한국어", nullptr, "lang.kor"},
                {"zho", "zh zho chi cmn yue chinese cince 中文", nullptr, "lang.zho"},
                {"hin", "hi hin hindi hintce", nullptr, "lang.hin"},
                {"hun", "hu hun hungarian", "Magyar", nullptr},
                {"ces", "cs ces cze czech", "\xC4\x8C" "e\xC5\xA1tina", nullptr},
                {"slk", "sk slk slo slovak", "Sloven\xC4\x8Dina", nullptr},
                {"ron", "ro ron rum mol romanian", "Rom\xC3\xA2n\xC4\x83", nullptr},
                {"bul", "bg bul bulgarian", "\xD0\x91\xD1\x8A\xD0\xBB\xD0\xB3\xD0\xB0\xD1\x80\xD1\x81\xD0\xBA\xD0\xB8", nullptr},
                {"ukr", "uk ukr ukrainian", "\xD0\xA3\xD0\xBA\xD1\x80\xD0\xB0\xD1\x97\xD0\xBD\xD1\x81\xD1\x8C\xD0\xBA\xD0\xB0", nullptr},
                {"hrv", "hr hrv croatian", "Hrvatski", nullptr},
                {"srp", "sr srp scc serbian", "Srpski", nullptr},
                {"bos", "bs bos bosnian", "Bosanski", nullptr},
                {"slv", "sl slv slovenian", "Sloven\xC5\xA1\xC4\x8Dina", nullptr},
                {"sqi", "sq sqi alb albanian", "Shqip", nullptr},
                {"mkd", "mk mkd mac macedonian", "\xD0\x9C\xD0\xB0\xD0\xBA\xD0\xB5\xD0\xB4\xD0\xBE\xD0\xBD\xD1\x81\xD0\xBA\xD0\xB8", nullptr},
                {"heb", "he heb iw hebrew ibranice", nullptr, "lang.heb"},
                {"fas", "fa fas per persian farsi farsca", nullptr, "lang.fas"},
                {"aze", "az aze azerbaijani", "Az\xC9\x99rbaycanca", nullptr},
                {"kur", "ku kur kmr ckb kurdish", "Kurd\xC3\xAE", nullptr},
                {"kat", "ka kat geo georgian", nullptr, "lang.kat"},
                {"hye", "hy hye arm armenian", nullptr, "lang.hye"},
                {"ind", "id ind indonesian", "Bahasa Indonesia", nullptr},
                {"msa", "ms msa may malay", "Bahasa Melayu", nullptr},
                {"tha", "th tha thai", nullptr, "lang.tha"},
                {"vie", "vi vie vietnamese", "Ti\xE1\xBA\xBFng Vi\xE1\xBB\x87t", nullptr},
                {"cat", "ca cat catalan", "Catal\xC3\xA0", nullptr},
                {"lit", "lt lit lithuanian", "Lietuvi\xC5\xB3", nullptr},
                {"lav", "lv lav latvian", "Latvie\xC5\xA1u", nullptr},
                {"est", "et est estonian", "Eesti", nullptr},
                {"isl", "is isl ice icelandic", "\xC3\x8Dslenska", nullptr},
                {"urd", "ur urd urdu", nullptr, "lang.urd"},
                {"ben", "bn ben bengali", nullptr, "lang.ben"},
                {"tam", "ta tam tamil", nullptr, "lang.tam"},
        };

        std::string lower(const std::string &s) {
            std::string out;
            for (char c: s) {
                out += (char) ((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
            }
            return out;
        }

        std::string trim(const std::string &s) {
            size_t b = s.find_first_not_of(" \t");
            if (b == std::string::npos) {
                return "";
            }
            return s.substr(b, s.find_last_not_of(" \t") - b + 1);
        }

        const Language *find(const std::string &code) {
            std::string c = lower(trim(code));
            // "en-US", "pt_BR": the primary subtag decides
            size_t sep = c.find_first_of("-_");
            if (sep != std::string::npos && sep > 0) {
                c = c.substr(0, sep);
            }
            if (c.empty()) {
                return nullptr;
            }
            for (const auto &l: LANGUAGES) {
                if (c == l.code) {
                    return &l;
                }
                const char *a = l.aliases;
                while (*a) {
                    const char *end = strchr(a, ' ');
                    size_t n = end ? (size_t) (end - a) : strlen(a);
                    if (c.size() == n && c.compare(0, n, a, n) == 0) {
                        return &l;
                    }
                    a += n;
                    while (*a == ' ') {
                        a++;
                    }
                }
            }
            return nullptr;
        }

        bool contains(const std::string &haystack, const char *needle) {
            return lower(haystack).find(needle) != std::string::npos;
        }
    }

    std::string normalizeLanguage(const std::string &code) {
        std::string c = lower(trim(code));
        if (c.empty() || c == "und" || c == "mul" || c == "zxx" || c == "mis" || c == "unknown") {
            return "";
        }
        const Language *l = find(c);
        return l ? l->code : c;
    }

    std::string languageName(const std::string &code) {
        std::string c = normalizeLanguage(code);
        if (c.empty()) {
            return "";
        }
        const Language *l = find(c);
        if (l) {
            return l->name ? l->name : i18n::tr(l->key);
        }
        std::string up;
        for (char ch: c) {
            if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-') {
                up += (char) ((ch >= 'a' && ch <= 'z') ? ch - 'a' + 'A' : ch);
            }
        }
        return up.substr(0, 12);
    }

    const std::vector<std::string> &commonLanguages() {
        static const std::vector<std::string> list = {"tur", "eng", "deu", "fra", "spa", "ita", "por", "rus", "ara",
                                                      "nld", "pol", "jpn", "kor", "zho", "ell", "swe", "dan", "nor",
                                                      "fin", "hun", "ces", "ron", "bul", "ukr", "hrv", "srp", "bos",
                                                      "sqi", "aze", "kur", "fas", "heb", "hin"};
        return list;
    }

    std::string effectiveLanguage(const Track &t) {
        std::string lang = normalizeLanguage(t.lang);
        if (!lang.empty()) {
            return lang;
        }
        // untagged track named after its language ("İngilizce", "English"): use the name
        const Language *l = find(t.title);
        return l ? l->code : "";
    }

    bool isCommentary(const Track &t) {
        return contains(t.title, "comment") || contains(t.title, "kommentar") || contains(t.title, "yorum")
               || contains(t.title, "comentario") || contains(t.title, "commento");
    }

    std::string codecName(const std::string &codec) {
        static const struct {
            const char *codec;
            const char *name;
        } names[] = {{"ac3", "AC-3"}, {"eac3", "E-AC-3"}, {"aac", "AAC"}, {"aac_latm", "AAC"}, {"dts", "DTS"},
                     {"truehd", "Dolby TrueHD"}, {"mp3", "MP3"}, {"mp2", "MP2"}, {"opus", "Opus"},
                     {"vorbis", "Vorbis"}, {"flac", "FLAC"}, {"subrip", "SRT"}, {"srt", "SRT"}, {"ass", "ASS"},
                     {"ssa", "SSA"}, {"webvtt", "WebVTT"}, {"mov_text", "Text"}, {"text", "Text"},
                     {"hdmv_pgs_subtitle", "PGS"}, {"dvd_subtitle", "VobSub"}, {"dvb_subtitle", "DVB"},
                     {"dvb_teletext", "Teletext"}};
        std::string c = lower(codec);
        if (c.compare(0, 4, "pcm_") == 0) {
            return "PCM";
        }
        for (const auto &n: names) {
            if (c == n.codec) {
                return n.name;
            }
        }
        std::string up;
        for (char ch: c) {
            up += (char) ((ch >= 'a' && ch <= 'z') ? ch - 'a' + 'A' : ch);
        }
        return up;
    }

    std::string channelsName(int channels) {
        switch (channels) {
            case 0:
                return "";
            case 1:
                return i18n::tr("track.mono");
            case 2:
                return i18n::tr("track.stereo");
            case 6:
                return "5.1";
            case 8:
                return "7.1";
            default:
                return i18n::tr("track.channels", {std::to_string(channels)});
        }
    }

    std::string label(const Track &t, int ordinal) {
        std::string name = languageName(effectiveLanguage(t));
        if (name.empty()) {
            name = trim(t.title);
        }
        if (name.empty()) {
            name = i18n::tr("track.number", {std::to_string(ordinal)});
        }
        if (isCommentary(t)) {
            name = i18n::tr("track.commentary", {name});
        } else if (t.forced) {
            name = i18n::tr("track.forced", {name});
        }
        return name;
    }

    std::string details(const Track &t) {
        std::string d = codecName(t.codec);
        auto add = [&d](const std::string &part) {
            if (!part.empty()) {
                d += (d.empty() ? "" : " \xC2\xB7 ") + part;
            }
        };
        if (t.kind == Kind::Audio) {
            add(channelsName(t.channels));
        } else {
            std::string c = lower(t.codec);
            if (c == "hdmv_pgs_subtitle" || c == "dvd_subtitle" || c == "dvb_subtitle") {
                add(i18n::tr("track.image"));
            }
        }
        if (t.external) {
            add(i18n::tr("track.external"));
        }
        return d;
    }

    int chooseAudio(const std::vector<Track> &list, const std::string &preferredLang) {
        std::vector<const Track *> audio;
        for (const auto &t: list) {
            if (t.kind == Kind::Audio && !isCommentary(t)) {
                audio.push_back(&t);
            }
        }
        if (audio.empty()) {
            for (const auto &t: list) {
                if (t.kind == Kind::Audio) {
                    audio.push_back(&t);   // only commentary tracks: still play something
                }
            }
        }
        if (audio.empty()) {
            return -1;
        }
        std::string pref = normalizeLanguage(preferredLang);
        if (!pref.empty()) {
            const Track *match = nullptr;
            for (const Track *t: audio) {
                if (effectiveLanguage(*t) == pref && (match == nullptr || (t->isDefault && !match->isDefault))) {
                    match = t;
                }
            }
            if (match) {
                return match->id;
            }
        }
        for (const Track *t: audio) {
            if (t->isDefault) {
                return t->id;
            }
        }
        return audio[0]->id;
    }

    int chooseSubtitle(const std::vector<Track> &list, SubtitleMode mode, const std::string &subtitleLang,
                       const std::string &audioLang) {
        std::vector<const Track *> subs;
        for (const auto &t: list) {
            if (t.kind == Kind::Subtitle) {
                subs.push_back(&t);
            }
        }
        if (subs.empty() || mode == SubtitleMode::Off) {
            return 0;
        }
        std::string pref = normalizeLanguage(subtitleLang);
        std::string audio = normalizeLanguage(audioLang);
        // a full (not forced-only, not commentary) track in a language
        auto inLanguage = [&subs](const std::string &lang) -> const Track * {
            const Track *best = nullptr;
            for (const Track *t: subs) {
                if (effectiveLanguage(*t) != lang || isCommentary(*t)) {
                    continue;
                }
                if (best == nullptr || (best->forced && !t->forced) || (t->isDefault && !best->isDefault
                                                                       && t->forced == best->forced)) {
                    best = t;
                }
            }
            return best;
        };
        if (mode == SubtitleMode::Always) {
            if (!pref.empty()) {
                if (const Track *t = inLanguage(pref)) {
                    return t->id;
                }
            }
            for (const Track *t: subs) {
                if (t->isDefault) {
                    return t->id;
                }
            }
            for (const Track *t: subs) {
                if (!t->forced && !isCommentary(*t)) {
                    return t->id;
                }
            }
            return subs[0]->id;
        }
        // Auto: subtitles in your language when the audio is in another one
        if (!pref.empty() && audio != pref) {
            if (const Track *t = inLanguage(pref)) {
                return t->id;
            }
        }
        // otherwise only what the file asks for: forced parts in the audio language, or a default track
        for (const Track *t: subs) {
            if (t->forced && (audio.empty() || effectiveLanguage(*t) == audio || effectiveLanguage(*t).empty())) {
                return t->id;
            }
        }
        for (const Track *t: subs) {
            if (t->isDefault) {
                return t->id;
            }
        }
        return 0;
    }

    const Track *selected(const std::vector<Track> &list, Kind kind) {
        for (const auto &t: list) {
            if (t.kind == kind && t.selected) {
                return &t;
            }
        }
        return nullptr;
    }

    std::vector<std::pair<std::string, std::string>> appearanceOptions(int size, int position, bool shadow) {
        const char *scale = size <= 0 ? "0.8" : size == 1 ? "1.0" : "1.3";
        const char *pos = position == 1 ? "88" : "100";   // percent of the screen height (mpv sub-pos)
        return {
                {"sub-scale", scale},
                {"sub-pos", pos},
                {"sub-shadow-offset", shadow ? "2" : "0"},
                {"sub-shadow-color", shadow ? "#C0000000" : "#80F0F0F0"},   // the second value is mpv's default
        };
    }
}
