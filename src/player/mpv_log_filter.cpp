#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mpv_log_filter.h"

namespace mpvlog {

    bool isFontSearchLine(const std::string &text, uint32_t *codePoint) {
        if (codePoint) {
            *codePoint = 0;
        }
        // libass 0.14 ass_font.c / ass_fontselect.c message texts
        if (text.compare(0, 8, "Glyph 0x") == 0 && text.find(" not found") != std::string::npos) {
            if (codePoint) {
                unsigned long v = strtoul(text.c_str() + 8, nullptr, 16);
                *codePoint = v <= 0x10FFFF ? (uint32_t) v : 0;
            }
            return true;
        }
        static const char FONTSELECT[] = "fontselect: ";
        static const char ALREADY[] = "Got a font face that already is available!";
        return text.compare(0, sizeof(FONTSELECT) - 1, FONTSELECT) == 0
               || text.compare(0, sizeof(ALREADY) - 1, ALREADY) == 0;
    }

    bool Filter::admit(const std::string &text, bool wouldWrite, double now) {
        uint32_t cp = 0;
        if (isFontSearchLine(text, &cp)) {
            if (cp != 0 && text.find("not found, selecting one more font") != std::string::npos) {
                missingTotal++;   // one search per missing glyph (the other lines of the search follow it)
                if (missing.size() < (size_t) MAX_CODE_POINTS || missing.count(cp)) {
                    missing.insert(cp);
                }
            }
            if (++fontLines <= FONT_LINES_KEPT) {
                return wouldWrite;
            }
            fontSuppressedSinceReport++;
            return false;
        }
        if (!wouldWrite) {
            return false;
        }
        if (windowStart < 0 || now - windowStart >= 1.0 || now < windowStart) {
            windowStart = now;
            windowLines = 0;
        }
        if (++windowLines > LINES_PER_SECOND) {
            rateSuppressed++;
            return false;
        }
        return true;
    }

    std::string Filter::fontSummary() {
        std::string cps;
        for (uint32_t c: missing) {
            char buf[16];
            snprintf(buf, sizeof(buf), " U+%04X", (unsigned) c);
            cps += buf;
        }
        char head[160];
        snprintf(head, sizeof(head), "subtitles: the subtitle font has no glyph for %d character(s)%s; "
                                     "%ld font search line(s) not written (%ld glyph searches)",
                 (int) missing.size(), missing.size() >= (size_t) MAX_CODE_POINTS ? " or more" : "",
                 fontSuppressedSinceReport, missingTotal);
        fontSuppressedSinceReport = 0;
        return std::string(head) + (cps.empty() ? "" : ":" + cps);
    }

    std::string Filter::takeSummary(double now) {
        std::string out;
        if (rateSuppressed > 0 && (windowStart < 0 || now - windowStart >= 1.0 || now < windowStart)) {
            char buf[120];
            snprintf(buf, sizeof(buf), "%ld mpv log line(s) not written (more than %d a second)", rateSuppressed,
                     LINES_PER_SECOND);
            out = buf;
            rateSuppressed = 0;
        }
        if (fontSuppressedSinceReport > 0 && (lastFontReport < 0 || now - lastFontReport >= FONT_REPORT_EVERY
                                              || now < lastFontReport)) {
            lastFontReport = now;
            out += (out.empty() ? "" : "\n") + fontSummary();
        }
        return out;
    }

    std::string Filter::endSession() {
        std::string out;
        if (rateSuppressed > 0) {
            char buf[120];
            snprintf(buf, sizeof(buf), "%ld mpv log line(s) not written (more than %d a second)", rateSuppressed,
                     LINES_PER_SECOND);
            out = buf;
        }
        if (fontSuppressedSinceReport > 0 || (missingTotal > 0 && lastFontReport < 0)) {
            out += (out.empty() ? "" : "\n") + fontSummary();
        }
        *this = Filter();
        return out;
    }
}
