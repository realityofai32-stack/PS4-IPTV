// Time the main thread spends in mpv_render_context_render (video frame + OSD + libass subtitles), counted by the
// video texture and summarised per playback session by Playback (diagnostics: GitHub issue #1).

#ifndef PS4IPTV_PLAYER_RENDER_TIMING_H
#define PS4IPTV_PLAYER_RENDER_TIMING_H

struct RenderTiming {
    unsigned long frames = 0;    // mpv_render_context_render calls
    double totalMs = 0;
    double maxMs = 0;
    unsigned long slow = 0;      // calls that took longer than SLOW_MS
    static constexpr double SLOW_MS = 33.0;

    void add(double ms) {
        frames++;
        totalMs += ms;
        if (ms > maxMs) {
            maxMs = ms;
        }
        if (ms > SLOW_MS) {
            slow++;
        }
    }
};

#endif // PS4IPTV_PLAYER_RENDER_TIMING_H
