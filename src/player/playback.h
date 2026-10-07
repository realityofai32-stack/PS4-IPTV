// Thin adapter around the hardware-proven pPlay playback backend (player/pplay/mpv.*): same mpv options,
// same lifecycle (Mpv::load with "pause=yes,speed=1", resume on MPV_EVENT_FILE_LOADED, Mpv::stop), same
// render path (VideoTexture). Adds state tracking, stream information and user-facing errors.

#ifndef PS4IPTV_PLAYER_PLAYBACK_H
#define PS4IPTV_PLAYER_PLAYBACK_H

#include <string>
#include <vector>

#include <functional>

#include "stability.h"
#include "tracks.h"
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
    HttpClient,       // 401 / 404: retrying does not help
    HttpOther,        // other HTTP errors (5xx...)
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
    long long cacheSpeed = 0;  // bytes/s arriving from the network (mpv cache-speed)
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

    // Sets mpv options (stability preset) on the handle; each result is logged. Call before open().
    void applyOptions(const stability::Options &options);

    struct OpenOptions {
        // seconds (VOD resume), passed unchanged (millisecond precision) as mpv's per-file "start" option.
        // mpv 0.34.1 turns it into an absolute seek (loadfile.c: queue_seek(MPSEEK_ABSOLUTE, start,
        // MPSEEK_DEFAULT)); with hr-seek at its default an absolute seek is precise (playloop.c: demuxer seek
        // to the keyframe before the target, frames before it decoded and dropped), so playback starts at
        // the requested timestamp, not at the previous keyframe.
        double start = 0;
        // Movies / episodes: called once the file is loaded (before playback starts) with mpv's tracks;
        // returns the audio track id (-1 = mpv's choice) and the subtitle track id (0 = none) to use.
        // Without a chooser (Live TV) subtitles stay off, exactly as pPlay does.
        std::function<std::pair<int, int>(const std::vector<tracks::Track> &)> chooseTracks;
    };

    // `format` is shown in the info overlay ("TS", "HLS", "mkv"...). URL is never logged unredacted.
    void open(const std::string &url, const std::string &format, const OpenOptions &options);

    void open(const std::string &url, const std::string &format);   // Live TV: no start, no subtitles

    void stop();

    void setPaused(bool paused);

    void seekRelative(double seconds);

    // keyframe seek to an absolute position (fast with software decoding)
    void seekTo(double seconds);

    // tracks of the current file (empty until it is loaded)
    const std::vector<tracks::Track> &trackList() const { return trackItems; }

    // runtime switching through mpv's aid / sid properties (no reload)
    void selectAudio(int id);

    void selectSubtitle(int id);   // 0 = off

    // set when a subtitle could not be drawn (missing precompiled shader): subtitles were turned off
    bool subtitlesDisabledByRenderer() const { return subtitleRenderFailed; }

    double position() const { return si.position; }

    // mpv's time-pos right now (not the last 0.25 s sample); -1 when no file is playing
    double queryPosition();

    double duration() const { return si.duration; }

    // call every frame: drains mpv events, polls stats (1 Hz), watchdog
    void update(double now);

    // called by the video texture owner when frames are rendered
    void setFramesRendered(unsigned long frames);

    PlaybackState state() const { return st; }

    PlaybackError error() const { return err; }

    const std::string &errorMessage() const { return errMsg; }

    const std::string &errorDetail() const { return errDetail; }

    const StreamInfo &info() const { return si; }

    // the first video frame was shown (audio-only streams: audio has been playing for half a second)
    bool started() const { return firstFrameAt >= 0; }

    bool hasAudio() const { return !si.audioOutput.empty(); }

    double openSeconds(double now) const { return openedAt < 0 ? 0 : now - openedAt; }

    // true when the run failed before any frame was shown (format fallback is possible)
    bool failedBeforeFirstFrame() const { return st == PlaybackState::Error && firstFrameAt < 0; }

    // seconds since playback last advanced (time-pos moved or a new video frame was rendered)
    double sinceProgress(double now) const { return lastProgress < 0 ? 0 : now - lastProgress; }

    bool pausedForCache() const { return cachePaused; }

    // how the recovery logic should treat the current error
    stability::FailKind failKind() const;

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
    unsigned long lastFrames = 0;
    bool cachePaused = false;
    std::string lastShader;
    std::string firstNetworkError;
    std::vector<tracks::Track> trackItems;
    std::function<std::pair<int, int>(const std::vector<tracks::Track> &)> chooser;
    bool subtitleRenderFailed = false;

    void readTracks();

    void markSelected(tracks::Kind kind, int id);
};

#endif // PS4IPTV_PLAYER_PLAYBACK_H
