// Thin adapter around the hardware-proven pPlay playback backend (player/pplay/mpv.*): same mpv options,
// same lifecycle (Mpv::load with "pause=yes,speed=1", resume on MPV_EVENT_FILE_LOADED, Mpv::stop), same
// render path (VideoTexture). Adds state tracking, stream information and user-facing errors.

#ifndef PS4IPTV_PLAYER_PLAYBACK_H
#define PS4IPTV_PLAYER_PLAYBACK_H

#include <string>
#include <vector>

#include "pplay/mpv.h"

enum class PlaybackState {
    Idle,
    Opening,      // loadfile sent, file not loaded yet
    Buffering,    // loaded, waiting for the first frame / cache
    Playing,
    Paused,
    Ended,        // end of stream / file
    Error
};

enum class PlaybackError {
    None,
    Http403,          // provider refused (often: previous connection still counted)
    HttpOther,
    HttpsUnsupported,
    Network,          // DNS / connect / timeout
    Demux,
    UnsupportedCodec,
    Renderer,         // incl. missing precompiled shader
    Audio,
    Other
};

struct StreamInfo {
    std::string format;        // "TS" / "HLS" / container extension
    std::string container;
    std::string videoCodec;
    std::string pixelFormat;
    int width = 0;
    int height = 0;
    double fps = 0;
    std::string audioCodec;
    int sampleRate = 0;
    int channels = 0;
    std::string audioOutput;   // "sdl" when the PS4 audio output is open
    double cacheSeconds = 0;
    long long droppedFrames = 0;
    double position = 0;
    double duration = 0;       // 0 for live
    bool seekable = false;
};

class Playback {

public:

    // Creates pPlay's Mpv (needs the GL context). Returns false with initError() on failure.
    bool init(const std::string &configDir);

    void shutdown();

    bool available() const { return mpv != nullptr && mpv->isAvailable(); }

    std::string initError() const;

    Mpv *backend() { return mpv; }

    // `format` is shown in the info overlay ("TS", "HLS", "mkv"...). URL is never logged unredacted.
    void open(const std::string &url, const std::string &format);

    void stop();

    void setPaused(bool paused);

    void seekRelative(double seconds);

    // call every frame: drains mpv events, polls stats (1 Hz), watchdog
    void update(double now);

    // called by the video texture owner when frames are rendered
    void setFramesRendered(unsigned long frames);

    PlaybackState state() const { return st; }

    PlaybackError error() const { return err; }

    const std::string &errorMessage() const { return errMsg; }

    const std::string &errorDetail() const { return errDetail; }

    const StreamInfo &info() const { return si; }

    bool hasVideoFrame() const { return firstFrameAt >= 0; }

    bool hasAudio() const { return !si.audioOutput.empty(); }

    double openSeconds(double now) const { return openedAt < 0 ? 0 : now - openedAt; }

    // true when the run failed before any frame was shown (format fallback is possible)
    bool failedBeforeFirstFrame() const { return st == PlaybackState::Error && firstFrameAt < 0; }

    // user-facing text for a state
    static const char *stateText(PlaybackState s);

private:

    void onLog(const mpv_event_log_message *msg);

    void onProperty(const mpv_event_property *prop);

    void onEndFile(const mpv_event_end_file *ef);

    void fail(PlaybackError e, const std::string &message, const std::string &detail);

    void pollStats();

    Mpv *mpv = nullptr;
    PlaybackState st = PlaybackState::Idle;
    PlaybackError err = PlaybackError::None;
    std::string errMsg;
    std::string errDetail;
    StreamInfo si;
    int64_t entryId = -1;
    bool stopRequested = false;
    double now = 0;
    double openedAt = -1;
    double firstFrameAt = -1;
    double lastStats = 0;
    double lastProgress = -1;
    double lastPos = -1;
    bool pausedForCache = false;
    std::string lastShader;
    std::string firstNetworkError;
};

#endif // PS4IPTV_PLAYER_PLAYBACK_H
