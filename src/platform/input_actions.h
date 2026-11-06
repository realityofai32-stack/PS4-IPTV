// User actions and their bindings on both input devices, the active device, and keyboard routing (host-testable).
//
// Every control hint in the app names an Action (ui::HintBar, inline "press ..." texts), never a controller
// button: the hint is drawn for the device the user is actually using - a DualShock 4 glyph (Cross, Circle, L1 ...)
// or a keyboard key cap (Enter, Esc, Tab ...). Each Action has exactly one entry in the binding table (binding()):
//   controller   the PadButton(s) the screens handle (a pair = [L1][R1] previous / next)
//   keyboard     the key(s) that perform it, or none (KeyKind::None: the hint is left out in keyboard mode)
//
// Keyboard routing (route()): screens keep handling PadButtons; a key that a screen does not take as text becomes
// the button of the action it stands for.
//   universal keys   always (they mean the same everywhere): arrows = d-pad, Enter = Cross, Esc / Backspace =
//                    Circle (a held Backspace never repeats), Page Up / Down = L2 / R2, Tab / Shift+Tab = R1 / L1
//   shortcut keys    letters, Space, Delete: ONLY when the screen on top declares an action bound to that key (its
//                    hint bars, ui::HintBar::setHints / setShortcuts), so a key does exactly what the visible hint
//                    says on that screen and never something hidden (F is Favorite where Favorite is offered; it
//                    never removes a Continue Watching card on Home, where that is Delete)
//   text keys        typed characters / Backspace / Space in a text field are taken by the screen first
//                    (KeyboardModel::applyKey); actions marked `text` are performed that way
//
// Active device (DeviceTracker): hints follow the device that produced the last real input event - a button
// press / repeat or a key event. Passive polling (an idle pad, a stick inside its deadzone, a keyboard that reports
// nothing) never switches; a keyboard that is disconnected switches back to the controller. Switching only happens
// on events, so the hints cannot flicker while nothing is pressed.

#ifndef PS4IPTV_PLATFORM_INPUT_ACTIONS_H
#define PS4IPTV_PLATFORM_INPUT_ACTIONS_H

#include <string>
#include <vector>

#include "input.h"
#include "keyboard_logic.h"

namespace actions {

    enum class Device {
        Controller,
        Keyboard
    };

    enum class Action {
        Confirm,        // select / open / OK / retry
        Back,           // back / cancel / close
        Navigate,       // move the focus
        Change,         // left / right: change a value
        Tabs,           // previous / next category, season, filter, tab, channel
        Page,           // page up / down
        Options,        // the screen's menu: Playback options (audio, subtitles, video), sort, Settings, ...
        Search,
        Details,        // open a title's detail page
        Favorite,       // add / remove a favorite
        Remove,         // remove from Favorites / Continue Watching
        DeleteItem,     // delete a profile
        Edit,           // edit a profile
        Download,
        PauseResume,    // pause / resume a download
        MoreOptions,    // a second menu on Triangle (Downloads)
        Refresh,        // rescan / refresh (Local Media)
        Restart,        // restart the diagnostics lines
        Info,           // the player's technical info
        PlayPause,      // the player
        Seek,           // the player: 10 s back / forward
        SeekMinute,     // the player: a minute back / forward
        NextEpisode,
        PreviousEpisode,
        PlayNow,        // the Next Episode card
        WatchCredits,   // the Next Episode card: cancel the automatic next episode for this playback
        Type,           // on-screen keyboard: type the focused key
        TextBackspace,  // on-screen keyboard / search: delete a character
        TextSpace,
        TextShift,
        TextDone,
        Count
    };

    enum class KeyKind {
        None,        // no keyboard equivalent (the hint is left out in keyboard mode)
        Named,       // an editing / navigation key (kbd::Key)
        Letter,      // a character key: letter a-z (or ' ' for Space), compared case-insensitively
        Typing       // typing characters on the keyboard itself
    };

    struct KeySpec {
        KeyKind kind = KeyKind::None;
        kbd::Key key = kbd::Key::Char;
        char letter = 0;
        bool shift = false;     // Shift+Tab
    };

    struct Binding {
        PadButton pad = PadButton::Count;     // Count = the d-pad (Navigate / Change / Seek)
        PadButton pad2 = PadButton::Count;    // the pair's second button ([L1][R1]), Count = single
        bool dpad = false;                    // the controller hint is the d-pad glyph
        KeySpec keys[2];                      // keyboard: the key(s) shown (keys[1]: the pair's second, or none)
        bool text = false;                    // performed by a text field's own key handling (not routed)
    };

    const Binding &binding(Action action);

    const char *actionName(Action action);    // for the log and the tests

    // the action has a keyboard key
    bool hasKeyboard(Action action);

    // a key matches a key spec (letters case-insensitively, Shift+Tab only with shift)
    bool matches(const KeySpec &spec, const kbd::Event &event);

    // the universal keys (see above); false: not a universal key
    bool routeUniversal(const kbd::Event &event, InputEvent &out);

    // the button a key stands for on a screen that declares `declared` actions: a universal key, else the first
    // declared action whose shortcut it is. false: the key means nothing there.
    bool route(const kbd::Event &event, const std::vector<Action> &declared, InputEvent &out);

    // the key cap text of a key spec ("Enter", "Esc", "Tab", "Shift+Tab", "PgUp", "F", "Space" ...); `space` is the
    // localized name of the space bar
    std::string keyLabel(const KeySpec &spec, const std::string &space = "Space");

    class DeviceTracker {
    public:
        Device active() const { return device; }

        // changes whenever the active device changes (hint bars rebuild)
        unsigned generation() const { return gen; }

        // a real controller event (press or repeat); true = the device changed
        bool controllerInput();

        // a real key event; true = the device changed
        bool keyboardInput();

        // the keyboard's connection state, polled every frame; a disconnect falls back to the controller
        bool keyboardConnected(bool connected);

    private:
        bool set(Device d);

        Device device = Device::Controller;
        unsigned gen = 0;
    };

    // the app's tracker (main thread)
    DeviceTracker &tracker();
}

#endif // PS4IPTV_PLATFORM_INPUT_ACTIONS_H
