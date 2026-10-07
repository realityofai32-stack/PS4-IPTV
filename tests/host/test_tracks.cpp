// Audio / subtitle tracks: language names (ISO 639 variants), labels and the selection policy, using the
// track layouts found in the provider's MKV files (Turkish default audio + English/Spanish, SRT subtitles
// in Turkish/English/German, none flagged default; English often has no language element).

#include "check.h"
#include "../../src/player/tracks.h"

using namespace tracks;

namespace {
    Track audio(int id, const char *lang, const char *title, bool def, int ch = 2, const char *codec = "ac3") {
        Track t;
        t.id = id;
        t.kind = Kind::Audio;
        t.lang = lang;
        t.title = title;
        t.isDefault = def;
        t.channels = ch;
        t.codec = codec;
        return t;
    }

    Track sub(int id, const char *lang, const char *title, bool def = false, bool forced = false) {
        Track t;
        t.id = id;
        t.kind = Kind::Subtitle;
        t.lang = lang;
        t.title = title;
        t.isDefault = def;
        t.forced = forced;
        t.codec = "subrip";
        return t;
    }

    // movie 96825 as probed: tur (default) + eng audio, tur/eng/ger SRT
    std::vector<Track> providerMovie() {
        return {audio(1, "tur", "T\xC3\xBCrk\xC3\xA7" "e", true), audio(2, "eng", "\xC4\xB0ngilizce", false),
                sub(1, "tur", "T\xC3\xBCrk\xC3\xA7" "e"), sub(2, "eng", "\xC4\xB0ngilizce"), sub(3, "ger", "Almanca")};
    }
}

TEST(track_language_names) {
    CHECK(normalizeLanguage("tr") == "tur" && normalizeLanguage("TUR") == "tur");
    CHECK(normalizeLanguage("ger") == "deu" && normalizeLanguage("de") == "deu" && normalizeLanguage("deu") == "deu");
    CHECK(normalizeLanguage("fre") == "fra" && normalizeLanguage("fr") == "fra");
    CHECK(normalizeLanguage("en-US") == "eng" && normalizeLanguage("pt_BR") == "por");
    CHECK(normalizeLanguage("English") == "eng");
    CHECK(normalizeLanguage("und").empty() && normalizeLanguage("").empty() && normalizeLanguage(" ").empty());
    CHECK(normalizeLanguage("xyz") == "xyz");
    CHECK(languageName("tur") == "T\xC3\xBCrk\xC3\xA7" "e" && languageName("tr") == "T\xC3\xBCrk\xC3\xA7" "e");
    CHECK(languageName("eng") == "English" && languageName("ger") == "Deutsch");
    CHECK(languageName("fre") == "Fran\xC3\xA7" "ais" && languageName("es") == "Espa\xC3\xB1ol");
    CHECK(languageName("ara") == "Arabic");     // script not in the UI font: English name
    CHECK(languageName("xyz") == "XYZ" && languageName("").empty() && languageName("und").empty());
    CHECK(languageName("<b>bad</b>") == "BBADB");   // unknown values are sanitized
    for (const auto &code: commonLanguages()) {
        CHECK(!languageName(code).empty());
    }
}

TEST(track_labels_and_details) {
    std::vector<Track> t = providerMovie();
    CHECK(label(t[0], 1) == "T\xC3\xBCrk\xC3\xA7" "e");
    CHECK(label(t[1], 2) == "English");                 // language wins over the Turkish title "İngilizce"
    CHECK(details(t[0]) == "AC-3 \xC2\xB7 Stereo");
    CHECK(label(t[4], 3) == "Deutsch" && details(t[4]) == "SRT");
    Track noLang = audio(3, "", "", false, 6, "eac3");
    CHECK(label(noLang, 3) == "Track 3" && details(noLang) == "E-AC-3 \xC2\xB7 5.1");
    Track titled = audio(4, "", "Director's Commentary", false);
    CHECK(label(titled, 4) == "Director's Commentary (Commentary)");
    Track forced = sub(5, "eng", "Forced", false, true);
    CHECK(label(forced, 1) == "English (Forced)");
    Track pgs = sub(6, "fra", "");
    pgs.codec = "hdmv_pgs_subtitle";
    pgs.external = true;
    CHECK(details(pgs) == "PGS \xC2\xB7 image \xC2\xB7 external");
    CHECK(codecName("pcm_s16le") == "PCM" && codecName("weird") == "WEIRD" && channelsName(8) == "7.1");
}

TEST(track_audio_policy) {
    std::vector<Track> t = providerMovie();
    CHECK_EQ(chooseAudio(t, ""), 1);        // Auto: the file's default (Turkish)
    CHECK_EQ(chooseAudio(t, "eng"), 2);
    CHECK_EQ(chooseAudio(t, "en"), 2);      // any ISO variant
    CHECK_EQ(chooseAudio(t, "tur"), 1);
    CHECK_EQ(chooseAudio(t, "deu"), 1);     // not available: default
    // never a commentary track unless it is the only one
    std::vector<Track> c = {audio(1, "eng", "Commentary", true), audio(2, "eng", "", false), audio(3, "tur", "", false)};
    CHECK_EQ(chooseAudio(c, "eng"), 2);
    CHECK_EQ(chooseAudio(c, ""), 2);        // default is commentary: first normal track
    CHECK_EQ(chooseAudio({audio(4, "eng", "Commentary", false)}, ""), 4);
    // two tracks in the preferred language: the default one
    std::vector<Track> two = {audio(1, "tur", "", false), audio(2, "tur", "", true)};
    CHECK_EQ(chooseAudio(two, "tur"), 2);
    CHECK_EQ(chooseAudio({}, "tur"), -1);
    CHECK_EQ(chooseAudio({sub(1, "tur", "")}, "tur"), -1);
}

TEST(track_subtitle_policy) {
    std::vector<Track> t = providerMovie();
    // Off: never
    CHECK_EQ(chooseSubtitle(t, SubtitleMode::Off, "tur", "eng"), 0);
    // Auto: Turkish audio with Turkish subtitle preference -> no subtitles
    CHECK_EQ(chooseSubtitle(t, SubtitleMode::Auto, "tur", "tur"), 0);
    // Auto: English audio, Turkish preference -> Turkish subtitles
    CHECK_EQ(chooseSubtitle(t, SubtitleMode::Auto, "tur", "eng"), 1);
    CHECK_EQ(chooseSubtitle(t, SubtitleMode::Auto, "tr", "en"), 1);
    // Auto without any preference: only what the file asks for (nothing here)
    CHECK_EQ(chooseSubtitle(t, SubtitleMode::Auto, "", "eng"), 0);
    // On when available
    CHECK_EQ(chooseSubtitle(t, SubtitleMode::Always, "deu", "tur"), 3);
    CHECK_EQ(chooseSubtitle(t, SubtitleMode::Always, "", "tur"), 1);
    CHECK_EQ(chooseSubtitle(t, SubtitleMode::Always, "spa", "tur"), 1);   // not available: first
    // forced / default flags
    std::vector<Track> f = {sub(1, "eng", "", false, true), sub(2, "eng", ""), sub(3, "tur", "", true)};
    CHECK_EQ(chooseSubtitle(f, SubtitleMode::Auto, "", "eng"), 1);         // forced parts of the audio language
    CHECK_EQ(chooseSubtitle(f, SubtitleMode::Auto, "eng", "tur"), 2);      // full track preferred over forced
    CHECK_EQ(chooseSubtitle(f, SubtitleMode::Always, "", "tur"), 3);       // the default one
    std::vector<Track> d = {sub(1, "fra", ""), sub(2, "spa", "", true)};
    CHECK_EQ(chooseSubtitle(d, SubtitleMode::Auto, "", "eng"), 2);
    CHECK_EQ(chooseSubtitle({}, SubtitleMode::Always, "tur", "eng"), 0);
    // selected()
    t[1].selected = true;
    CHECK(selected(t, Kind::Audio) && selected(t, Kind::Audio)->id == 2);
    CHECK(selected(t, Kind::Subtitle) == nullptr);
}

TEST(subtitle_appearance_options) {
    auto o = appearanceOptions(2, 1, true);
    CHECK(o.size() == 4);
    CHECK(o[0].first == "sub-scale" && o[0].second == "1.3");
    CHECK(o[1].first == "sub-pos" && o[1].second == "88");
    CHECK(o[2].first == "sub-shadow-offset" && o[2].second == "2");
    auto d = appearanceOptions(1, 0, false);
    CHECK(d[0].second == "1.0" && d[1].second == "100" && d[2].second == "0");
    CHECK(appearanceOptions(0, 0, false)[0].second == "0.8");
}

TEST(track_untagged_language_from_title) {
    // the provider's English tracks often carry no language element, only the Turkish title
    std::vector<Track> t = {audio(1, "tur", "T\xC3\xBCrk\xC3\xA7" "e", true), audio(2, "", "\xC4\xB0ngilizce", false),
                            sub(1, "", "T\xC3\xBCrk\xC3\xA7" "e"), sub(2, "", "Almanca")};
    CHECK(effectiveLanguage(t[1]) == "eng" && effectiveLanguage(t[3]) == "deu");
    CHECK(label(t[1], 2) == "English" && label(t[3], 2) == "Deutsch");
    CHECK_EQ(chooseAudio(t, "eng"), 2);
    CHECK_EQ(chooseSubtitle(t, SubtitleMode::Auto, "tur", "eng"), 1);
    Track odd = audio(3, "", "Main track", false);
    CHECK(effectiveLanguage(odd).empty() && label(odd, 3) == "Main track");
}

#include "../../src/storage/settings_store.h"

TEST(settings_language_and_subtitle_roundtrip) {
    SettingsStore s("unused");
    CHECK(s.get().audioLanguage.empty() && s.get().subtitleMode == SubtitleMode::Auto);
    CHECK(s.get().subtitleSize == 1 && s.get().subtitlePosition == 0 && !s.get().subtitleShadow);
    s.get().audioLanguage = "eng";
    s.get().subtitleMode = SubtitleMode::Always;
    s.get().subtitleLanguage = "tur";
    s.get().subtitleSize = 2;
    s.get().subtitlePosition = 1;
    s.get().subtitleShadow = true;
    SettingsStore t("unused");
    std::string err;
    CHECK(t.deserialize(s.serialize(), &err));
    CHECK(t.get().audioLanguage == "eng" && t.get().subtitleMode == SubtitleMode::Always);
    CHECK(t.get().subtitleLanguage == "tur" && t.get().subtitleSize == 2 && t.get().subtitlePosition == 1);
    CHECK(t.get().subtitleShadow);
    // older files and odd values: defaults, never crashes or bad indices
    CHECK(t.deserialize(R"({"version":1,"subtitleMode":"sometimes","subtitleSize":"huge","audioLanguage":"<x>"})", &err));
    CHECK(t.get().subtitleMode == SubtitleMode::Auto && t.get().subtitleSize == 1 && t.get().audioLanguage == "x");
    CHECK(t.deserialize(R"({"audioLanguage":"abcdefghijk"})", &err) && t.get().audioLanguage.empty());
}
