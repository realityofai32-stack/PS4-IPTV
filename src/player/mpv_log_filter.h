// What of mpv's log reaches the log file (host-testable).
//
// Playback asks mpv for its verbose log (needed to detect a missing precompiled shader), and every line the app
// writes costs a redaction pass, a write + flush of log.txt and a kernel debug print on the main thread - the thread
// that also draws the video. Two kinds of floods made that cost visible as stutter:
//   - libass font searches: a subtitle character the subtitle font has no glyph for is searched again for every
//     glyph of every frame, with up to five lines each time (GitHub issue #1: Arabic subtitles with a Latin-only
//     font: ~80 lines per frame). Only the first few are kept; the rest become one summary with the code points.
//   - any other burst (a broken live stream makes FFmpeg print a decode error per damaged frame): at most
//     LINES_PER_SECOND lines a second are written, the rest are counted and reported once a second.
// Suppressed lines are still parsed by Playback::onLog for errors: only the writing is limited.

#ifndef PS4IPTV_PLAYER_MPV_LOG_FILTER_H
#define PS4IPTV_PLAYER_MPV_LOG_FILTER_H

#include <cstdint>
#include <set>
#include <string>

namespace mpvlog {

    // a libass font-search line ("Glyph 0x627 not found ...", "fontselect: ...", "Got a font face that already is
    // available! Skipping."); *codePoint is set for the "Glyph 0x..." lines (0 otherwise)
    bool isFontSearchLine(const std::string &text, uint32_t *codePoint = nullptr);

    class Filter {

    public:

        static const int LINES_PER_SECOND = 60;
        static const int FONT_LINES_KEPT = 6;      // per playback session: enough to see which font was chosen
        static const int MAX_CODE_POINTS = 24;     // listed in a summary (the total is counted)
        static constexpr double FONT_REPORT_EVERY = 30.0;

        // true: write the line. `wouldWrite`: the log would keep it (not a verbose line with verbose logging off);
        // lines the log drops anyway are not counted against the budget.
        bool admit(const std::string &text, bool wouldWrite, double now);

        // a summary of what was held back since the last one ("" when nothing is due): call every frame
        std::string takeSummary(double now);

        // end of a playback session: the final summary ("" if nothing was suppressed), counters cleared
        std::string endSession();

        long fontSearchLines() const { return fontLines; }

        const std::set<uint32_t> &missingCodePoints() const { return missing; }

    private:

        std::string fontSummary();

        // font searches (this session)
        long fontLines = 0;
        long fontSuppressedSinceReport = 0;
        long missingTotal = 0;
        std::set<uint32_t> missing;
        double lastFontReport = -1;
        // rate limit (one-second windows)
        double windowStart = -1;
        int windowLines = 0;
        long rateSuppressed = 0;
    };
}

#endif // PS4IPTV_PLAYER_MPV_LOG_FILTER_H
