#include <cerrno>
#include <cstring>
#include <sys/stat.h>

#include "app.h"
#include "build_info.h"
#include "diag_log.h"
#include "redact.h"

using namespace c2d;

namespace {

    const double WD_STOP_TIMEOUT = 8;  // keep in sync with app_playback.cpp
    const int INPUT_REPEAT_MS = 250;

    // local playback is proven by untouched pPlay on hardware: this build only tests the network path
    const char *const MENU_LABELS[] = {
            "NETWORK TS TEST",
            "NETWORK HLS TEST",
            "SHOW DIAGNOSTICS",
            "EXIT"
    };
    const int MENU_COUNT = 4;

    // measured with scripts/preflight-streams.py: the provider answered HTTP 403 15 s and 30 s after a
    // stream connection closed, and 200 again after ~45-60 s (one connection per account)
    const double PROVIDER_COOLDOWN = 60;

    // libcross2d key slot -> SDL joystick button index of PacBrew's SDL2 PS4 driver
    // (src/joystick/ps4/SDL_sysjoystick.c @ bf797a5: 0 cross, 1 circle, 2 square, 3 triangle,
    // 4 options, 6 touchpad, 9 L1, 10 R1, 11-14 d-pad). libcross2d's default PS4 mapping puts
    // "Start" on R1 and leaves Options unmapped; this mapping makes Start = Options.
    const int JOY_MAPPING[] = {
            11, 12, 13, 14,   // up, down, left, right
            6, 4,             // select (touchpad), start (options)
            0, 1, 2, 3,       // fire1 cross, fire2 circle, fire3 square, fire4 triangle
            9, 10,            // fire5 L1, fire6 R1
            4, 6,             // menu1 (options), menu2 (touchpad)
            0, 1, 2, 3,       // axes lx, ly, rx, ry
            0
    };

    // properties logged whenever they change
    const char *const OBSERVED[] = {
            "file-format", "video-codec", "video-params", "container-fps",
            "audio-codec-name", "audio-params", "audio-out-params", "current-ao", "current-vo",
            "hwdec-current", "paused-for-cache", "core-idle", "idle-active", "eof-reached",
            "pause", "seekable", "demuxer-via-network"
    };

    std::string truncate(const std::string &s, size_t max) {
        return s.size() <= max ? s : s.substr(0, max - 3) + "...";
    }

    Text *makeText(C2DObject *parent, const std::string &s, unsigned int size, float x, float y,
                   const Color &color = Color::White) {
        auto *t = new Text(s, size);
        t->setPosition(x, y);
        t->setFillColor(color);
        parent->add(t);
        return t;
    }

    bool isDir(const std::string &path) {
        struct stat st{};
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

App::App(const Vector2f &size) : C2DRenderer(size) {

    platformOk = available;
    if (platformOk) {
        auto glStr = [](GLenum name) {
            const GLubyte *s = glGetString(name);
            return s != nullptr ? std::string((const char *) s) : std::string("(null)");
        };
        glInfo = "GL_VENDOR=" + glStr(GL_VENDOR) + " GL_RENDERER=" + glStr(GL_RENDERER)
                 + " GL_VERSION=" + glStr(GL_VERSION) + " GLSL=" + glStr(GL_SHADING_LANGUAGE_VERSION);
        LOG_I("platform", "renderer init OK: SDL2 + OpenGL ES 2 (Piglet), %dx%d", (int) size.x, (int) size.y);
        LOG_I("platform", "%s", glInfo.c_str());
    } else {
        LOG_E("platform", "renderer init FAILED (SDL2Renderer not available): SDL_GetError=\"%s\"", SDL_GetError());
    }

    for (int i = 0; i < PLAYER_MAX; i++) {
        getInput()->setJoystickMapping(i, JOY_MAPPING, 8000);
    }
    getInput()->setRepeatDelay(INPUT_REPEAT_MS);
    LOG_I("input", "joystick mapping installed (X=select, Circle=back, Triangle=overlay, Options=diagnostics), "
                   "repeat delay %d ms", INPUT_REPEAT_MS);

    setClearColor(Color::Black);

    // pPlay creates its data dir and "<data>/mpv" before creating Mpv (pplay::Io::create)
    std::string mpvDir = std::string(APP_DATA_DIR) + "mpv";
    if (!isDir(mpvDir) && mkdir(mpvDir.c_str(), 0777) != 0) {
        LOG_W("storage", "mkdir(%s) failed: errno %d (%s)", mpvDir.c_str(), errno, strerror(errno));
    }

    ffmpeg = queryFfmpegInfo();
    {
        std::string protocols;
        for (const auto &p: ffmpeg.inputProtocols) {
            protocols += p + " ";
        }
        std::string demuxers, decoders;
        for (const auto &d: ffmpeg.demuxers) {
            demuxers += d + " ";
        }
        for (const auto &d: ffmpeg.decoders) {
            decoders += d + " ";
        }
        LOG_I("ffmpeg", "version %s (%s)", ffmpeg.version.c_str(), ffmpeg.libVersions.c_str());
        LOG_I("ffmpeg", "input protocols: %s", protocols.c_str());
        LOG_I("ffmpeg", "http: %s, https: %s, tls: %s", ffmpeg.hasHttp ? "yes" : "NO",
              ffmpeg.hasHttps ? "yes" : "NO", ffmpeg.hasTls ? "yes" : "NO");
        LOG_I("ffmpeg", "demuxers: %s", demuxers.c_str());
        LOG_I("ffmpeg", "decoders: %s", decoders.c_str());
    }

    // pPlay: Player() -> new Mpv(main->getIo()->getDataPath() + "mpv", true)
    LOG_I("player", "creating pPlay Mpv (config dir %s, render API OpenGL)", mpvDir.c_str());
    mpv = new Mpv(mpvDir, true);
    if (mpv->isAvailable()) {
        mpv_handle *h = mpv->getHandle();
        int res = mpv_request_log_messages(h, "v");
        if (res < 0) {
            LOG_W("player", "mpv_request_log_messages failed: %d (%s)", res, mpv_error_string(res));
        }
        char *v = mpv_get_property_string(h, "mpv-version");
        mpvVersion = v ? v : "(unknown)";
        mpv_free(v);
        v = mpv_get_property_string(h, "ffmpeg-version");
        ffmpegVersionFromMpv = v ? v : "(unknown)";
        mpv_free(v);
        LOG_I("player", "mpv init OK: %s, ffmpeg (as seen by mpv) %s, client API %lu.%lu", mpvVersion.c_str(),
              ffmpegVersionFromMpv.c_str(), mpv_client_api_version() >> 16, mpv_client_api_version() & 0xffff);

        // Deviation from pPlay (documented): no watch-later resume, so every open of the same URL starts
        // identically (pPlay resumes local files at the last position; meaningless for live TV).
        res = mpv_set_property_string(h, "resume-playback", "no");
        LOG_I("player", "set resume-playback=no: %d (%s)", res, mpv_error_string(res));

        for (size_t i = 0; i < sizeof(OBSERVED) / sizeof(OBSERVED[0]); i++) {
            res = mpv_observe_property(h, i, OBSERVED[i], MPV_FORMAT_NODE);
            if (res < 0) {
                LOG_W("player", "observe %s failed: %d (%s)", OBSERVED[i], res, mpv_error_string(res));
            }
        }
    } else {
        LOG_E("player", "MPV INIT FAILED at %s: %d (%s)", mpv->getInitErrorStep().c_str(), mpv->getInitError(),
              mpv_error_string(mpv->getInitError()));
    }

    // same romfs path pPlay reads its skin from (PS4Io::getRomFsPath() = "/app0/")
    romfsPath = getIo()->getRomFsPath();
    buildUi();
    refreshEnvironment();
    startDnsCheck();
    showScreen(Screen::Menu);
    LOG_I("app", "ready");
}

App::~App() {
    LOG_I("cleanup", "shutting down");
    if (mpv != nullptr) {
        LOG_I("cleanup", "destroying pPlay Mpv (mpv_render_context_free + mpv_terminate_destroy)");
        delete mpv;
        mpv = nullptr;
        LOG_I("cleanup", "mpv destroyed");
    }
    copyLogToUsb();
    // videoTexture and the UI objects are owned and deleted by the renderer (C2DObject children)
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// UI

void App::buildUi() {
    const Vector2f size = getSize();

    // playback: pPlay video texture (full screen FBO, same as pPlay's Player)
    videoTexture = new VideoTexture(mpv, size);
    videoTexture->setVisibility(Visibility::Hidden);
    add(videoTexture);

    overlayLayer = new RectangleShape(FloatRect(0, 0, size.x, size.y));
    overlayLayer->setFillColor(Color::Transparent);
    add(overlayLayer);
    overlayBg = new RectangleShape(FloatRect(20, 20, size.x - 40, 300));
    overlayBg->setFillColor(Color(0, 0, 0, 170));
    overlayLayer->add(overlayBg);
    overlayText = makeText(overlayLayer, "", 26, 40, 34);
    overlayWatchdogText = makeText(overlayLayer, "", 26, 40, 300, Color::Yellow);
    overlayErrorText = makeText(overlayLayer, "", 26, 40, 340, Color(255, 90, 90));

    // menu
    menuLayer = new RectangleShape(FloatRect(0, 0, size.x, size.y));
    menuLayer->setFillColor(Color(24, 26, 32));
    add(menuLayer);
    titleText = makeText(menuLayer, "PS4 IPTV Playback Test", 48, 60, 40);
    subtitleText = makeText(menuLayer, diag::format("v%s  |  build %s  |  git %s  |  title %s  |  pPlay playback stack "
                                                    "(libmpv 0.34.1 / FFmpeg 5.0 / SDL2 2.0.18)",
                                                    APP_VERSION, BUILD_DATE, BUILD_GIT_HASH, APP_TITLE_ID),
                            22, 62, 106, Color(160, 170, 180));
    menuHighlight = new RectangleShape(FloatRect(70, 0, 640, 58));
    menuHighlight->setFillColor(Color(40, 110, 200));
    menuLayer->add(menuHighlight);
    for (int i = 0; i < MENU_COUNT; i++) {
        menuItems.push_back(makeText(menuLayer, MENU_LABELS[i], 38, 90, 180 + (float) i * 72));
    }
    envText = makeText(menuLayer, "", 24, 780, 180, Color(210, 215, 220));
    messageText = makeText(menuLayer, "", 26, 60, 500, Color(255, 90, 90));
    cooldownText = makeText(menuLayer, "", 26, 60, 580, Color::Yellow);
    resultsText = makeText(menuLayer, "", 22, 60, 640, Color(200, 230, 200));
    hintText = makeText(menuLayer, "D-pad / left stick: select     X: run test     Circle: stop / back     "
                                   "Options: diagnostics     Triangle (during playback): hide overlay",
                        22, 60, size.y - 50, Color(150, 160, 170));

    // diagnostics
    diagLayer = new RectangleShape(FloatRect(0, 0, size.x, size.y));
    diagLayer->setFillColor(Color(16, 18, 22));
    add(diagLayer);
    diagText = makeText(diagLayer, "", 19, 30, 20, Color(220, 225, 230));
    makeText(diagLayer, "Circle / Options: back to menu", 22, 30, size.y - 40, Color(150, 160, 170));
}

void App::showScreen(Screen s) {
    screen = s;
    menuLayer->setVisibility(s == Screen::Menu ? Visibility::Visible : Visibility::Hidden);
    diagLayer->setVisibility(s == Screen::Diagnostics ? Visibility::Visible : Visibility::Hidden);
    videoTexture->setVisibility(s == Screen::Playback ? Visibility::Visible : Visibility::Hidden);
    overlayLayer->setVisibility(s == Screen::Playback && overlayVisible ? Visibility::Visible : Visibility::Hidden);
    if (s == Screen::Menu) {
        updateMenuText();
    } else if (s == Screen::Diagnostics) {
        updateDiagnosticsText();
    } else {
        updateOverlayText();
    }
}

std::vector<std::string> App::environmentLines(bool detailed) {
    std::vector<std::string> l;
    diag::FileStatus fs = diag::fileStatus();

    redact::UrlInfo ts = redact::parseUrl(env.config.tsUrl);
    redact::UrlInfo hls = redact::parseUrl(env.config.hlsUrl);

    std::string usb;
    for (const auto &u: env.usb) {
        if (u.exists) {
            usb += u.path + (u.hasTestFile ? " [test.mp4]" : "") + (u.hasConfig ? " [test_streams.txt]" : "") + "  ";
        }
    }
    if (usb.empty()) {
        usb = "no /mnt/usbN directory found";
    }

    std::string mpvState = mpv->isAvailable()
                           ? "OK  " + mpvVersion
                           : diag::format("INIT FAILED at %s: %d (%s)", mpv->getInitErrorStep().c_str(),
                                          mpv->getInitError(), mpv_error_string(mpv->getInitError()));

    auto describe = [&](const redact::UrlInfo &u, const std::string &raw) -> std::string {
        if (raw.empty()) {
            return "(not set)";
        }
        if (!u.valid) {
            return "INVALID URL";
        }
        std::string s = detailed ? u.sanitized : u.scheme + "://" + u.host + (u.port.empty() ? "" : ":" + u.port)
                                                 + "  (." + u.extension + ")";
        if (u.scheme == "https" && !ffmpeg.hasHttps) {
            s += "   !! https is NOT supported by this FFmpeg build";
        }
        return s;
    };

    if (!detailed) {
        l.push_back("Log file:   " + std::string(fs.ok ? "OK  " : "FAILED  ") + fs.path + (fs.ok ? "" : "  " + fs.detail));
        if (!logCopyStatus.empty()) {
            l.push_back("            " + logCopyStatus);
        }
        l.push_back("Platform:   " + std::string(platformOk ? "renderer OK" : "renderer FAILED"));
        l.push_back("mpv:        " + mpvState);
        l.push_back("FFmpeg:     " + ffmpeg.version + "   http " + (ffmpeg.hasHttp ? "yes" : "NO")
                    + " / https " + (ffmpeg.hasHttps ? "yes" : "NO"));
        l.push_back("Config:     " + configOrigin());
        l.push_back("TS:         " + std::string(env.config.tsUrl.empty() || !ts.valid ? "NOT READY  " : "READY  ")
                    + describe(ts, env.config.tsUrl));
        l.push_back("HLS:        " + std::string(env.config.hlsUrl.empty() || !hls.valid ? "NOT READY  " : "READY  ")
                    + describe(hls, env.config.hlsUrl));
        for (const auto &line: dnsLines()) {
            l.push_back("DNS:        " + line);
        }
        for (const auto &p: env.config.problems) {
            l.push_back("Config:     !! " + p);
        }
        return l;
    }

    l.push_back(diag::format("PS4 IPTV Playback Test v%s  build %s  git %s  title %s", APP_VERSION, BUILD_DATE,
                             BUILD_GIT_HASH, APP_TITLE_ID));
    l.push_back("LOG: " + fs.path + "  [" + (fs.ok ? "OK, " : "FAILED: ") + fs.detail
                + "]  previous session: log.prev.txt  |  " + (logCopyStatus.empty() ? "not copied to USB yet"
                                                                                     : logCopyStatus));
    l.push_back(std::string("PLATFORM: renderer ") + (platformOk ? "OK" : "FAILED") + "  " + glInfo);
    l.push_back("MPV: " + mpvState + "   FFmpeg seen by mpv: " + ffmpegVersionFromMpv);
    std::string protocols;
    for (const auto &p: ffmpeg.inputProtocols) {
        protocols += p + " ";
    }
    l.push_back("FFMPEG: " + ffmpeg.version + " (" + ffmpeg.libVersions + ")  protocols: " + protocols);
    std::string dem, dec;
    for (const auto &d: ffmpeg.demuxers) {
        dem += d + " ";
    }
    for (const auto &d: ffmpeg.decoders) {
        dec += d + " ";
    }
    l.push_back("DEMUXERS: " + dem + "  DECODERS: " + dec);
    l.push_back("CONFIG: " + configOrigin());
    for (const auto &line: dnsLines()) {
        l.push_back("DNS: " + line);
    }
    std::string root = env.rootError;
    for (const auto &e: env.rootEntries) {
        root += e + " ";
    }
    std::string mnt = env.mntError;
    for (const auto &e: env.mntEntries) {
        mnt += e + " ";
    }
    l.push_back("FS (evidence only) '/': " + root);
    l.push_back("FS (evidence only) '/mnt': " + mnt + "   USB: " + usb);
    l.push_back("  TS:  " + describe(ts, env.config.tsUrl));
    l.push_back("  HLS: " + describe(hls, env.config.hlsUrl));
    if (!env.config.userAgent.empty()) {
        l.push_back("  USER_AGENT: " + env.config.userAgent);
    }
    for (const auto &o: env.config.mpvOptions) {
        l.push_back("  MPV_OPT: " + o.first + "=" + o.second);
    }
    for (const auto &p: env.config.problems) {
        l.push_back("  !! " + p);
    }
    return l;
}

std::string App::configOrigin() const {
    if (!env.config.found) {
        std::string searched;
        for (const auto &c: env.configCandidates) {
            searched += c + " ";
        }
        return "NOT FOUND (searched " + searched + ") - rebuild with config/test_streams.txt";
    }
    bool packaged = env.config.path.compare(0, romfsPath.size(), romfsPath) == 0;
    return env.config.path + (packaged ? "  (packaged in the PKG)" : "  (override, not the packaged copy)");
}

std::vector<std::string> App::dnsLines() const {
    std::vector<std::string> lines;
    for (const auto &r: dns.results()) {
        if (!r.done) {
            lines.push_back(r.host + " resolving...");
        } else if (r.ok) {
            lines.push_back(diag::format("%s -> %s (%.0f ms)", r.host.c_str(), r.addresses.c_str(), r.ms));
        } else {
            lines.push_back(diag::format("%s FAILED: %s", r.host.c_str(), r.error.c_str()));
        }
    }
    return lines;
}

void App::updateMenuText() {
    menuHighlight->setPosition(70, 180 + (float) menuIndex * 72 - 4);
    for (int i = 0; i < MENU_COUNT; i++) {
        menuItems[i]->setFillColor(i == menuIndex ? Color::White : Color(170, 175, 185));
    }

    std::string e;
    for (const auto &line: environmentLines(false)) {
        e += truncate(line, 95) + "\n";
    }
    setTextCached(envText, cacheEnv, e);
    setTextCached(messageText, cacheMessage, menuMessage);
    std::string cooldown;
    if (lastStreamClosedUptime >= 0) {
        double ago = diag::uptime() - lastStreamClosedUptime;
        if (ago < PROVIDER_COOLDOWN) {
            cooldown = diag::format("Last stream closed %.0f s ago. This provider keeps the connection slot busy for "
                                    "30-60 s (HTTP 403 meanwhile): wait %.0f s before the next test.",
                                    ago, PROVIDER_COOLDOWN - ago);
        } else {
            cooldown = diag::format("Last stream closed %.0f s ago: OK to start the next test.", ago);
        }
    }
    setTextCached(cooldownText, cacheCooldown, cooldown);

    std::string r = "RESULTS (this session):\n";
    if (results.empty()) {
        r += "  none yet\n";
    }
    for (const auto &line: results) {
        r += "  " + truncate(line, 150) + "\n";
    }
    setTextCached(resultsText, cacheResults, r);
}

void App::updateDiagnosticsText() {
    std::string d;
    for (const auto &line: environmentLines(true)) {
        d += truncate(line, 175) + "\n";
    }
    d += "RESULTS:\n";
    for (const auto &line: results) {
        d += "  " + truncate(line, 170) + "\n";
    }
    if (results.empty()) {
        d += "  none yet\n";
    }
    size_t used = 0;
    for (char c: d) {
        used += c == '\n';
    }
    size_t room = used < 42 ? 42 - used : 4;
    d += "RECENT LOG (no verbose lines):\n";
    for (const auto &line: diag::recent(room)) {
        d += truncate(line, 175) + "\n";
    }
    setTextCached(diagText, cacheDiag, d);
}

void App::handleInput() {
    unsigned int keys = getInput()->getKeys(0);
    if (keys & EV_QUIT) {
        LOG_I("app", "quit event received");
        running = false;
        return;
    }
    if (keys == Input::Key::Delay) {
        return;  // repeat delay running: nothing new
    }
    // actions trigger on press only, navigation also repeats while held
    unsigned int pressed = keys & ~prevKeys;
    prevKeys = keys;
    unsigned int nav = keys & (Input::Key::Up | Input::Key::Down);

    switch (screen) {
        case Screen::Menu:
            if (nav & Input::Key::Up) {
                menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT;
                updateMenuText();
            } else if (nav & Input::Key::Down) {
                menuIndex = (menuIndex + 1) % MENU_COUNT;
                updateMenuText();
            } else if (pressed & Input::Key::Start) {
                showScreen(Screen::Diagnostics);
            } else if (pressed & Input::Key::Fire1) {
                switch (menuIndex) {
                    case 0:
                        startTest(Source::Ts);
                        break;
                    case 1:
                        startTest(Source::Hls);
                        break;
                    case 2:
                        refreshEnvironment();
                        showScreen(Screen::Diagnostics);
                        break;
                    default:
                        LOG_I("app", "EXIT selected");
                        running = false;
                        break;
                }
            }
            break;

        case Screen::Diagnostics:
            if (pressed & (Input::Key::Fire2 | Input::Key::Start)) {
                showScreen(Screen::Menu);
            }
            break;

        case Screen::Playback:
            if (pressed & Input::Key::Fire2) {
                if (state == PlayState::Opening || state == PlayState::Buffering || state == PlayState::Playing) {
                    requestStop();
                } else if (state == PlayState::Stopping) {
                    if (runTime() - run.tStopRequested > WD_STOP_TIMEOUT) {
                        LOG_W("watchdog", "forced return to menu: mpv did not confirm stop within %.0fs",
                              WD_STOP_TIMEOUT);
                        finishRun("stop NOT confirmed by mpv (forced return)");
                        returnToMenu();
                    }
                } else {
                    returnToMenu();
                }
            } else if (pressed & Input::Key::Fire4) {
                overlayVisible = !overlayVisible;
                overlayLayer->setVisibility(overlayVisible ? Visibility::Visible : Visibility::Hidden);
            } else if (pressed & Input::Key::Start) {
                // full diagnostics are shown from the menu; during playback Options shows the overlay again
                overlayVisible = true;
                overlayLayer->setVisibility(Visibility::Visible);
            }
            break;
    }
}

void App::startDnsCheck() {
    std::vector<std::pair<std::string, std::string>> hosts;
    for (const auto *url: {&env.config.tsUrl, &env.config.hlsUrl}) {
        redact::UrlInfo u = redact::parseUrl(*url);
        if (!u.valid) {
            continue;
        }
        std::string port = u.port.empty() ? (u.scheme == "https" ? "443" : "80") : u.port;
        bool dup = false;
        for (const auto &h: hosts) {
            dup |= h.first == u.host && h.second == port;
        }
        if (!dup) {
            hosts.emplace_back(u.host, port);
        }
    }
    if (!hosts.empty()) {
        LOG_I("dns", "resolving %d stream host(s) in the background (same getaddrinfo call as FFmpeg tcp.c)",
              (int) hosts.size());
        dns.start(hosts);
    }
}

void App::copyLogToUsb() {
    std::string root;
    for (const auto &u: env.usb) {
        if (u.exists) {
            root = u.path;
            break;
        }
    }
    if (root.empty()) {
        logCopyStatus = "log not copied: no USB stick found";
        return;
    }
    std::string dest = root + "PS4IPTVTest-log.txt";
    std::string error;
    if (diag::copyLogTo(dest, error)) {
        logCopyStatus = "log copied to " + dest;
        LOG_I("storage", "%s", logCopyStatus.c_str());
    } else {
        logCopyStatus = "log copy to USB FAILED: " + error;
        LOG_W("storage", "%s", logCopyStatus.c_str());
    }
}

void App::refreshEnvironment() {
    env = probeTestEnv(APP_DATA_DIR, romfsPath);
    if (!env.config.tsUrl.empty()) {
        redact::addUrl(env.config.tsUrl);
    }
    if (!env.config.hlsUrl.empty()) {
        redact::addUrl(env.config.hlsUrl);
    }

    std::string mnt = env.mntError;
    for (const auto &e: env.mntEntries) {
        mnt += e + " ";
    }
    LOG_I("env", "/mnt entries: %s", mnt.c_str());
    for (const auto &u: env.usb) {
        if (u.exists) {
            LOG_I("env", "%s present (test.mp4: %s, test_streams.txt: %s)", u.path.c_str(),
                  u.hasTestFile ? "yes" : "no", u.hasConfig ? "yes" : "no");
        }
    }
    std::string root = env.rootError;
    for (const auto &e: env.rootEntries) {
        root += e + " ";
    }
    LOG_I("env", "'/' entries (filesystem view of this process): %s", root.c_str());
    if (env.config.found) {
        redact::UrlInfo ts = redact::parseUrl(env.config.tsUrl);
        redact::UrlInfo hls = redact::parseUrl(env.config.hlsUrl);
        LOG_I("env", "config %s: TS %s, HLS %s, USER_AGENT %s, MPV_OPT x%d", env.config.path.c_str(),
              ts.valid ? ts.sanitized.c_str() : "(none)", hls.valid ? hls.sanitized.c_str() : "(none)",
              env.config.userAgent.empty() ? "(default)" : env.config.userAgent.c_str(),
              (int) env.config.mpvOptions.size());
        for (const auto &p: env.config.problems) {
            LOG_W("env", "config: %s", p.c_str());
        }
    } else {
        LOG_W("env", "test_streams.txt not found on USB roots or in %s", APP_DATA_DIR);
    }
}
