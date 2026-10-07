// When the movie / episode player hides its overlay (host-testable).
//
// While playing, the HUD hides AUTO_HIDE_SECONDS after the last relevant activity: a button press, a seek,
// resuming from pause, or playback (re)starting. It stays while paused, while the Playback options panel
// is open, while a seek is pending and whenever the player is not plainly playing (opening, buffering,
// reconnecting, error, end): those states carry messages the user must see.

#ifndef PS4IPTV_PLAYER_HUD_LOGIC_H
#define PS4IPTV_PLAYER_HUD_LOGIC_H

namespace hud {

    const double AUTO_HIDE_SECONDS = 4.0;

    struct State {
        bool visible = false;
        bool playing = false;       // recovery status "playing" and the player is not paused
        bool paused = false;
        bool panelOpen = false;
        bool seekPending = false;
        bool finished = false;
        double lastActivity = 0;    // last input / seek / unpause / playback start
    };

    inline bool shouldAutoHide(const State &s, double now) {
        return s.visible && s.playing && !s.paused && !s.panelOpen && !s.seekPending && !s.finished
               && now - s.lastActivity >= AUTO_HIDE_SECONDS;
    }
}

#endif // PS4IPTV_PLAYER_HUD_LOGIC_H
