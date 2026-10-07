// PS4 IPTV playback test application.
//
// A minimal libcross2d screen around pPlay's playback path (pplay/mpv.*, pplay/video_texture.*):
// it runs one source at a time (local file, Xtream TS, Xtream HLS), records everything mpv reports,
// and keeps the app usable whatever the stream does (watchdog messages, Circle always works).

#ifndef PS4IPTV_TEST_APP_H
#define PS4IPTV_TEST_APP_H

#include <string>
#include <vector>

#include "cross2d/c2d.h"
#include "ffmpeg_info.h"
#include "net_check.h"
#include "pplay/mpv.h"
#include "pplay/video_texture.h"
#include "test_env.h"

#define APP_DATA_DIR "/data/PS4IPTVTest/"

// Only re-layouts a Text when its content changed.
inline void setTextCached(c2d::Text *text, std::string &cache, const std::string &value) {
    if (cache != value) {
        cache = value;
        text->setString(value);
    }
}

enum class Source {
    Local = 0,
    Ts = 1,
    Hls = 2
};

enum class Screen {
    Menu,
    Playback,
    Diagnostics
};

enum class PlayState {
    Idle,
    Opening,     // loadfile sent, file not loaded yet
    Buffering,   // loaded, waiting for the first frame or for the cache
    Playing,
    Stopping,    // stop sent, waiting for MPV_EVENT_END_FILE
    Ended,       // end of file / stopped, result shown
    Failed       // loadfile or playback error, result shown
};

// Everything observed for one test run. Times are seconds since the load request, -1 = not reached.
struct RunInfo {
    Source source = Source::Local;
    int attempt = 0;
    std::string target;             // sanitized URL / path
    double startUptime = 0;
    int64_t entryId = -1;

    double tStartFile = -1;
    double tLoaded = -1;
    double tVideoReconfig = -1;
    double tFirstFrame = -1;
    double tAudioReconfig = -1;
    double tAudioOut = -1;
    double tStopRequested = -1;
    double tEnded = -1;

    std::string fileFormat;
    std::string videoCodec;
    std::string pixelFormat;
    int width = 0;
    int height = 0;
    double containerFps = 0;
    double estimatedFps = 0;
    std::string audioCodec;
    int sampleRate = 0;
    int channels = 0;
    std::string audioOut;           // audio-out-params summary
    std::string currentAo;
    std::string hwdec;
    bool hasVideoTrack = false;
    bool hasAudioTrack = false;

    double timePos = -1;
    double lastTimePos = -1;
    double lastProgressUptime = -1;
    double audioPts = -1;
    bool audioPtsUnavailable = false;
    double cacheDuration = -1;
    double avsync = 0;
    int64_t frameDrops = 0;
    int64_t decoderDrops = 0;
    bool pausedForCache = false;
    double pausedForCacheSince = -1;

    std::string endReason;
    int endError = 0;
    std::vector<std::string> errors;     // visible, sanitized
    std::vector<std::string> warnings;   // last mpv warnings, sanitized
    std::vector<std::string> watchdogFired;
    int ffmpegVideoErrors = 0;           // error-level lines from libavcodec/libavformat (counted)
    int ffmpegAudioErrors = 0;
    int ffmpegOtherErrors = 0;
    std::string lastFfmpegError;
    std::string lastDemuxerError;        // last ffmpeg/demuxer error line
    int hlsSegmentFailures = 0;          // "Failed to open segment" / "Failed to reload playlist"
    int decodeFrameErrors = 0;           // [vd] "Error while decoding frame" warnings
    bool networkErrorShown = false;
    std::string watchdog;                // current watchdog message
    bool shaderMissing = false;
};

class App : public c2d::C2DRenderer {

public:

    explicit App(const c2d::Vector2f &size);

    ~App() override;

    bool isRunning() const { return running; }

private:

    void onUpdate() override;

    // ui
    void buildUi();

    void showScreen(Screen screen);

    void updateMenuText();

    void updateDiagnosticsText();

    void updateOverlayText();

    void handleInput();

    // environment
    void refreshEnvironment();

    void copyLogToUsb();

    void startDnsCheck();

    std::string configOrigin() const;

    std::vector<std::string> dnsLines() const;

    std::vector<std::string> environmentLines(bool detailed);

    // playback
    void startTest(Source source);

    void requestStop();

    void finishRun(const std::string &reason);

    void returnToMenu();

    void processMpvEvents();

    void onLogMessage(const mpv_event_log_message *msg);

    void onPropertyChange(const mpv_event_property *prop);

    void onFileLoaded();

    void onEndFile(const mpv_event_end_file *ef);

    void pollPlaybackStats();

    void runWatchdog();

    void addError(const std::string &error);

    void setState(PlayState state);

    double runTime() const;

    std::string resultSummary() const;

    static const char *sourceName(Source source);

    static const char *stateName(PlayState state);

    // state
    bool running = true;
    Screen screen = Screen::Menu;
    PlayState state = PlayState::Idle;
    int menuIndex = 0;
    unsigned int prevKeys = 0;
    bool overlayVisible = true;
    bool pendingReturnToMenu = false;
    double lastStatsPoll = 0;
    double lastStatsLog = 0;
    double lastOverlayUpdate = 0;
    int attempts[3] = {0, 0, 0};
    bool idleSeen = false;          // MPV_EVENT_IDLE received since the last load
    bool stopByUser = false;
    double endedUptime = 0;

    RunInfo run;
    std::vector<std::string> results;
    std::string menuMessage;
    std::string logCopyStatus;
    std::string romfsPath;              // PS4Io::getRomFsPath(): "/app0/"
    DnsCheck dns;
    double lastStreamClosedUptime = -1;
    double lastMenuRefresh = 0;
    std::string lastShaderInfo;         // last "compile_attach_shader: type: X, sha: Y" from mpv
    std::string glInfo;
    bool platformOk = false;

    TestEnv env;
    FfmpegInfo ffmpeg;
    std::string mpvVersion;
    std::string ffmpegVersionFromMpv;

    // playback (pPlay)
    Mpv *mpv = nullptr;
    VideoTexture *videoTexture = nullptr;

    // ui objects (owned by the renderer through add())
    c2d::RectangleShape *menuLayer = nullptr;
    c2d::Text *titleText = nullptr;
    c2d::Text *subtitleText = nullptr;
    std::vector<c2d::Text *> menuItems;
    c2d::RectangleShape *menuHighlight = nullptr;
    c2d::Text *envText = nullptr;
    c2d::Text *resultsText = nullptr;
    c2d::Text *messageText = nullptr;
    c2d::Text *cooldownText = nullptr;
    c2d::Text *hintText = nullptr;

    c2d::RectangleShape *diagLayer = nullptr;
    c2d::Text *diagText = nullptr;

    c2d::RectangleShape *overlayLayer = nullptr;
    c2d::RectangleShape *overlayBg = nullptr;
    c2d::Text *overlayText = nullptr;
    c2d::Text *overlayErrorText = nullptr;
    c2d::Text *overlayWatchdogText = nullptr;

    // cached strings to avoid re-laying out unchanged text
    std::string cacheEnv, cacheResults, cacheMessage, cacheCooldown, cacheDiag, cacheOverlay, cacheOverlayErr, cacheOverlayWd;
};

#endif // PS4IPTV_TEST_APP_H
