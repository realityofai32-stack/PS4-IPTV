// Playback side of the test app: drives pPlay's Mpv exactly like pPlay's Player does
// (Player::load -> Mpv::load(path, Replace, "pause=yes,speed=1"), resume on MPV_EVENT_FILE_LOADED,
// Mpv::stop on user stop) and records everything mpv reports.

#include <algorithm>
#include <cstring>

#include "app.h"
#include "diag_log.h"
#include "redact.h"

using namespace c2d;

namespace {

    const double WD_OPEN_TIMEOUT = 20;
    const double WD_FIRST_FRAME_TIMEOUT = 15;
    const double WD_BUFFERING_TIMEOUT = 15;
    const double WD_STALL_TIMEOUT = 10;
    const double WD_STOP_TIMEOUT = 8;
    const double RETURN_AFTER_END_TIMEOUT = 3;
    const int MAX_EVENTS_PER_FRAME = 500;
    const size_t MAX_VISIBLE_ERRORS = 6;
    const size_t MAX_RESULTS = 12;

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char) std::tolower(c); });
        return s;
    }

    bool startsWith(const std::string &s, const char *prefix) {
        return s.compare(0, strlen(prefix), prefix) == 0;
    }

    std::string trimNewline(std::string s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) {
            s.pop_back();
        }
        return s;
    }

    std::string nodeString(const mpv_node *n) {
        if (n == nullptr) {
            return "(unavailable)";
        }
        switch (n->format) {
            case MPV_FORMAT_STRING:
                return n->u.string ? n->u.string : "";
            case MPV_FORMAT_FLAG:
                return n->u.flag ? "yes" : "no";
            case MPV_FORMAT_INT64:
                return std::to_string(n->u.int64);
            case MPV_FORMAT_DOUBLE:
                return diag::format("%.3f", n->u.double_);
            case MPV_FORMAT_NONE:
                return "(unavailable)";
            case MPV_FORMAT_NODE_MAP: {
                std::string s = "{";
                for (int i = 0; i < n->u.list->num; i++) {
                    s += std::string(i ? ", " : "") + n->u.list->keys[i] + "=" + nodeString(&n->u.list->values[i]);
                }
                return s + "}";
            }
            case MPV_FORMAT_NODE_ARRAY: {
                std::string s = "[";
                for (int i = 0; i < n->u.list->num; i++) {
                    s += (i ? ", " : "") + nodeString(&n->u.list->values[i]);
                }
                return s + "]";
            }
            default:
                return "(format " + std::to_string((int) n->format) + ")";
        }
    }

    const mpv_node *mapGet(const mpv_node *n, const char *key) {
        if (n == nullptr || n->format != MPV_FORMAT_NODE_MAP) {
            return nullptr;
        }
        for (int i = 0; i < n->u.list->num; i++) {
            if (strcmp(n->u.list->keys[i], key) == 0) {
                return &n->u.list->values[i];
            }
        }
        return nullptr;
    }

    int64_t mapInt(const mpv_node *n, const char *key) {
        const mpv_node *v = mapGet(n, key);
        if (v && v->format == MPV_FORMAT_INT64) {
            return v->u.int64;
        }
        if (v && v->format == MPV_FORMAT_DOUBLE) {
            return (int64_t) v->u.double_;
        }
        return 0;
    }

    double mapDouble(const mpv_node *n, const char *key) {
        const mpv_node *v = mapGet(n, key);
        if (v && v->format == MPV_FORMAT_DOUBLE) {
            return v->u.double_;
        }
        if (v && v->format == MPV_FORMAT_INT64) {
            return (double) v->u.int64;
        }
        return 0;
    }

    std::string mapStr(const mpv_node *n, const char *key) {
        const mpv_node *v = mapGet(n, key);
        return v && v->format == MPV_FORMAT_STRING && v->u.string ? v->u.string : "";
    }

    bool mapFlag(const mpv_node *n, const char *key) {
        const mpv_node *v = mapGet(n, key);
        return v && v->format == MPV_FORMAT_FLAG && v->u.flag;
    }

    std::string shortCodec(const std::string &codec) {
        size_t p = codec.find(" (");
        return p == std::string::npos ? codec : codec.substr(0, p);
    }

    std::string secs(double t) {
        return t < 0 ? "-" : diag::format("%.2fs", t);
    }

    const char *endReasonName(int reason) {
        switch (reason) {
            case MPV_END_FILE_REASON_EOF:
                return "EOF";
            case MPV_END_FILE_REASON_STOP:
                return "STOP";
            case MPV_END_FILE_REASON_QUIT:
                return "QUIT";
            case MPV_END_FILE_REASON_ERROR:
                return "ERROR";
            case MPV_END_FILE_REASON_REDIRECT:
                return "REDIRECT";
            default:
                return "UNKNOWN";
        }
    }

    bool contains(const std::string &haystack, const char *needle) {
        return haystack.find(needle) != std::string::npos;
    }

    // Labels an mpv/FFmpeg log line that indicates a failure. Message texts and levels were checked
    // against the sources of the exact versions linked (mpv 0.34.1, FFmpeg 5.0):
    //   ffmpeg (URLContext): tcp.c "Failed to resolve hostname %s: %s" (error),
    //                        network.c "Connection to %s failed: %s" (error), http.c "HTTP error %d %s" (WARNING)
    //   stream:              stream.c "Failed to open %s." (error)
    //   cplayer:             loadfile.c "Failed to recognize file format." (error),
    //                        audio.c "Could not open/initialize audio device -> no sound." (error),
    //                        video.c "Could not initialize video chain." (fatal)
    //   ao/*:                ao.c "Failed to initialize audio driver '%s'", ao_sdl.c "could not open audio: %s"
    //   vd:                  vd_lavc.c "Could not open codec." (error)
    // Returns "" when the line is not a failure.
    std::string classify(const std::string &prefix, const std::string &text, bool isError) {
        const std::string p = lower(prefix);
        if (p == "ffmpeg") {
            if (contains(text, "HTTP error")) {
                std::string label = "NETWORK OPEN FAILED: " + text;
                if (contains(text, "HTTP error 403")) {
                    label += "  (provider refused: if the previous stream closed <60 s ago, the account's "
                             "connection slot may still be busy - wait and retry)";
                }
                return label;
            }
            if (contains(text, "Failed to resolve hostname")) {
                return "NETWORK OPEN FAILED (DNS): " + text;
            }
            if (contains(text, "Connection to") || contains(text, "Connection refused")
                || contains(text, "timed out") || contains(text, "Protocol not found")) {
                return "NETWORK OPEN FAILED: " + text;
            }
            return isError ? "NETWORK/IO ERROR: [ffmpeg] " + text : "";
        }
        if (startsWith(p, "stream")) {
            return isError ? "NETWORK OPEN FAILED: [" + prefix + "] " + text : "";
        }
        if (contains(text, "Failed to recognize file format")) {
            return "DEMUX FAILED: " + text;
        }
        if (startsWith(p, "lavf") || startsWith(p, "demux")) {
            return isError ? "DEMUX FAILED: [" + prefix + "] " + text : "";
        }
        if (startsWith(p, "ao") || contains(text, "audio device") || contains(text, "audio driver")) {
            return isError ? "AUDIO INIT FAILED: [" + prefix + "] " + text : "";
        }
        if (startsWith(p, "vd") || contains(text, "video chain")) {
            return isError ? "VIDEO DECODER FAILED: [" + prefix + "] " + text : "";
        }
        if (startsWith(p, "ad")) {
            return isError ? "AUDIO DECODER FAILED: [" + prefix + "] " + text : "";
        }
        if (startsWith(p, "vo") || startsWith(p, "libmpv") || startsWith(p, "ra") || contains(p, "opengl")
            || contains(text, "shader")) {
            return isError ? "RENDERER FAILED: [" + prefix + "] " + text : "";
        }
        return isError ? "MPV ERROR: [" + prefix + "] " + text : "";
    }
}

const char *App::sourceName(Source source) {
    switch (source) {
        case Source::Local:
            return "LOCAL";
        case Source::Ts:
            return "TS";
        default:
            return "HLS";
    }
}

const char *App::stateName(PlayState s) {
    switch (s) {
        case PlayState::Idle:
            return "IDLE";
        case PlayState::Opening:
            return "OPENING...";
        case PlayState::Buffering:
            return "BUFFERING...";
        case PlayState::Playing:
            return "PLAYING";
        case PlayState::Stopping:
            return "STOPPING...";
        case PlayState::Ended:
            return "ENDED";
        default:
            return "ERROR";
    }
}

double App::runTime() const {
    return diag::uptime() - run.startUptime;
}

void App::setState(PlayState s) {
    if (s != state) {
        LOG_I("state", "%s -> %s (t=%.2fs)", stateName(state), stateName(s), state == PlayState::Idle ? 0.0 : runTime());
        state = s;
    }
}

void App::addError(const std::string &error) {
    std::string e = redact::apply(error);
    if (std::find(run.errors.begin(), run.errors.end(), e) != run.errors.end()) {
        return;
    }
    if (run.errors.size() < MAX_VISIBLE_ERRORS) {
        run.errors.push_back(e);
    }
    LOG_E("visible", "%s", e.c_str());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void App::startTest(Source source) {
    if (!mpv->isAvailable()) {
        menuMessage = diag::format("MPV INIT FAILED: %s returned %d (%s)", mpv->getInitErrorStep().c_str(),
                                   mpv->getInitError(), mpv_error_string(mpv->getInitError()));
        LOG_E("test", "%s", menuMessage.c_str());
        updateMenuText();
        return;
    }

    refreshEnvironment();

    std::string target;
    std::string display;
    bool network = source != Source::Local;
    if (source == Source::Local) {
        target = env.localFile;
        display = target;
        if (target.empty()) {
            menuMessage = "LOCAL FILE NOT FOUND: copy test.mp4 to the USB root (see Diagnostics for searched paths)";
        }
    } else {
        target = source == Source::Ts ? env.config.tsUrl : env.config.hlsUrl;
        if (target.empty()) {
            menuMessage = diag::format("%s URL NOT SET: add %s=<url> to test_streams.txt (%s)", sourceName(source),
                                       sourceName(source), env.config.found ? env.config.path.c_str()
                                                                            : "file not found");
        } else {
            redact::UrlInfo u = redact::parseUrl(target);
            if (!u.valid) {
                menuMessage = diag::format("%s URL INVALID: check test_streams.txt", sourceName(source));
                target.clear();
            } else {
                display = u.sanitized;
                LOG_I("network", "%s source: protocol=%s host=%s port=%s extension=.%s", sourceName(source),
                      u.scheme.c_str(), u.host.c_str(), u.port.empty() ? "(default)" : u.port.c_str(),
                      u.extension.c_str());
                if (u.scheme == "https" && !ffmpeg.hasHttps) {
                    LOG_W("network", "https URL but this FFmpeg build has no https protocol: expect "
                                     "'Protocol not found'");
                }
            }
        }
    }
    if (target.empty()) {
        LOG_W("test", "%s", menuMessage.c_str());
        updateMenuText();
        return;
    }

    run = RunInfo();
    run.source = source;
    run.attempt = ++attempts[(int) source];
    run.target = display;
    run.startUptime = diag::uptime();
    idleSeen = false;
    stopByUser = false;
    pendingReturnToMenu = false;
    menuMessage.clear();
    lastStatsPoll = lastStatsLog = 0;
    videoTexture->resetFrameStats();
    overlayVisible = true;

    LOG_I("test", "========== %s TEST #%d ==========", sourceName(source), run.attempt);
    LOG_I("test", "requested source type: %s, target: %s", sourceName(source), display.c_str());

    mpv_handle *h = mpv->getHandle();
    if (network && !env.config.userAgent.empty()) {
        int res = mpv_set_property_string(h, "user-agent", env.config.userAgent.c_str());
        LOG_I("player", "user-agent from test_streams.txt: %d (%s)", res, mpv_error_string(res));
    }
    for (const auto &opt: env.config.mpvOptions) {
        int res = mpv_set_property_string(h, opt.first.c_str(), opt.second.c_str());
        LOG_W("player", "MPV_OPT from test_streams.txt (deviates from pPlay): %s=%s -> %d (%s)",
              opt.first.c_str(), opt.second.c_str(), res, mpv_error_string(res));
    }

    // pPlay Player::load(): disable subtitles if slang is not set
    char *slang = mpv_get_property_string(h, "slang");
    if (slang == nullptr || strlen(slang) == 0) {
        int res = mpv_set_option_string(h, "sid", "no");
        LOG_V("player", "slang not set, sid=no: %d", res);
    }
    mpv_free(slang);

    setState(PlayState::Opening);
    showScreen(Screen::Playback);

    // pPlay Player::load(): mpv->load(path, Mpv::LoadType::Replace, "pause=yes,speed=1")
    int res = mpv->load(target, Mpv::LoadType::Replace, "pause=yes,speed=1");
    LOG_I("player", "loadfile returned %d (%s)", res, mpv_error_string(res));
    if (res != 0) {
        run.endError = res;
        addError(diag::format("LOADFILE FAILED: mpv error %d (%s)", res, mpv_error_string(res)));
        finishRun("loadfile command failed");
        setState(PlayState::Failed);
    }
}

void App::requestStop() {
    stopByUser = true;
    run.tStopRequested = runTime();
    LOG_I("test", "stop requested (Circle) at t=%.2fs in state %s", run.tStopRequested, stateName(state));
    setState(PlayState::Stopping);
    int res = mpv->stop();  // pPlay: write-watch-later-config + stop
    if (res < 0) {
        addError(diag::format("STOP FAILED: mpv error %d (%s)", res, mpv_error_string(res)));
    }
}

void App::finishRun(const std::string &reason) {
    if (run.tEnded >= 0) {
        return;
    }
    run.endReason = reason;
    run.tEnded = runTime();
    if (run.source != Source::Local && run.tStartFile >= 0) {
        lastStreamClosedUptime = diag::uptime();
    }
    std::string summary = resultSummary();
    results.push_back(summary);
    while (results.size() > MAX_RESULTS) {
        results.erase(results.begin());
    }
    LOG_I("RESULT", "%s", summary.c_str());
    for (const auto &e: run.errors) {
        LOG_I("RESULT", "  error: %s", e.c_str());
    }
    for (const auto &w: run.watchdogFired) {
        LOG_I("RESULT", "  watchdog: %s", w.c_str());
    }
    LOG_I("RESULT", "  hls segment/playlist failures %d, [vd] frame decode errors %d", run.hlsSegmentFailures,
          run.decodeFrameErrors);
    LOG_I("RESULT", "  ffmpeg error lines: video %d, audio %d, other %d%s%s", run.ffmpegVideoErrors,
          run.ffmpegAudioErrors, run.ffmpegOtherErrors, run.lastFfmpegError.empty() ? "" : ", last: ",
          run.lastFfmpegError.c_str());
}

void App::returnToMenu() {
    if (mpv->isAvailable()) {
        char *idle = mpv_get_property_string(mpv->getHandle(), "idle-active");
        char *ao = mpv_get_property_string(mpv->getHandle(), "current-ao");
        LOG_I("cleanup", "after %s test #%d: idle-active=%s, current-ao=%s, frames rendered=%lu",
              sourceName(run.source), run.attempt, idle ? idle : "(unavailable)",
              ao ? ao : "(none: audio output closed)", videoTexture->getFramesRendered());
        mpv_free(idle);
        mpv_free(ao);
    }
    if (!run.errors.empty()) {
        menuMessage = std::string(sourceName(run.source)) + ": " + run.errors.front();
    }
    pendingReturnToMenu = false;
    videoTexture->resetFrameStats();
    setState(PlayState::Idle);
    copyLogToUsb();
    showScreen(Screen::Menu);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// mpv events

void App::processMpvEvents() {
    if (!mpv->isAvailable()) {
        return;
    }
    for (int i = 0; i < MAX_EVENTS_PER_FRAME; i++) {
        mpv_event *ev = mpv->getEvent();
        if (ev == nullptr || ev->event_id == MPV_EVENT_NONE) {
            break;
        }
        if (ev->error < 0) {
            LOG_W("mpv-event", "%s: error %d (%s)", mpv_event_name(ev->event_id), ev->error,
                  mpv_error_string(ev->error));
        }
        switch (ev->event_id) {
            case MPV_EVENT_LOG_MESSAGE:
                onLogMessage((mpv_event_log_message *) ev->data);
                break;
            case MPV_EVENT_PROPERTY_CHANGE:
                onPropertyChange((mpv_event_property *) ev->data);
                break;
            case MPV_EVENT_START_FILE: {
                auto *sf = (mpv_event_start_file *) ev->data;
                LOG_I("mpv-event", "START_FILE (playlist entry %lld)", (long long) sf->playlist_entry_id);
                if (state != PlayState::Idle && run.entryId < 0) {
                    run.entryId = sf->playlist_entry_id;
                    if (run.tStartFile < 0) {
                        run.tStartFile = runTime();
                    }
                }
                break;
            }
            case MPV_EVENT_FILE_LOADED:
                onFileLoaded();
                break;
            case MPV_EVENT_END_FILE:
                onEndFile((mpv_event_end_file *) ev->data);
                break;
            case MPV_EVENT_VIDEO_RECONFIG: {
                mpv_node node;
                if (mpv_get_property(mpv->getHandle(), "video-params", MPV_FORMAT_NODE, &node) >= 0) {
                    LOG_I("video", "VIDEO_RECONFIG: video-params %s", nodeString(&node).c_str());
                    mpv_free_node_contents(&node);
                } else {
                    LOG_I("video", "VIDEO_RECONFIG: video-params unavailable");
                }
                if (state != PlayState::Idle && run.tVideoReconfig < 0) {
                    run.tVideoReconfig = runTime();
                }
                break;
            }
            case MPV_EVENT_AUDIO_RECONFIG: {
                mpv_node node;
                if (mpv_get_property(mpv->getHandle(), "audio-params", MPV_FORMAT_NODE, &node) >= 0) {
                    LOG_I("audio", "AUDIO_RECONFIG: audio-params %s", nodeString(&node).c_str());
                    mpv_free_node_contents(&node);
                } else {
                    LOG_I("audio", "AUDIO_RECONFIG: audio-params unavailable");
                }
                if (state != PlayState::Idle && run.tAudioReconfig < 0) {
                    run.tAudioReconfig = runTime();
                }
                break;
            }
            case MPV_EVENT_PLAYBACK_RESTART:
                LOG_I("mpv-event", "PLAYBACK_RESTART (playback started/resumed after load or seek) t=%.2fs",
                      runTime());
                break;
            case MPV_EVENT_IDLE:
                LOG_I("mpv-event", "IDLE");
                idleSeen = true;
                break;
            case MPV_EVENT_SHUTDOWN:
                LOG_E("mpv-event", "SHUTDOWN (mpv core terminated)");
                break;
            default:
                LOG_V("mpv-event", "%s", mpv_event_name(ev->event_id));
                break;
        }
    }
}

void App::onLogMessage(const mpv_event_log_message *msg) {
    diag::Level level;
    if (msg->log_level <= MPV_LOG_LEVEL_ERROR) {
        level = diag::Level::Error;
    } else if (msg->log_level == MPV_LOG_LEVEL_WARN) {
        level = diag::Level::Warn;
    } else if (msg->log_level == MPV_LOG_LEVEL_INFO) {
        level = diag::Level::Info;
    } else {
        level = diag::Level::Verbose;
    }
    std::string prefix = msg->prefix ? msg->prefix : "?";
    std::string text = trimNewline(msg->text ? msg->text : "");
    diag::write(level, "mpv/" + prefix, text);

    // the PS4 mpv patch logs "compile_attach_shader: type: <t>, sha: <hash>" before the lookup
    if (text.find("compile_attach_shader: type:") != std::string::npos) {
        lastShaderInfo = text.substr(text.find("type:"));
    }

    bool active = state == PlayState::Opening || state == PlayState::Buffering || state == PlayState::Playing
                  || state == PlayState::Stopping;
    if (!active) {
        return;
    }
    if (!run.shaderMissing && text.find("precompiled shader not found") != std::string::npos) {
        run.shaderMissing = true;
        addError(diag::format("RENDERER SHADER MISSING: %s is not in pPlay's precompiled shader table "
                              "(video %s %dx%d) - mpv cannot compile shaders at runtime on retail firmware",
                              lastShaderInfo.empty() ? "a shader" : lastShaderInfo.c_str(),
                              run.pixelFormat.empty() ? "?" : run.pixelFormat.c_str(), run.width, run.height));
        return;
    }

    const bool isError = level == diag::Level::Error;
    if (prefix == "ffmpeg/demuxer" && (text.find("Failed to open segment") != std::string::npos
                                       || text.find("Failed to reload playlist") != std::string::npos)) {
        if (++run.hlsSegmentFailures == 1) {
            addError("HLS SEGMENT/PLAYLIST OPEN FAILED: " + text + " (see log for the HTTP status)");
        }
        return;
    }
    if (startsWith(prefix, "ffmpeg/")) {
        // error lines from libavcodec/libavformat ("non-existing PPS", "no frame!", "PES packet size
        // mismatch", ...) are routine when joining a live MPEG-TS mid-stream: count them, they are
        // reported as the cause only if the run fails
        if (isError) {
            int &count = startsWith(prefix, "ffmpeg/video") ? run.ffmpegVideoErrors
                         : startsWith(prefix, "ffmpeg/audio") ? run.ffmpegAudioErrors : run.ffmpegOtherErrors;
            count++;
            run.lastFfmpegError = redact::apply("[" + prefix + "] " + text);
            if (prefix == "ffmpeg/demuxer") {
                run.lastDemuxerError = run.lastFfmpegError;
            }
        }
        return;
    }
    if (prefix == "vd" && text.find("Error while decoding frame") != std::string::npos) {
        run.decodeFrameErrors++;
        return;
    }

    std::string label = classify(prefix, text, isError);
    if (!label.empty()) {
        addError(label);
        if (startsWith(label, "NETWORK")) {
            run.networkErrorShown = true;
        }
    } else if (level == diag::Level::Warn) {
        run.warnings.push_back(redact::apply("[" + prefix + "] " + text));
        if (run.warnings.size() > 3) {
            run.warnings.erase(run.warnings.begin());
        }
    }
}

void App::onPropertyChange(const mpv_event_property *prop) {
    const std::string name = prop->name;
    const mpv_node *node = prop->format == MPV_FORMAT_NODE ? (const mpv_node *) prop->data : nullptr;
    LOG_I("prop", "%s = %s", name.c_str(), nodeString(node).c_str());

    if (name == "idle-active" && node && node->format == MPV_FORMAT_FLAG && node->u.flag) {
        idleSeen = true;  // MPV_EVENT_IDLE is deprecated in this API version; the property is authoritative
    }
    if (state == PlayState::Idle) {
        return;
    }
    if (name == "file-format") {
        run.fileFormat = node ? nodeString(node) : "";
    } else if (name == "video-codec") {
        run.videoCodec = node ? shortCodec(nodeString(node)) : "";
    } else if (name == "video-params" && node) {
        run.width = (int) mapInt(node, "w");
        run.height = (int) mapInt(node, "h");
        run.pixelFormat = mapStr(node, "pixelformat");
    } else if (name == "container-fps" && node) {
        run.containerFps = node->format == MPV_FORMAT_DOUBLE ? node->u.double_ : 0;
    } else if (name == "audio-codec-name") {
        run.audioCodec = node ? nodeString(node) : "";
    } else if (name == "audio-params" && node) {
        run.sampleRate = (int) mapInt(node, "samplerate");
        run.channels = (int) mapInt(node, "channel-count");
    } else if (name == "audio-out-params" && node) {
        run.audioOut = diag::format("%lldHz %s %s", (long long) mapInt(node, "samplerate"),
                                    mapStr(node, "channels").c_str(), mapStr(node, "format").c_str());
    } else if (name == "current-ao") {
        run.currentAo = node && node->format == MPV_FORMAT_STRING ? node->u.string : "";
        if (!run.currentAo.empty()) {
            LOG_I("audio", "audio device initialized: ao=%s (SDL2 audio -> PS4 sceAudioOut)", run.currentAo.c_str());
        }
    } else if (name == "hwdec-current") {
        run.hwdec = node ? nodeString(node) : "";
    } else if (name == "paused-for-cache") {
        bool paused = node && node->format == MPV_FORMAT_FLAG && node->u.flag;
        if (paused && !run.pausedForCache) {
            run.pausedForCacheSince = diag::uptime();
        }
        run.pausedForCache = paused;
        if (paused && state == PlayState::Playing) {
            setState(PlayState::Buffering);
        }
    }
}

void App::onFileLoaded() {
    if (state == PlayState::Idle) {
        LOG_I("mpv-event", "FILE_LOADED (no active test)");
        return;
    }
    run.tLoaded = runTime();
    LOG_I("mpv-event", "FILE_LOADED after %.2fs", run.tLoaded);

    mpv_node node;
    if (mpv_get_property(mpv->getHandle(), "track-list", MPV_FORMAT_NODE, &node) >= 0) {
        if (node.format == MPV_FORMAT_NODE_ARRAY) {
            LOG_I("tracks", "%d track(s)", node.u.list->num);
            for (int i = 0; i < node.u.list->num; i++) {
                const mpv_node *t = &node.u.list->values[i];
                std::string type = mapStr(t, "type");
                bool selected = mapFlag(t, "selected");
                LOG_I("tracks", "  #%lld %s codec=%s lang=%s selected=%s %s", (long long) mapInt(t, "id"),
                      type.c_str(), mapStr(t, "codec").c_str(), mapStr(t, "lang").c_str(), selected ? "yes" : "no",
                      type == "video" ? diag::format("%lldx%lld fps=%.3f", (long long) mapInt(t, "demux-w"),
                                                     (long long) mapInt(t, "demux-h"),
                                                     mapDouble(t, "demux-fps")).c_str()
                                      : type == "audio" ? diag::format("%lldHz %lldch", (long long) mapInt(t, "demux-samplerate"),
                                                                      (long long) mapInt(t, "demux-channel-count")).c_str()
                                                        : "");
                if (selected && type == "video") {
                    run.hasVideoTrack = true;
                } else if (selected && type == "audio") {
                    run.hasAudioTrack = true;
                }
            }
        }
        mpv_free_node_contents(&node);
    } else {
        LOG_W("tracks", "track-list unavailable");
    }
    if (!run.hasVideoTrack) {
        addError("NO VIDEO TRACK selected by mpv");
    }
    if (!run.hasAudioTrack) {
        addError("NO AUDIO TRACK selected by mpv");
    }

    // pPlay Player::onLoadEvent(): resume()
    int res = mpv->resume();
    LOG_I("player", "resume (set pause no): %d (%s)", res, mpv_error_string(res));
    setState(PlayState::Buffering);
}

void App::onEndFile(const mpv_event_end_file *ef) {
    LOG_I("mpv-event", "END_FILE (entry %lld) reason=%s error=%d (%s)", (long long) ef->playlist_entry_id,
          endReasonName(ef->reason), ef->error, mpv_error_string(ef->error));
    if (state == PlayState::Idle) {
        return;
    }
    if (run.entryId < 0) {
        // mpv always sends START_FILE before END_FILE for an entry: this is the previous file
        LOG_I("mpv-event", "END_FILE before this test's START_FILE: previous file, ignored");
        return;
    }
    if (ef->playlist_entry_id != run.entryId) {
        LOG_I("mpv-event", "END_FILE belongs to another playlist entry (current %lld), ignored",
              (long long) run.entryId);
        return;
    }
    run.endError = ef->error;

    if (stopByUser && ef->reason != MPV_END_FILE_REASON_REDIRECT) {
        // a stop during opening may be reported as an aborted load: the user's stop is what happened
        finishRun(ef->reason == MPV_END_FILE_REASON_STOP
                  ? std::string("stopped by user")
                  : diag::format("stopped by user (mpv end reason %s, error %d)", endReasonName(ef->reason),
                                 ef->error));
        pendingReturnToMenu = true;
        endedUptime = diag::uptime();
        return;
    }

    if (ef->reason == MPV_END_FILE_REASON_REDIRECT) {
        // mpv opened the source as a playlist and will start its first entry
        LOG_I("test", "mpv resolved the source as a playlist (REDIRECT): following the next entry");
        run.entryId = -1;
        return;
    }

    if (ef->reason == MPV_END_FILE_REASON_ERROR) {
        const char *label;
        switch (ef->error) {
            case MPV_ERROR_AO_INIT_FAILED:
                label = "AUDIO INIT FAILED";
                break;
            case MPV_ERROR_VO_INIT_FAILED:
                label = "RENDERER (VO) INIT FAILED";
                break;
            case MPV_ERROR_LOADING_FAILED:
                label = run.source == Source::Local ? "OPEN FAILED" : "NETWORK OPEN FAILED";
                break;
            case MPV_ERROR_UNKNOWN_FORMAT:
                label = "UNSUPPORTED FORMAT";
                break;
            case MPV_ERROR_NOTHING_TO_PLAY:
                label = "NOTHING TO PLAY (no decodable audio/video)";
                break;
            default:
                label = "PLAYBACK FAILED";
                break;
        }
        addError(diag::format("%s: mpv error %d (%s)", label, ef->error, mpv_error_string(ef->error)));
        if (run.tFirstFrame < 0 && !run.networkErrorShown && !run.lastDemuxerError.empty()) {
            addError("DEMUX FAILED: last demuxer error: " + run.lastDemuxerError);
        }
        finishRun(diag::format("error %d (%s)", ef->error, mpv_error_string(ef->error)));
        setState(PlayState::Failed);
        return;
    }

    finishRun(ef->reason == MPV_END_FILE_REASON_EOF ? "end of file (EOF)"
                                                    : std::string("ended: ") + endReasonName(ef->reason));
    setState(PlayState::Ended);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// periodic

void App::pollPlaybackStats() {
    mpv_handle *h = mpv->getHandle();
    double up = diag::uptime();
    double d;
    int64_t i;

    if (mpv_get_property(h, "time-pos", MPV_FORMAT_DOUBLE, &d) >= 0) {
        run.timePos = d;
        if (run.lastTimePos < 0 || d > run.lastTimePos + 0.01) {
            run.lastProgressUptime = up;
            run.lastTimePos = d;
        }
    }
    if (mpv_get_property(h, "demuxer-cache-duration", MPV_FORMAT_DOUBLE, &d) >= 0) {
        run.cacheDuration = d;
    }
    if (mpv_get_property(h, "frame-drop-count", MPV_FORMAT_INT64, &i) >= 0) {
        run.frameDrops = i;
    }
    if (mpv_get_property(h, "decoder-frame-drop-count", MPV_FORMAT_INT64, &i) >= 0) {
        run.decoderDrops = i;
    }
    if (mpv_get_property(h, "estimated-vf-fps", MPV_FORMAT_DOUBLE, &d) >= 0) {
        run.estimatedFps = d;
    }
    if (mpv_get_property(h, "avsync", MPV_FORMAT_DOUBLE, &d) >= 0) {
        run.avsync = d;
    }

    // first audio output: the audio clock moves while an audio output is open
    if (!run.audioPtsUnavailable) {
        int res = mpv_get_property(h, "audio-pts", MPV_FORMAT_DOUBLE, &d);
        if (res == MPV_ERROR_PROPERTY_NOT_FOUND) {
            run.audioPtsUnavailable = true;
            LOG_I("audio", "audio-pts property not available in this mpv; audio output is inferred from "
                           "time-pos + current-ao");
        } else if (res >= 0) {
            if (run.tAudioOut < 0 && run.audioPts >= 0 && d > run.audioPts + 0.01 && !run.currentAo.empty()) {
                run.tAudioOut = runTime();
                LOG_I("audio", "first audio output observed at t=%.2fs: audio-pts advancing %.3f -> %.3f on ao=%s",
                      run.tAudioOut, run.audioPts, d, run.currentAo.c_str());
            }
            run.audioPts = d;
        }
    }
    if (run.audioPtsUnavailable && run.tAudioOut < 0 && !run.currentAo.empty() && run.hasAudioTrack
        && run.lastProgressUptime == up && run.lastTimePos > 0) {
        run.tAudioOut = runTime();
        LOG_I("audio", "audio output inferred at t=%.2fs (ao=%s open, time-pos advancing)", run.tAudioOut,
              run.currentAo.c_str());
    }

    if (state == PlayState::Buffering && run.tFirstFrame >= 0 && !run.pausedForCache
        && run.lastProgressUptime == up) {
        setState(PlayState::Playing);
    }

    if (up - lastStatsLog >= 5) {
        lastStatsLog = up;
        LOG_I("stats", "t=%.1f pos=%.2f cache=%.1fs fps(est)=%.2f drops vo=%lld dec=%lld avsync=%.3f "
                       "frames=%lu ao=%s paused-for-cache=%s",
              runTime(), run.timePos, run.cacheDuration, run.estimatedFps, (long long) run.frameDrops,
              (long long) run.decoderDrops, run.avsync, videoTexture->getFramesRendered(),
              run.currentAo.empty() ? "-" : run.currentAo.c_str(), run.pausedForCache ? "yes" : "no");
    }
}

void App::runWatchdog() {
    double t = runTime();
    double up = diag::uptime();
    std::string wd;

    switch (state) {
        case PlayState::Opening:
            if (t > WD_OPEN_TIMEOUT) {
                wd = diag::format("WATCHDOG: stream not opened after %.0fs - mpv is still trying (Circle = stop)", t);
            }
            break;
        case PlayState::Buffering:
        case PlayState::Playing:
            if (run.tLoaded >= 0 && run.tFirstFrame < 0 && t - run.tLoaded > WD_FIRST_FRAME_TIMEOUT) {
                wd = diag::format("WATCHDOG: no video frame %.0fs after open%s", t - run.tLoaded,
                                  run.tAudioOut >= 0 ? " (audio IS playing)" : "");
            } else if (run.pausedForCache && up - run.pausedForCacheSince > WD_BUFFERING_TIMEOUT) {
                wd = diag::format("WATCHDOG: buffering (paused-for-cache) for %.0fs", up - run.pausedForCacheSince);
            } else if (run.tFirstFrame >= 0 && run.lastProgressUptime > 0 && !run.pausedForCache
                       && up - run.lastProgressUptime > WD_STALL_TIMEOUT) {
                wd = diag::format("WATCHDOG: playback position stalled for %.0fs", up - run.lastProgressUptime);
            }
            break;
        case PlayState::Stopping:
            if (t - run.tStopRequested > WD_STOP_TIMEOUT) {
                wd = diag::format("WATCHDOG: mpv has not confirmed stop after %.0fs - press Circle again to force "
                                  "return to the menu", t - run.tStopRequested);
            }
            break;
        default:
            break;
    }

    if (!wd.empty()) {
        std::string key = wd.substr(0, 24);
        bool fired = false;
        for (const auto &f: run.watchdogFired) {
            fired |= f.compare(0, 24, key) == 0;
        }
        if (!fired) {
            run.watchdogFired.push_back(wd);
            LOG_W("watchdog", "%s", wd.c_str());
            if (state == PlayState::Opening) {
                addError(diag::format("OPEN TIMEOUT: no stream data after %.0f s (mpv gives up at network-timeout "
                                      "60 s; Circle = stop)", t));
            } else if (run.tFirstFrame < 0 && run.tLoaded >= 0) {
                addError(diag::format("NO VIDEO FRAME %.0f s after open%s", t - run.tLoaded,
                                      run.tAudioOut >= 0 ? " while audio plays: decoder/renderer problem"
                                                         : ""));
            }
        }
    }
    run.watchdog = wd;
}

void App::onUpdate() {
    C2DRenderer::onUpdate();  // input update + children

    handleInput();
    processMpvEvents();

    double up = diag::uptime();
    bool active = state == PlayState::Opening || state == PlayState::Buffering || state == PlayState::Playing
                  || state == PlayState::Stopping;

    if (active && run.tFirstFrame < 0 && videoTexture->getFramesRendered() > 0) {
        run.tFirstFrame = runTime();
        LOG_I("video", "first video frame rendered at t=%.2fs (%dx%d %s, codec %s)", run.tFirstFrame, run.width,
              run.height, run.pixelFormat.c_str(), run.videoCodec.c_str());
    }

    if (active && mpv->isAvailable()) {
        if (up - lastStatsPoll >= 1.0) {
            lastStatsPoll = up;
            pollPlaybackStats();
        }
        runWatchdog();
    }

    if (pendingReturnToMenu && (idleSeen || up - endedUptime > RETURN_AFTER_END_TIMEOUT)) {
        if (!idleSeen) {
            LOG_W("cleanup", "no MPV_EVENT_IDLE within %.0fs after stop", RETURN_AFTER_END_TIMEOUT);
        }
        returnToMenu();
    }

    if (screen == Screen::Menu && up - lastMenuRefresh >= 1.0) {
        lastMenuRefresh = up;
        updateMenuText();
    }

    if (screen == Screen::Playback && up - lastOverlayUpdate >= 0.25) {
        lastOverlayUpdate = up;
        updateOverlayText();
    } else if (screen == Screen::Diagnostics && up - lastOverlayUpdate >= 1.0) {
        lastOverlayUpdate = up;
        updateDiagnosticsText();
    }
}

void App::updateOverlayText() {
    std::string s = diag::format("[%s #%d]  %s   t=%.1fs        Circle: %s   Triangle: hide overlay\n",
                                 sourceName(run.source), run.attempt, stateName(state),
                                 state == PlayState::Idle ? 0.0 : runTime(),
                                 state == PlayState::Ended || state == PlayState::Failed ? "back to menu" : "stop");
    s += "SOURCE: " + run.target + (run.fileFormat.empty() ? "" : "   container: " + run.fileFormat) + "\n";
    s += "TIMING: opened " + secs(run.tLoaded) + "   first video frame " + secs(run.tFirstFrame)
         + "   first audio out " + secs(run.tAudioOut) + "\n";
    s += diag::format("VIDEO: %s %dx%d %s  fps %.3f (est %.2f)   frames rendered %lu   hwdec %s\n",
                      run.videoCodec.empty() ? "-" : run.videoCodec.c_str(), run.width, run.height,
                      run.pixelFormat.empty() ? "-" : run.pixelFormat.c_str(), run.containerFps, run.estimatedFps,
                      videoTexture->getFramesRendered(), run.hwdec.empty() ? "-" : run.hwdec.c_str());
    s += diag::format("AUDIO: %s %d Hz %d ch   ao=%s   out: %s\n", run.audioCodec.empty() ? "-" : run.audioCodec.c_str(),
                      run.sampleRate, run.channels, run.currentAo.empty() ? "-" : run.currentAo.c_str(),
                      run.audioOut.empty() ? "-" : run.audioOut.c_str());
    s += diag::format("STREAM: pos %.1fs   cache %.1fs   paused-for-cache %s   drops vo %lld / dec %lld   avsync %.3f",
                      run.timePos, run.cacheDuration, run.pausedForCache ? "YES" : "no", (long long) run.frameDrops,
                      (long long) run.decoderDrops, run.avsync);
    if (state == PlayState::Ended || state == PlayState::Failed) {
        s += "\nRESULT: " + (results.empty() ? std::string("-") : results.back());
        s += "\nPress Circle to return to the menu";
    }
    setTextCached(overlayText, cacheOverlay, s);

    std::string wd = run.watchdog;
    for (const auto &w: run.warnings) {
        wd += (wd.empty() ? "" : "\n") + std::string("warning: ") + w.substr(0, 160);
    }
    if (run.hlsSegmentFailures + run.decodeFrameErrors > 0) {
        wd += (wd.empty() ? "" : "\n") + diag::format("HLS segment/playlist failures: %d   frame decode errors: %d",
                                                      run.hlsSegmentFailures, run.decodeFrameErrors);
    }
    if (run.ffmpegVideoErrors + run.ffmpegAudioErrors + run.ffmpegOtherErrors > 0) {
        wd += (wd.empty() ? "" : "\n")
              + diag::format("ffmpeg error lines (normal in small numbers on live TS): video %d, audio %d, other %d",
                             run.ffmpegVideoErrors, run.ffmpegAudioErrors, run.ffmpegOtherErrors)
              + "\n  last: " + run.lastFfmpegError.substr(0, 150);
    }
    setTextCached(overlayWatchdogText, cacheOverlayWd, wd);

    std::string err;
    for (const auto &e: run.errors) {
        err += (err.empty() ? "" : "\n") + e.substr(0, 170);
    }
    setTextCached(overlayErrorText, cacheOverlayErr, err);

    // stack the three texts and size the background to fit
    float y = overlayText->getPosition().y + overlayText->getLocalBounds().height + 16;
    overlayWatchdogText->setPosition(40, y);
    if (!wd.empty()) {
        y += overlayWatchdogText->getLocalBounds().height + 16;
    }
    overlayErrorText->setPosition(40, y);
    if (!err.empty()) {
        y += overlayErrorText->getLocalBounds().height + 16;
    }
    overlayBg->setSize(overlayBg->getSize().x, y - 20 + 8);
}

std::string App::resultSummary() const {
    bool video = run.tFirstFrame >= 0;
    bool audio = run.tAudioOut >= 0;
    std::string verdict;
    if (video && audio) {
        verdict = "VIDEO+AUDIO OK";
    } else if (video && !run.hasAudioTrack) {
        verdict = "VIDEO OK, NO AUDIO TRACK";
    } else if (video) {
        verdict = "VIDEO ONLY - NO AUDIO OUTPUT";
    } else if (audio) {
        verdict = "AUDIO ONLY - NO VIDEO FRAME";
    } else {
        verdict = "FAILED";
    }
    return diag::format("%s #%d %s | open %s video %s audio %s played %.1fs | %s %dx%d %s %.2ffps | %s %dHz %dch ao=%s "
                        "| drops %lld/%lld | end: %s",
                        sourceName(run.source), run.attempt, verdict.c_str(), secs(run.tLoaded).c_str(),
                        secs(run.tFirstFrame).c_str(), secs(run.tAudioOut).c_str(), run.timePos < 0 ? 0 : run.timePos,
                        run.videoCodec.empty() ? "-" : run.videoCodec.c_str(), run.width, run.height,
                        run.pixelFormat.empty() ? "-" : run.pixelFormat.c_str(),
                        run.containerFps > 0 ? run.containerFps : run.estimatedFps,
                        run.audioCodec.empty() ? "-" : run.audioCodec.c_str(), run.sampleRate, run.channels,
                        run.currentAo.empty() ? "-" : run.currentAo.c_str(), (long long) run.frameDrops,
                        (long long) run.decoderDrops, run.endReason.c_str());
}
