// Subtitle rendering harness: drives libass 0.14.0 (the PS4 build's configuration: no fontconfig, no HarfBuzz, C
// rasterizer; tests/subtitles/build-host-ass-stack.sh) the way libmpv 0.34.1's sd_ass does for a text subtitle
// track, frame by frame, and replays every libass message through the app's own log path (diag::write after
// mpv's level mapping and Playback::onLog's level mapping) - the cost the app pays per message on the console.
//
// mpv 0.34.1 behaviour reproduced (sub/ass_mp.c, sub/sd_ass.c):
//   - ass_set_fonts_dir(<config-dir>/fonts) when that folder exists; ass_set_extract_fonts(embeddedfonts=yes)
//   - ass_set_fonts(renderer, <config-dir>/subfont.ttf, sub-font, ASS_FONTPROVIDER_AUTODETECT, NULL, 1)
//   - converted (SRT) tracks: mp_ass_default_track (PlayResY 288, style from the sub-* options: font size 55 at 720,
//     border 3, shadow offset, margin 22, bottom centre), ass_set_font_scale(sub-scale)
//   - libass message levels mapped like ass_mp.c's map_ass_level, every frame rendered with ass_render_frame
// Usage: ass_bench --sub FILE [--subfont PATH] [--fonts-dir DIR] [--family NAME] [--fps N] [--from S] [--to S]
//                  [--scale X] [--shadow N] [--applog FILE] [--timing] [--quiet] [--filter]
// Output: one "key value" per line (parsed by run-subtitle-tests.sh).

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include <ass/ass.h>
}

#include "platform/fs.h"
#include "platform/log.h"
#include "player/mpv_log_filter.h"

namespace {

    // mpv msg levels (common/msg.h) and ass_mp.c's mapping of libass levels 0..7
    enum { MSGL_FATAL, MSGL_ERR, MSGL_WARN, MSGL_INFO, MSGL_STATUS, MSGL_V, MSGL_DEBUG, MSGL_TRACE };
    const int MAP_ASS_LEVEL[] = {MSGL_ERR, MSGL_WARN, MSGL_INFO, MSGL_V, MSGL_V, MSGL_DEBUG, MSGL_TRACE, MSGL_TRACE};

    struct Counters {
        long byMpvLevel[8] = {0};
        long glyphMissingInFont = 0;      // "Glyph 0x%X not found in font for" (error)
        long glyphSelectMore = 0;         // "Glyph 0x%X not found, selecting one more font"
        long defaultFontUsed = 0;         // "fontselect: Using default font:" (the default font path chosen)
        long faceAlreadyAvailable = 0;    // "Got a font face that already is available! Skipping."
        long appLogged = 0;               // lines the app writes to log.txt (non-verbose)
        long appVerboseDropped = 0;       // verbose lines (dropped unless verbose logging is on)
        double appLogSeconds = 0;         // time spent in diag::write
        std::map<std::string, long> distinct;
    };

    Counters g;
    bool g_appLog = false;
    bool g_useFilter = false;        // --filter: the app's mpvlog::Filter in front of diag::write (Playback::onLog)
    mpvlog::Filter g_filter;
    double g_now = 0;                // the frame time (the filter's clock)
    bool g_mpvClientWantsVerbose = true;   // Playback::init: mpv_request_log_messages(h, "v")

    double nowSec() {
        using namespace std::chrono;
        return (double) duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count() / 1e9;
    }

    void onAssMessage(int level, const char *fmt, va_list va, void *) {
        char buf[1024];
        vsnprintf(buf, sizeof(buf), fmt, va);
        int mpvLevel = MAP_ASS_LEVEL[std::min(std::max(level, 0), 7)];
        g.byMpvLevel[mpvLevel]++;
        if (strstr(buf, "not found in font for")) {
            g.glyphMissingInFont++;
        } else if (strstr(buf, "not found, selecting one more font")) {
            g.glyphSelectMore++;
        } else if (strstr(buf, "fontselect: Using default font:")) {
            g.defaultFontUsed++;
        } else if (strstr(buf, "already is available")) {
            g.faceAlreadyAvailable++;
        }
        // message kinds: digits / code points folded, event dumps cut short
        std::string key;
        for (const char *c = buf; *c && key.size() < 60; c++) {
            if (*c >= '0' && *c <= '9') {
                if (key.empty() || key.back() != '#') {
                    key += '#';
                }
            } else {
                key += *c;
            }
        }
        g.distinct[key]++;
        // what reaches the app: the client asked for "v" (MSGL_V and more severe)
        if (!g_mpvClientWantsVerbose || mpvLevel > MSGL_V) {
            return;
        }
        // Playback::onLog: error -> Error, warn -> Warn, info -> Info, everything else -> Verbose
        diag::Level appLevel = mpvLevel <= MSGL_ERR ? diag::Level::Error : mpvLevel == MSGL_WARN ? diag::Level::Warn
                               : mpvLevel == MSGL_INFO ? diag::Level::Info : diag::Level::Verbose;
        bool wouldWrite = appLevel != diag::Level::Verbose;
        if (g_useFilter && !g_filter.admit(buf, wouldWrite, g_now)) {
            return;
        }
        if (!wouldWrite) {
            g.appVerboseDropped++;
        } else {
            g.appLogged++;
        }
        if (g_appLog) {
            double t0 = nowSec();
            diag::write(appLevel, "mpv/sub/ass", buf);
            g.appLogSeconds += nowSec() - t0;
        }
    }

    std::string readFile(const std::string &path) {
        std::string s;
        FILE *f = fopen(path.c_str(), "rb");
        if (!f) {
            return s;
        }
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            s.append(buf, n);
        }
        fclose(f);
        return s;
    }

    struct SrtEvent {
        long long start = 0, duration = 0;
        std::string text;
    };

    // SRT -> events (what FFmpeg's subrip decoder hands mpv: one ASS dialogue per block, lines joined with \N)
    bool parseSrtTime(const char *s, long long *ms) {
        int h, m, sec, milli;
        if (sscanf(s, "%d:%d:%d%*[,.]%d", &h, &m, &sec, &milli) != 4) {
            return false;
        }
        *ms = ((long long) h * 3600 + m * 60 + sec) * 1000 + milli;
        return true;
    }

    std::vector<SrtEvent> parseSrt(const std::string &data, long *malformed) {
        std::vector<SrtEvent> out;
        std::vector<std::string> lines;
        size_t pos = 0;
        if (data.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            pos = 3;
        }
        while (pos <= data.size()) {
            size_t nl = data.find('\n', pos);
            if (nl == std::string::npos) {
                nl = data.size();
            }
            std::string l = data.substr(pos, nl - pos);
            if (!l.empty() && l.back() == '\r') {
                l.pop_back();
            }
            lines.push_back(l);
            pos = nl + 1;
        }
        for (size_t i = 0; i < lines.size(); i++) {
            size_t arrow = lines[i].find("-->");
            if (arrow == std::string::npos) {
                continue;
            }
            SrtEvent e;
            long long a, b;
            if (!parseSrtTime(lines[i].c_str(), &a) || !parseSrtTime(lines[i].c_str() + arrow + 3, &b) || b < a) {
                (*malformed)++;
                continue;
            }
            e.start = a;
            e.duration = b - a;
            for (i++; i < lines.size() && !lines[i].empty(); i++) {
                e.text += (e.text.empty() ? "" : "\\N") + lines[i];
            }
            out.push_back(e);
        }
        return out;
    }

    void usage() {
        fprintf(stderr, "usage: ass_bench --sub FILE [--subfont PATH] [--fonts-dir DIR] [--family NAME] [--fps N] "
                        "[--from S] [--to S] [--scale X] [--shadow N] [--applog FILE] [--timing] [--quiet]\n");
        exit(2);
    }
}

int main(int argc, char **argv) {
    std::string sub, subfont, fontsDir, family = "sans-serif", applog;
    double fps = 30, from = 0, to = -1, scale = 1.0, shadow = 2;
    bool timing = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                usage();
            }
            return argv[++i];
        };
        if (a == "--sub") sub = next();
        else if (a == "--subfont") subfont = next();
        else if (a == "--fonts-dir") fontsDir = next();
        else if (a == "--family") family = next();
        else if (a == "--fps") fps = atof(next().c_str());
        else if (a == "--from") from = atof(next().c_str());
        else if (a == "--to") to = atof(next().c_str());
        else if (a == "--scale") scale = atof(next().c_str());
        else if (a == "--shadow") shadow = atof(next().c_str());
        else if (a == "--applog") applog = next();
        else if (a == "--timing") timing = true;
        else if (a == "--quiet") g_mpvClientWantsVerbose = false;
        else if (a == "--filter") g_useFilter = true;
        else usage();
    }
    if (sub.empty() || fps <= 0) {
        usage();
    }
    if (!applog.empty()) {
        diag::init(applog);
        diag::setVerbose(false);   // the default: Settings > Diagnostics > Verbose logging off
        g_appLog = true;
    }

    ASS_Library *lib = ass_library_init();
    ass_set_message_cb(lib, onAssMessage, nullptr);
    if (!fontsDir.empty()) {
        ass_set_fonts_dir(lib, fontsDir.c_str());
    }
    ass_set_extract_fonts(lib, 1);
    ASS_Renderer *r = ass_renderer_init(lib);
    const int W = 1920, H = 1080;
    ass_set_frame_size(r, W, H);
    ass_set_storage_size(r, W, H);
    ass_set_shaper(r, ASS_SHAPING_COMPLEX);   // mpv default "complex": the simple FriBidi shaper without HarfBuzz
    ass_set_hinting(r, ASS_HINTING_NONE);
    ass_set_line_spacing(r, 0);
    double tSetup = nowSec();
    ass_set_fonts(r, subfont.empty() ? nullptr : subfont.c_str(), family.c_str(), ASS_FONTPROVIDER_AUTODETECT,
                  nullptr, 1);
    tSetup = nowSec() - tSetup;

    std::string data = readFile(sub);
    bool isAss = sub.size() > 4 && (sub.compare(sub.size() - 4, 4, ".ass") == 0 || sub.compare(sub.size() - 4, 4, ".ssa") == 0);
    ASS_Track *track = nullptr;
    long malformed = 0;
    std::vector<SrtEvent> srt;
    if (isAss) {
        track = ass_read_memory(lib, &data[0], data.size(), nullptr);
        if (!track) {
            printf("error ass_read_memory failed\n");
            return 1;
        }
    } else {
        // sd_ass init for a converted track: FFmpeg's generated header (ff_ass_subtitle_header_default, lavc_conv
        // extradata), then configure_ass: mp_ass_set_style on the default style and the selective style override
        // (font name, size fields, colours, border; sub-font-size 55, border 3, margin-y 22 at PlayResY 720)
        static const char HEADER[] =
                "[Script Info]\n; Script generated by FFmpeg/Lavc\nScriptType: v4.00+\nPlayResX: 384\nPlayResY: 288\n"
                "ScaledBorderAndShadow: yes\nYCbCr Matrix: None\n\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, "
                "PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
                "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
                "Style: Default,Arial,16,&Hffffff,&Hffffff,&H0,&H0,0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,1\n\n"
                "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
        track = ass_new_track(lib);
        std::string header = HEADER;
        ass_process_codec_private(track, &header[0], (int) header.size());
        auto mpStyle = [&](ASS_Style *st, double resY) {
            free(st->FontName);
            st->FontName = strdup(family.c_str());
            double k = resY / 720.0;
            st->FontSize = 55 * k;
            st->PrimaryColour = 0xFFFFFF00;
            st->SecondaryColour = st->PrimaryColour;
            st->OutlineColour = 0x00000000;
            st->BackColour = 0x000000C0;
            st->BorderStyle = 1;
            st->Outline = 3 * k;
            st->Shadow = shadow * k;
            st->Spacing = 0;
            st->MarginL = st->MarginR = (int) (25 * k);
            st->MarginV = (int) (22 * k);
            st->ScaleX = st->ScaleY = 1.0;
            st->Alignment = 2;
            st->Blur = 0;
            st->Bold = 0;
            st->Italic = 0;
        };
        if (track->default_style < track->n_styles) {
            mpStyle(&track->styles[track->default_style], track->PlayResY);
        }
        ASS_Style over = {};
        mpStyle(&over, 288);
        ass_set_selective_style_override_enabled(r, ASS_OVERRIDE_BIT_FONT_NAME | ASS_OVERRIDE_BIT_FONT_SIZE_FIELDS
                                                    | ASS_OVERRIDE_BIT_COLORS | ASS_OVERRIDE_BIT_BORDER
                                                    | ASS_OVERRIDE_BIT_SELECTIVE_FONT_SCALE);
        ass_set_selective_style_override(r, &over);
        free(over.FontName);
        ass_set_line_position(r, 0);
        ass_set_font_scale(r, scale);
        srt = parseSrt(data, &malformed);
        long readOrder = 0;
        for (const auto &e: srt) {
            // ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text (FFmpeg numbers the events)
            std::string line = std::to_string(readOrder++) + ",0,Default,,0,0,0,," + e.text;
            ass_process_chunk(track, &line[0], (int) line.size(), e.start, e.duration);
        }
    }
    long long lastEnd = 0;
    for (int i = 0; i < track->n_events; i++) {
        lastEnd = std::max(lastEnd, track->events[i].Start + track->events[i].Duration);
    }
    if (to < 0) {
        to = (double) lastEnd / 1000.0 + 0.5;
    }

    // frame loop: mpv renders the subtitles for every video frame while a subtitle track is selected
    std::vector<double> frameMs;
    long frames = 0, framesWithImage = 0, images = 0, changes = 0;
    long timingMismatch = 0;
    long long pixels = 0;
    for (double t = from; t < to; t += 1.0 / fps) {
        long long ms = (long long) (t * 1000.0 + 0.5);
        g_now = t;
        if (g_useFilter) {
            std::string held = g_filter.takeSummary(t);
            if (!held.empty()) {
                g.appLogged++;   // Playback::update writes the summary as one warning
                if (g_appLog) {
                    diag::write(diag::Level::Warn, "player", held);
                }
            }
        }
        int changed = 0;
        double t0 = nowSec();
        ASS_Image *img = ass_render_frame(r, track, ms, &changed);
        double dt = (nowSec() - t0) * 1000.0;
        frameMs.push_back(dt);
        frames++;
        changes += changed != 0;
        if (img) {
            framesWithImage++;
        }
        for (ASS_Image *p = img; p; p = p->next) {
            images++;
            pixels += (long long) p->w * p->h;
        }
        if (timing) {
            // an event is due exactly when Start <= t < Start + Duration (libass), with something to draw
            bool due = false;
            for (int i = 0; i < track->n_events && !due; i++) {
                const ASS_Event &e = track->events[i];
                due = ms >= e.Start && ms < e.Start + e.Duration && e.Text && e.Text[0];
            }
            if (due != (img != nullptr)) {
                timingMismatch++;
            }
        }
    }
    std::vector<double> sorted = frameMs;
    std::sort(sorted.begin(), sorted.end());
    auto pct = [&](double p) { return sorted.empty() ? 0.0 : sorted[std::min(sorted.size() - 1, (size_t) (p * sorted.size()))]; };
    double total = 0;
    for (double v: frameMs) {
        total += v;
    }
    long over = 0;   // frames whose subtitle render alone exceeded a 60 fps budget share (4 ms)
    for (double v: frameMs) {
        over += v > 4.0;
    }

    if (g_useFilter) {
        std::string held = g_filter.endSession();
        if (!held.empty()) {
            g.appLogged++;
            printf("filter_summary %s\n", held.substr(0, held.find('\n')).c_str());
        }
    }
    printf("file %s\n", sub.c_str());
    printf("events %d\n", track->n_events);
    printf("srt_malformed %ld\n", malformed);
    printf("setup_ms %.3f\n", tSetup * 1000);
    printf("frames %ld\n", frames);
    printf("frames_with_image %ld\n", framesWithImage);
    printf("images %ld\n", images);
    printf("pixels %lld\n", pixels);
    printf("render_total_ms %.3f\n", total);
    printf("render_mean_ms %.4f\n", frames ? total / frames : 0.0);
    printf("render_p50_ms %.4f\n", pct(0.50));
    printf("render_p95_ms %.4f\n", pct(0.95));
    printf("render_p99_ms %.4f\n", pct(0.99));
    printf("render_max_ms %.4f\n", sorted.empty() ? 0.0 : sorted.back());
    printf("frames_over_4ms %ld\n", over);
    printf("msg_err %ld\n", g.byMpvLevel[MSGL_ERR] + g.byMpvLevel[MSGL_FATAL]);
    printf("msg_warn %ld\n", g.byMpvLevel[MSGL_WARN]);
    printf("msg_info %ld\n", g.byMpvLevel[MSGL_INFO]);
    printf("msg_v %ld\n", g.byMpvLevel[MSGL_V]);
    printf("msg_debug %ld\n", g.byMpvLevel[MSGL_DEBUG] + g.byMpvLevel[MSGL_TRACE]);
    printf("glyph_missing_in_font %ld\n", g.glyphMissingInFont);
    printf("glyph_select_more %ld\n", g.glyphSelectMore);
    printf("default_font_used %ld\n", g.defaultFontUsed);
    printf("face_already_available %ld\n", g.faceAlreadyAvailable);
    printf("app_lines_logged %ld\n", g.appLogged);
    printf("app_verbose_dropped %ld\n", g.appVerboseDropped);
    printf("app_log_ms %.3f\n", g.appLogSeconds * 1000);
    printf("timing_mismatch %ld\n", timingMismatch);
    for (const auto &kv: g.distinct) {
        printf("distinct %ld %s\n", kv.second, kv.first.c_str());
    }
    ass_free_track(track);
    ass_renderer_done(r);
    ass_library_done(lib);
    if (g_appLog) {
        diag::shutdown();
    }
    return 0;
}
