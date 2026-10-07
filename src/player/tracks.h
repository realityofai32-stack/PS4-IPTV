// Audio / subtitle tracks (host-testable): readable names, labels and the automatic selection policy.
// The player reads mpv's track-list (id, type, lang, title, codec, demux-channel-count, default, forced,
// external, selected) into Track and switches with mpv's aid / sid properties - no decoder-level work.

#ifndef PS4IPTV_PLAYER_TRACKS_H
#define PS4IPTV_PLAYER_TRACKS_H

#include <string>
#include <utility>
#include <vector>

namespace tracks {

    enum class Kind {
        Audio,
        Subtitle
    };

    struct Track {
        int id = 0;                 // mpv track id (per type, from 1)
        Kind kind = Kind::Audio;
        std::string lang;           // as in the file: "tur", "en", "ger", "" ...
        std::string title;          // track name, often a language name in some language ("İngilizce")
        std::string codec;          // mpv codec name: "ac3", "subrip", "hdmv_pgs_subtitle" ...
        int channels = 0;
        bool isDefault = false;
        bool forced = false;
        bool external = false;
        bool selected = false;
    };

    // ISO 639-1 / 639-2B / 639-2T codes (and a few English names) -> canonical 639-2/T ("tr" -> "tur",
    // "ger" -> "deu"). "" for undetermined; unknown codes come back lowercased.
    std::string normalizeLanguage(const std::string &code);

    // "Türkçe", "English", "Deutsch" ...; unknown codes uppercased ("XYZ"); "" for "".
    std::string languageName(const std::string &code);

    // languages offered in Settings (canonical codes), in display order
    const std::vector<std::string> &commonLanguages();

    // the track's language: its tag, or - for untagged tracks - a language name used as the title
    // (the provider's files name untagged English tracks "İngilizce")
    std::string effectiveLanguage(const Track &t);

    bool isCommentary(const Track &t);

    // menu label: language name (or the track title, or "Track N"), with "Commentary" / "Forced" notes
    std::string label(const Track &t, int ordinal);

    // secondary line: "AC-3 · Stereo", "SubRip", "PGS (image)"
    std::string details(const Track &t);

    std::string codecName(const std::string &codec);

    std::string channelsName(int channels);

    // Audio policy: 1. preferred language (not commentary; the file's default first)  2. the track marked
    // default  3. the first track. preferredLang "" = Auto (the file's default). Returns a track id,
    // -1 when there is no audio track.
    int chooseAudio(const std::vector<Track> &tracks, const std::string &preferredLang);

    enum class SubtitleMode {
        Off,       // never automatically
        Auto,      // when the audio is not in your subtitle language, or the file marks a track default/forced
        Always     // "On when available": your language, else default, else the first track
    };

    // Returns a subtitle track id, or 0 for none. subtitleLang "" = no language preference.
    int chooseSubtitle(const std::vector<Track> &tracks, SubtitleMode mode, const std::string &subtitleLang,
                       const std::string &audioLang);

    // the selected track of a kind (nullptr when none)
    const Track *selected(const std::vector<Track> &tracks, Kind kind);

    // subtitle appearance -> mpv options (sub-scale, sub-pos, sub-shadow-offset, sub-shadow-color).
    // size 0 small / 1 medium / 2 large, position 0 bottom / 1 raised. Text subtitles only (libass).
    std::vector<std::pair<std::string, std::string>> appearanceOptions(int size, int position, bool shadow);
}

#endif // PS4IPTV_PLAYER_TRACKS_H
