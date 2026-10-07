#include <SDL2/SDL.h>

#include "app.h"
#include "build_info.h"
#include "screens.h"
#include "../network/http.h"
#include "../platform/clock.h"
#include "../platform/fs.h"
#include "../platform/log.h"
#include "../ui/text.h"
#include "../ui/theme.h"
#include "../ui/widgets.h"

using namespace c2d;

namespace {
    const double KEEPALIVE_REDRAW = 1.0;   // idle screens are redrawn at most once per second
    const double IDLE_SLEEP_MS = 8;
}

Screen::Screen(App &a, bool isModal) : RectangleShape(FloatRect(0, 0, theme::SCREEN_W, theme::SCREEN_H)),
                                       app(a), modal(isModal) {
    setFillColor(isModal ? theme::scrim() : Color::Transparent);
}

Screen::~Screen() {
    *alive = false;
}

void Screen::redraw() {
    app.requestRedraw();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

App::App() : C2DRenderer({theme::SCREEN_W, theme::SCREEN_H}),
             profileStore(APP_DATA_DIR), settingsStore(APP_DATA_DIR), xtreamService(jobSystem), vodLibrary(xtreamService, APP_DATA_DIR),
             libraryStore(APP_DATA_DIR), imageLoader(jobSystem, APP_DATA_DIR "cache/images") {
    LOG_I("app", "renderer: %s", available ? "OK (SDL2 + OpenGL ES 2 / Piglet)" : "FAILED");
    romfsPath = getIo()->getRomFsPath();
    setClearColor(theme::bgTop());
    getInput()->setRepeatDelay(0);  // repeat/acceleration is handled by InputManager

    clockx::init();
    if (!ui::loadFonts(romfsPath + "assets/fonts/")) {
        LOG_E("app", "UI fonts missing from the package");
    }
    http::globalInit(romfsPath + "assets/cacert.pem", std::string("PS4IPTV/") + APP_VERSION);
    jobSystem.start(3);

    std::string warning;
    std::string err;
    if (!fs::ensureDir(APP_DATA_DIR, &err)) {
        LOG_E("storage", "%s", err.c_str());
    }
    profileStore.load(&warning);
    if (!warning.empty()) {
        LOG_W("storage", "%s", warning.c_str());
    }
    warning.clear();
    settingsStore.load(&warning);
    if (!warning.empty()) {
        LOG_W("storage", "%s", warning.c_str());
    }
    LOG_I("storage", "%d profile(s), active '%s'", (int) profileStore.profiles().size(),
          profileStore.activeId().c_str());
    warning.clear();
    libraryStore.load(&warning);
    if (!warning.empty()) {
        LOG_W("storage", "%s", warning.c_str());
    }
    imageLoader.setEnabled(settingsStore.get().loadImages);

    // the proven pPlay playback backend; created once, like pPlay's Player (needs the GL context)
    std::string mpvDir = std::string(APP_DATA_DIR) + "mpv";
    fs::ensureDir(mpvDir);
    if (!player.init(mpvDir)) {
        LOG_E("app", "%s", player.initError().c_str());
    }
    forceContinuousRedraw = fs::exists(std::string(APP_DATA_DIR) + "force_redraw");
    if (forceContinuousRedraw) {
        LOG_W("app", "force_redraw present: drawing every frame");
    }

    screenLayer = new RectangleShape(FloatRect(0, 0, theme::SCREEN_W, theme::SCREEN_H));
    screenLayer->setFillColor(Color::Transparent);
    add(screenLayer);

    toastBox = ui::box(this, FloatRect(0, 0, 900, 76), theme::surfaceRaised(), 38);
    toastBox->setOrigin(Origin::Bottom);
    toastBox->setPosition(theme::SCREEN_W / 2, theme::SCREEN_H - 120);
    toastText = ui::label(toastBox, "", theme::BODY, 0, ui::Label::centerOffset(theme::BODY, 76),
                          ui::Weight::SemiBold);
    toastText->setAlign(ui::Align::Center, 900);
    toastText->setMaxWidth(860);
    toastBox->setVisibility(Visibility::Hidden);

    // first screen
#if PS4IPTV_TEXT_TEST_AT_START
    push(screens::makeTextTest(*this, true));
#else
    push(firstScreen());
#endif
    applyNavigation();
}

Screen *App::firstScreen() {
    if (profileStore.profiles().empty()) {
        return screens::makeOnboarding(*this);
    } else if (profileStore.active() != nullptr) {
        return screens::makeConnect(*this, *profileStore.active());
    }
    return screens::makeProfiles(*this);
}

App::~App() {
    LOG_I("app", "shutting down");
    player.stop();
    player.shutdown();
    jobSystem.stop();
    for (auto *s: graveyard) {
        delete s;
    }
    graveyard.clear();
    http::globalShutdown();
}

void App::saveLibrary() {
    std::string err;
    if (!libraryStore.save(&err)) {
        LOG_E("storage", "library save failed: %s", err.c_str());
        toast("Could not save favorites/history", ToastKind::Error);
    }
}

double App::now() const {
    return clockx::monotonic();
}

void App::requestRedrawAt(double when) {
    if (redrawAt <= 0 || when < redrawAt) {
        redrawAt = when;
    }
}

void App::toast(const std::string &message, ToastKind kind) {
    toastText->setText(message);
    toastBox->setFillColor(kind == ToastKind::Error ? Color(120, 36, 44) : kind == ToastKind::Success
                                                                         ? Color(26, 92, 70)
                                                                         : theme::surfaceRaised());
    toastBox->setVisibility(Visibility::Visible);
    toastUntil = now() + 3.5;
    requestRedraw();
}

void App::push(Screen *screen) {
    pendingOps.emplace_back(0, screen);
    requestRedraw();
}

void App::pop() {
    pendingOps.emplace_back(1, nullptr);
    requestRedraw();
}

void App::replaceAll(Screen *screen) {
    pendingOps.emplace_back(2, screen);
    requestRedraw();
}

void App::applyNavigation() {
    std::vector<std::pair<int, Screen *>> ops;
    ops.swap(pendingOps);
    for (auto &op: ops) {
        if (op.first == 0) {
            if (top()) {
                top()->onPause();
            }
            screenLayer->add(op.second);
            stack.push_back(op.second);
            LOG_V("nav", "push %s", op.second->name());
            op.second->onEnter();
        } else if (op.first == 1) {
            if (stack.empty()) {
                continue;
            }
            Screen *s = stack.back();
            stack.pop_back();
            screenLayer->remove(s);
            graveyard.push_back(s);
            LOG_V("nav", "pop %s", s->name());
            if (top()) {
                top()->onResume();
            }
        } else {
            for (auto *s: stack) {
                screenLayer->remove(s);
                graveyard.push_back(s);
            }
            stack.clear();
            screenLayer->add(op.second);
            stack.push_back(op.second);
            LOG_V("nav", "replace all with %s", op.second->name());
            op.second->onEnter();
        }
    }
    if (!ops.empty()) {
        updateVisibility();
    }
}

void App::updateVisibility() {
    bool visible = true;
    for (int i = (int) stack.size() - 1; i >= 0; i--) {
        stack[(size_t) i]->setVisibility(visible ? Visibility::Visible : Visibility::Hidden);
        if (visible && !stack[(size_t) i]->isModal()) {
            visible = false;  // screens below an opaque screen are not drawn
        }
    }
}

void App::onUpdate() {
    C2DRenderer::onUpdate();  // libcross2d input polling + children updates (text geometry)
    if (!inDrawPass) {
        logic();
    }
}

void App::logic() {
    double t = now();

    // screens popped during the previous frame are deleted here, outside any of their callbacks
    for (auto *s: graveyard) {
        delete s;
    }
    graveyard.clear();

    if (getInput()->getKeys(0) & EV_QUIT) {
        LOG_I("app", "quit event");
        running = false;
        return;
    }

    // libcross2d opens pads only at startup: pick up a controller connected later
    void *joystick = getInput()->getPlayer(0)->data;
    if (joystick == nullptr) {
        if (ownPad == nullptr && t >= nextPadProbe) {
            nextPadProbe = t + 2.0;
            SDL_JoystickUpdate();
            if (SDL_NumJoysticks() > 0) {
                ownPad = SDL_JoystickOpen(0);
                LOG_I("input", "controller connected after startup: %s", ownPad ? "opened" : SDL_GetError());
            }
        }
        joystick = ownPad;
    }
    input.update(joystick, t);
    for (const auto &e: input.events()) {
        if (top()) {
            top()->handleInput(e);
        }
        requestRedraw();
    }

    if (jobSystem.pump() > 0) {
        requestRedraw();
    }
    if (imageLoader.update(t)) {
        requestRedraw();  // logos became available (at most two new textures per frame)
    }
    player.update(t);  // mpv events are drained every frame, whichever screen is on top
    if (top()) {
        top()->tick(t);
    }
    if (toastBox->isVisible() && t > toastUntil) {
        toastBox->setVisibility(Visibility::Hidden);
        requestRedraw();
    }
    applyNavigation();
}

void App::run() {
    LOG_I("app", "main loop running");
    while (running) {
        c2d::Renderer::flip(false, true);  // input + logic only, no GPU work
        if (!running) {
            break;
        }
        double t = now();
        bool timed = redrawAt > 0 && t >= redrawAt;
        bool animating = top() && top()->animating();
        if (dirty || timed || animating || forceContinuousRedraw || t - lastDraw > KEEPALIVE_REDRAW) {
            dirty = false;
            if (timed) {
                redrawAt = 0;
            }
            inDrawPass = true;
            C2DRenderer::flip(true, false);  // draw + swap
            inDrawPass = false;
            lastDraw = t;
        } else {
            SDL_Delay((Uint32) IDLE_SLEEP_MS);
        }
    }
    LOG_I("app", "main loop ended");
}
