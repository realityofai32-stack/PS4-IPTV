// Base class for full screens and modal overlays.

#ifndef PS4IPTV_APP_SCREEN_H
#define PS4IPTV_APP_SCREEN_H

#include <functional>
#include <memory>

#include "cross2d/c2d.h"
#include "../platform/input.h"
#include "../platform/keyboard_logic.h"

class App;

class Screen : public c2d::RectangleShape {

public:

    explicit Screen(App &app, bool modal = false);

    ~Screen() override;

    virtual const char *name() const = 0;

    virtual void onEnter() {}

    virtual void onResume() {}   // top again after the screen above was closed

    virtual void onPause() {}    // another screen opened above

    virtual void onAppExit() {}  // the app is closing (every screen on the stack, top first)

    // the user switched between the controller and the keyboard (actions::tracker()): hint bars redraw themselves;
    // screens with key names inside their texts (screens::keyName) set them again
    virtual void onInputDeviceChanged() {}

    virtual void handleInput(const InputEvent &event) = 0;

    // A physical keyboard key. Text entry screens take characters and editing keys and return true; anything a
    // screen does not take is turned into the matching controller button (App::onKey): arrows move, Enter is
    // Cross, Escape / Backspace are Circle, Page Up / Down are L2 / R2.
    virtual bool handleKey(const kbd::Event &event) {
        (void) event;
        return false;
    }

    // every logic frame while this screen is on top
    virtual void tick(double now) { (void) now; }

    // true while an animation needs continuous redraws
    virtual bool animating() const { return false; }

    // keeps receiving tick() while a modal screen (the choice picker) is on top of it: the video player's playback
    // supervision, progress and video frames go on behind the darkened picker
    virtual bool ticksUnderModal() const { return false; }

    bool isModal() const { return modal; }

protected:

    // wraps a job completion callback so it does nothing once this screen is gone
    template<typename F>
    std::function<void()> guarded(F f) {
        std::weak_ptr<bool> w = alive;
        return [w, f]() {
            if (w.lock()) {
                f();
            }
        };
    }

    // the same for menu / dialog choices (onChoice(index))
    template<typename F>
    std::function<void(int)> whileAlive(F f) {
        std::weak_ptr<bool> w = alive;
        return [w, f](int choice) {
            if (w.lock()) {
                f(choice);
            }
        };
    }

    void redraw();

    App &app;

private:

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    bool modal;
};

#endif // PS4IPTV_APP_SCREEN_H
