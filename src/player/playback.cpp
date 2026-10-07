#include <cmath>
#include <cstring>

#include "playback.h"
#include "../platform/log.h"
#include "../platform/redact.h"

namespace {

    const char *const OBSERVED[] = {
            "file-format", "video-codec", "video-params", "container-fps", "audio-codec-name", "audio-params",
            "current-ao", "paused-for-cache", "idle-active", "seekable", "duration", "pause"};

    std::string shortCodec(const std::string &codec) {
        size_t p = codec.find(" (");
        return p == std::string::npos ? codec : codec.substr(0, p);
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
        return v && v->format == MPV_FORMAT_INT64 ? v->u.int64 : v && v->format == MPV_FORMAT_DOUBLE
                                                                  ? (int64_t) v->u.double_ : 0;
    }

    bool mapFlag(const mpv_node *n, const char *key) {
        const mpv_node *v = mapGet(n, key);
        return v && v->format == MPV_FORMAT_FLAG && v->u.flag;
    }

    std::string mapStr(const mpv_node *n, const char *key) {
        const mpv_node *v = mapGet(n, key);
        return v && v->format == MPV_FORMAT_STRING && v->u.string ? v->u.string : "";
    }

    bool startsWith(const std::string &s, const char *p) {
        return s.compare(0, strlen(p), p) == 0;
    }

    bool contains(const std::string &s, const char *p) {
        return s.find(p) != std::string::npos;
    }
}

const char *Playback::stateText(PlaybackState s) {
    switch (s) {
        case PlaybackState::Idle:
            return "";
        case PlaybackState::Opening:
            return "Opening\xE2\x80\xA6";
        case PlaybackState::Buffering:
            return "Buffering\xE2\x80\xA6";
        case PlaybackState::Playing:
            return "Playing";
        case PlaybackState::Paused:
            return "Paused";
        case PlaybackState::Ended:
            return "Ended";
        default:
            return "Playback error";
    }
}

bool Playback::init(const std::string &configDir) {
    // pPlay: Player() -> new Mpv(<data>/mpv, true)
    mpv = new Mpv(configDir, true);
    if (!mpv->isAvailable()) {
        LOG_E("player", "pPlay Mpv init failed at %s: %d (%s)", mpv->getInitErrorStep().c_str(), mpv->getInitError(),
              mpv_error_string(mpv->getInitError()));
        return false;
    }
    mpv_handle *h = mpv->getHandle();
    mpv_request_log_messages(h, "v");  // verbose lines are needed to detect a missing precompiled shader
    // VOD resume is handled by the app (progress store), not by mpv's watch-later files
    mpv_set_property_string(h, "resume-playback", "no");
    for (size_t i = 0; i < sizeof(OBSERVED) / sizeof(OBSERVED[0]); i++) {
        mpv_observe_property(h, i, OBSERVED[i], MPV_FORMAT_NODE);
    }
    char *v = mpv_get_property_string(h, "mpv-version");
    LOG_I("player", "pPlay playback backend ready: %s", v ? v : "?");
    mpv_free(v);
    return true;
}

void Playback::shutdown() {
    if (mpv != nullptr) {
        delete mpv;  // mpv_render_context_free + mpv_terminate_destroy (GL context still alive)
        mpv = nullptr;
    }
}

std::string Playback::initError() const {
    if (mpv == nullptr) {
        return "Player not initialized";
    }
    return diag::format("Player initialization failed (%s: %s)", mpv->getInitErrorStep().c_str(),
                        mpv_error_string(mpv->getInitError()));
}

void Playback::open(const std::string &url, const std::string &format) {
    open(url, format, OpenOptions());
}

void Playback::open(const std::string &url, const std::string &format, const OpenOptions &options) {
    if (!available()) {
        fail(PlaybackError::Other, "Player initialization failed", initError());
        return;
    }
    si = StreamInfo();
    si.format = format;
    err = PlaybackError::None;
    errMsg.clear();
    errDetail.clear();
    entryId = -1;
    stopRequested = false;
    openedAt = now;
    firstFrameAt = -1;
    lastStats = 0;
    lastProgress = -1;
    lastPos = -1;
    lastFrames = 0;
    cachePaused = false;
    firstNetworkError.clear();
    trackItems.clear();
    chooser = options.chooseTracks;
    subtitleRenderFailed = false;
    redact::addUrl(url);

    if (url.compare(0, 8, "https://") == 0) {
        // pPlay's FFmpeg build has no https/tls protocol (verified: file ftp http rtmp rtp tcp udp)
        fail(PlaybackError::HttpsUnsupported, "This build currently supports HTTP streams only.", "https URL");
        return;
    }

    mpv_handle *h = mpv->getHandle();
    // pPlay Player::load() turns subtitles off unless slang is configured. Here they always start off: for
    // movies/episodes the chooser picks the tracks once the file is loaded (mpv then switches at runtime).
    mpv_set_option_string(h, "sid", "no");
    mpv_set_option_string(h, "aid", "auto");

    st = PlaybackState::Opening;
    std::string loadOptions = "pause=yes,speed=1";   // pPlay: start paused, resume on MPV_EVENT_FILE_LOADED
    if (options.start > 1) {
        loadOptions += diag::format(",start=%.1f", options.start);
        LOG_I("player", "resuming at %.1fs", options.start);
    }
    int res = mpv->load(url, Mpv::LoadType::Replace, loadOptions);
    if (res != 0) {
        fail(PlaybackError::Other, "Playback error", diag::format("loadfile: %d (%s)", res, mpv_error_string(res)));
    }
}

void Playback::stop() {
    if (!available() || st == PlaybackState::Idle) {
        st = PlaybackState::Idle;
        return;
    }
    stopRequested = true;
    mpv->stop();  // pPlay: write-watch-later-config + stop
    st = PlaybackState::Idle;
}

void Playback::setPaused(bool paused) {
    if (!available() || (st != PlaybackState::Playing && st != PlaybackState::Paused)) {
        return;
    }
    if (paused) {
        mpv->pause();
        st = PlaybackState::Paused;
    } else {
        mpv->resume();
        st = PlaybackState::Playing;
        lastProgress = now;   // the pause was not a stall
    }
}

void Playback::seekRelative(double seconds) {
    if (available() && si.seekable && (st == PlaybackState::Playing || st == PlaybackState::Paused)) {
        mpv->seek(seconds);
    }
}

void Playback::applyOptions(const stability::Options &options) {
    if (!available()) {
        return;
    }
    for (const auto &o: options) {
        int res = mpv_set_property_string(mpv->getHandle(), o.first.c_str(), o.second.c_str());
        if (res < 0) {
            LOG_E("player", "option %s=%s rejected: %d (%s)", o.first.c_str(), o.second.c_str(), res,
                  mpv_error_string(res));
        } else {
            LOG_V("player", "option %s=%s", o.first.c_str(), o.second.c_str());
        }
    }
}

stability::FailKind Playback::failKind() const {
    switch (err) {
        case PlaybackError::Http403:
            return stability::FailKind::Refused;
        case PlaybackError::HttpClient:
        case PlaybackError::HttpsUnsupported:
        case PlaybackError::UnsupportedCodec:
        case PlaybackError::Renderer:
        case PlaybackError::Audio:
        case PlaybackError::Demux:
            return stability::FailKind::Fatal;
        default:
            return stability::FailKind::Retryable;   // network, HTTP 5xx, other
    }
}

void Playback::seekTo(double seconds) {
    if (!available() || !si.seekable || (st != PlaybackState::Playing && st != PlaybackState::Paused
                                         && st != PlaybackState::Buffering)) {
        return;
    }
    std::string target = diag::format("%.1f", seconds < 0 ? 0.0 : seconds);
    const char *cmd[] = {"seek", target.c_str(), "absolute+keyframes", nullptr};
    int res = mpv_command(mpv->getHandle(), cmd);
    if (res < 0) {
        LOG_W("player", "seek to %s failed: %d (%s)", target.c_str(), res, mpv_error_string(res));
    }
    lastProgress = now;   // re-buffering after a seek starts the stall clock again
}

void Playback::readTracks() {
    trackItems.clear();
    mpv_node node;
    if (mpv_get_property(mpv->getHandle(), "track-list", MPV_FORMAT_NODE, &node) < 0) {
        return;
    }
    if (node.format == MPV_FORMAT_NODE_ARRAY) {
        for (int i = 0; i < node.u.list->num; i++) {
            const mpv_node *t = &node.u.list->values[i];
            std::string type = mapStr(t, "type");
            if (type != "audio" && type != "sub") {
                continue;
            }
            tracks::Track tr;
            tr.kind = type == "audio" ? tracks::Kind::Audio : tracks::Kind::Subtitle;
            tr.id = (int) mapInt(t, "id");
            tr.lang = mapStr(t, "lang");
            tr.title = mapStr(t, "title");
            tr.codec = mapStr(t, "codec");
            tr.channels = (int) mapInt(t, "demux-channel-count");
            tr.isDefault = mapFlag(t, "default");
            tr.forced = mapFlag(t, "forced");
            tr.external = mapFlag(t, "external");
            tr.selected = mapFlag(t, "selected");
            trackItems.push_back(tr);
        }
    }
    mpv_free_node_contents(&node);
    int audio = 0;
    int subs = 0;
    for (const auto &t: trackItems) {
        (t.kind == tracks::Kind::Audio ? audio : subs)++;
        LOG_I("player", "track %s %d: lang '%s' codec %s ch %d%s%s", t.kind == tracks::Kind::Audio ? "audio" : "sub",
              t.id, t.lang.c_str(), t.codec.c_str(), t.channels, t.isDefault ? " default" : "",
              t.forced ? " forced" : "");
    }
    LOG_I("player", "%d audio / %d subtitle track(s)", audio, subs);
}

void Playback::markSelected(tracks::Kind kind, int id) {
    for (auto &t: trackItems) {
        if (t.kind == kind) {
            t.selected = t.id == id;
        }
    }
}

void Playback::selectAudio(int id) {
    if (!available() || id <= 0) {
        return;
    }
    int res = mpv->setAid(id);   // pPlay wrapper: "no-osd set aid <id>"
    LOG_I("player", "audio track %d: %d (%s)", id, res, mpv_error_string(res));
    if (res >= 0) {
        markSelected(tracks::Kind::Audio, id);
    }
}

void Playback::selectSubtitle(int id) {
    if (!available()) {
        return;
    }
    int res = mpv->setSid(id > 0 ? id : -1);   // -1: "no-osd set sid no"
    LOG_I("player", "subtitle track %d: %d (%s)", id, res, mpv_error_string(res));
    if (res >= 0) {
        markSelected(tracks::Kind::Subtitle, id);
    }
}

void Playback::setFramesRendered(unsigned long frames) {
    if (frames > lastFrames && st != PlaybackState::Idle && st != PlaybackState::Error) {
        lastFrames = frames;
        lastProgress = now;   // a new video frame is progress
    }
    if (frames > 0 && firstFrameAt < 0 && st != PlaybackState::Idle && st != PlaybackState::Error) {
        firstFrameAt = now;
        LOG_I("player", "first video frame after %.2fs: %s %dx%d %s, audio %s %dHz %dch (ao %s)", now - openedAt,
              si.videoCodec.c_str(), si.width, si.height, si.pixelFormat.c_str(), si.audioCodec.c_str(),
              si.sampleRate, si.channels, si.audioOutput.empty() ? "-" : si.audioOutput.c_str());
    }
}

void Playback::fail(PlaybackError e, const std::string &message, const std::string &detail) {
    if (st == PlaybackState::Error && err != PlaybackError::None) {
        return;  // keep the first, most specific error
    }
    st = PlaybackState::Error;
    err = e;
    errMsg = message;
    errDetail = redact::apply(detail);
    LOG_E("player", "%s (%s)", errMsg.c_str(), errDetail.c_str());
}

void Playback::update(double t) {
    now = t;
    if (!available()) {
        return;
    }
    for (int i = 0; i < 500; i++) {
        mpv_event *ev = mpv->getEvent();
        if (ev == nullptr || ev->event_id == MPV_EVENT_NONE) {
            break;
        }
        switch (ev->event_id) {
            case MPV_EVENT_LOG_MESSAGE:
                onLog((mpv_event_log_message *) ev->data);
                break;
            case MPV_EVENT_PROPERTY_CHANGE:
                onProperty((mpv_event_property *) ev->data);
                break;
            case MPV_EVENT_START_FILE:
                if (entryId < 0 && st != PlaybackState::Idle) {
                    entryId = ((mpv_event_start_file *) ev->data)->playlist_entry_id;
                }
                break;
            case MPV_EVENT_FILE_LOADED:
                if (st == PlaybackState::Opening) {
                    readTracks();
                    if (chooser) {
                        std::pair<int, int> pick = chooser(trackItems);
                        if (pick.first > 0) {
                            selectAudio(pick.first);
                        }
                        selectSubtitle(pick.second);
                    }
                    mpv->resume();  // pPlay Player::onLoadEvent()
                    st = PlaybackState::Buffering;
                    LOG_I("player", "file loaded after %.2fs", now - openedAt);
                }
                break;
            case MPV_EVENT_END_FILE:
                onEndFile((mpv_event_end_file *) ev->data);
                break;
            default:
                break;
        }
    }

    if (st == PlaybackState::Buffering || st == PlaybackState::Playing) {
        if (now - lastStats >= 0.25) {   // progress / cache state for stall detection
            lastStats = now;
            pollStats();
        }
        if (st == PlaybackState::Buffering && firstFrameAt >= 0 && !cachePaused && lastProgress >= now - 1.5) {
            st = PlaybackState::Playing;
        }
    }
}

void Playback::pollStats() {
    mpv_handle *h = mpv->getHandle();
    double d;
    int64_t i;
    if (mpv_get_property(h, "time-pos", MPV_FORMAT_DOUBLE, &d) >= 0) {
        si.position = d;
        if (lastPos < 0 || std::fabs(d - lastPos) > 0.01) {   // backwards too: a seek is progress
            lastProgress = now;
            lastPos = d;
        }
        // radio channels never render a video frame: audio playing counts as started
        if (firstFrameAt < 0 && d > 0.5 && si.videoCodec.empty() && !si.audioOutput.empty()) {
            firstFrameAt = now;
            LOG_I("player", "audio-only stream playing after %.2fs: %s %dHz %dch", now - openedAt,
                  si.audioCodec.c_str(), si.sampleRate, si.channels);
        }
    }
    if (mpv_get_property(h, "demuxer-cache-duration", MPV_FORMAT_DOUBLE, &d) >= 0) {
        si.cacheSeconds = d;
    }
    if (mpv_get_property(h, "frame-drop-count", MPV_FORMAT_INT64, &i) >= 0) {
        si.droppedFrames = (long long) i;
    }
    if (mpv_get_property(h, "cache-speed", MPV_FORMAT_INT64, &i) >= 0) {
        si.cacheSpeed = (long long) i;
    }
    if (si.fps <= 0 && mpv_get_property(h, "estimated-vf-fps", MPV_FORMAT_DOUBLE, &d) >= 0) {
        si.fps = d;
    }
}

void Playback::onLog(const mpv_event_log_message *msg) {
    std::string prefix = msg->prefix ? msg->prefix : "?";
    std::string text = msg->text ? msg->text : "";
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    diag::Level level = msg->log_level <= MPV_LOG_LEVEL_ERROR ? diag::Level::Error
                        : msg->log_level == MPV_LOG_LEVEL_WARN ? diag::Level::Warn
                        : msg->log_level == MPV_LOG_LEVEL_INFO ? diag::Level::Info : diag::Level::Verbose;
    diag::write(level, "mpv/" + prefix, text);

    if (contains(text, "compile_attach_shader: type:")) {
        lastShader = text.substr(text.find("type:"));
    }
    if (st != PlaybackState::Opening && st != PlaybackState::Buffering && st != PlaybackState::Playing) {
        return;
    }
    // message texts and levels verified against mpv 0.34.1 / FFmpeg 5.0 sources (see the playback test)
    if (contains(text, "precompiled shader not found")) {
        const tracks::Track *sub = tracks::selected(trackItems, tracks::Kind::Subtitle);
        if (sub != nullptr && firstFrameAt >= 0) {
            // the picture was fine until subtitles were drawn: keep playing, without subtitles
            LOG_W("player", "subtitle rendering not available (%s): subtitles turned off", lastShader.c_str());
            subtitleRenderFailed = true;
            selectSubtitle(0);
            return;
        }
        fail(PlaybackError::Renderer, "This video format is not supported on PS4 yet.",
             "precompiled mpv shader missing: " + lastShader);
        return;
    }
    if (prefix == "ffmpeg" && contains(text, "HTTP error")) {
        if (firstNetworkError.empty()) {
            firstNetworkError = text;
        }
        if (contains(text, "HTTP error 403")) {
            fail(PlaybackError::Http403, "Provider temporarily refused the stream (HTTP 403).", text);
        } else if (contains(text, "HTTP error 404")) {
            fail(PlaybackError::HttpClient, "Stream unavailable (HTTP 404).", text);
        } else if (contains(text, "HTTP error 401")) {
            fail(PlaybackError::HttpClient, "Stream access denied (HTTP 401).", text);
        } else {
            fail(PlaybackError::HttpOther, "Stream unavailable (" + text.substr(text.find("HTTP error")) + ").", text);
        }
        return;
    }
    if (level != diag::Level::Error) {
        return;
    }
    if (prefix == "ffmpeg" && (contains(text, "Failed to resolve hostname") || contains(text, "Connection to"))) {
        fail(PlaybackError::Network, "Unable to connect to the stream server.", text);
    } else if (contains(text, "Protocol not found")) {
        fail(PlaybackError::HttpsUnsupported, "This build currently supports HTTP streams only.", text);
    } else if (contains(text, "Could not open/initialize audio device")) {
        fail(PlaybackError::Audio, "Audio initialization failed.", text);
    } else if (startsWith(prefix, "vd") && contains(text, "Could not open codec")) {
        fail(PlaybackError::UnsupportedCodec, "Unsupported video codec.", text);
    } else if (contains(text, "Failed to recognize file format")) {
        fail(PlaybackError::Demux, "Stream format not recognized.", text);
    }
}

void Playback::onProperty(const mpv_event_property *prop) {
    const std::string name = prop->name;
    const mpv_node *n = prop->format == MPV_FORMAT_NODE ? (const mpv_node *) prop->data : nullptr;
    auto str = [n]() {
        return n && n->format == MPV_FORMAT_STRING && n->u.string ? std::string(n->u.string) : std::string();
    };
    if (name == "file-format") {
        si.container = str();
    } else if (name == "video-codec") {
        si.videoCodec = shortCodec(str());
    } else if (name == "video-params" && n) {
        si.width = (int) mapInt(n, "w");
        si.height = (int) mapInt(n, "h");
        si.pixelFormat = mapStr(n, "pixelformat");
    } else if (name == "container-fps" && n && n->format == MPV_FORMAT_DOUBLE) {
        si.fps = n->u.double_;
    } else if (name == "audio-codec-name") {
        si.audioCodec = str();
    } else if (name == "audio-params" && n) {
        si.sampleRate = (int) mapInt(n, "samplerate");
        si.channels = (int) mapInt(n, "channel-count");
    } else if (name == "current-ao") {
        si.audioOutput = str();
    } else if (name == "paused-for-cache") {
        cachePaused = n && n->format == MPV_FORMAT_FLAG && n->u.flag;
        if (cachePaused && st == PlaybackState::Playing) {
            st = PlaybackState::Buffering;
        }
    } else if (name == "seekable") {
        si.seekable = n && n->format == MPV_FORMAT_FLAG && n->u.flag;
    } else if (name == "duration" && n && n->format == MPV_FORMAT_DOUBLE) {
        si.duration = n->u.double_;
    }
}

void Playback::onEndFile(const mpv_event_end_file *ef) {
    LOG_I("player", "END_FILE entry %lld reason %d error %d (%s)", (long long) ef->playlist_entry_id, ef->reason,
          ef->error, mpv_error_string(ef->error));
    if (entryId < 0 || ef->playlist_entry_id != entryId) {
        return;  // previous file, or a playlist redirect before this file started
    }
    if (stopRequested || st == PlaybackState::Idle) {
        return;
    }
    if (ef->reason == MPV_END_FILE_REASON_REDIRECT) {
        entryId = -1;  // mpv resolved a playlist: follow its first entry
        return;
    }
    if (ef->reason == MPV_END_FILE_REASON_ERROR) {
        switch (ef->error) {
            case MPV_ERROR_AO_INIT_FAILED:
                fail(PlaybackError::Audio, "Audio initialization failed.", mpv_error_string(ef->error));
                break;
            case MPV_ERROR_VO_INIT_FAILED:
                fail(PlaybackError::Renderer, "Video output failed.", mpv_error_string(ef->error));
                break;
            case MPV_ERROR_UNKNOWN_FORMAT:
                fail(PlaybackError::Demux, "Stream format not recognized.", mpv_error_string(ef->error));
                break;
            case MPV_ERROR_LOADING_FAILED:
                fail(PlaybackError::Network, "Stream unavailable.", firstNetworkError.empty()
                                                                  ? mpv_error_string(ef->error) : firstNetworkError);
                break;
            default:
                fail(PlaybackError::Other, "Playback error.", mpv_error_string(ef->error));
                break;
        }
        return;
    }
    st = PlaybackState::Ended;
    LOG_I("player", "stream ended (%s) after %.1fs", ef->reason == MPV_END_FILE_REASON_EOF ? "EOF" : "stop",
          si.position);
}
