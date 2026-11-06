# V7 IPTV - input hints, series Continue Watching, Next Episode card, Home hero artwork

Branch `feature/v7-next-features` (local only). Four issues found on the PS4:

1. keyboard users still saw DualShock 4 hints;
2. a series could disappear from Continue Watching;
3. the 45-second Next Episode overlay was too intrusive;
4. the Home hero looked poor when the provider has no landscape backdrop.

## 1. Hints follow the active input device

`src/platform/input_actions.{h,cpp}` (host-tested) is the one table of user actions (`actions::Action`). Each action
has:

- a controller binding (`PadButton`, a pair such as L1/R1, or the d-pad);
- a keyboard binding (`KeySpec`: a named key, a letter, typing, or none);
- its hint, derived from those bindings: a DS4 glyph or a key cap, drawn by `ui::HintBar` / `ui::actionGlyph`.

Screens name actions, never glyphs: `{A::Remove, tr("home.cw_remove")}`. The host tests fail if a screen still names
`ui::Glyph::`, or names an action without a keyboard key (`hints_every_screen_action_has_a_keyboard_key`).

| Action | DS4 | Keyboard | Routed |
|---|---|---|---|
| Confirm / Details / Play now | ✕ | Enter | always |
| Back / Watch credits | ○ | Esc (Backspace too) | always |
| Navigate / Change / Seek 10 s | D-pad | arrows | always |
| Tabs (category, season, filter, tab, channel) | L1 / R1 | Tab / Shift+Tab | always (new) |
| Page / Seek 1 min | L2 / R2 | PgUp / PgDn | always |
| Previous episode | L1 | Shift+Tab | always (new) |
| Options (Playback options = audio / subtitles / video, Settings, sort, menus) | OPTIONS | O | where offered (new) |
| Search | △ | S | where offered (new) |
| Favorite | □ | F | where offered (new) |
| Remove (Favorites, Continue Watching) | □ | Del | where offered (new) |
| Delete (a source) | △ | Del | where offered (new) |
| Edit (a source) | □ | E | where offered (new) |
| Download (an episode) | □ | D | where offered (new) |
| Pause / resume (Downloads) | □ | P | where offered (new) |
| More options (Downloads) | △ | O | where offered (new) |
| Refresh (Local Media) | △ | R | where offered (new) |
| Info (player) | △ | I | where offered (new) |
| Play / Pause (player) | ✕ | Space (Enter too) | where offered (new) |
| Next episode (player) | R1 | N (Tab too) | where offered (new) |
| On-screen keyboard: type / delete / space / done | ✕ □ △ R2 | typing / Backspace / Space / Enter | text field |
| On-screen keyboard: shift | L2 | (the keyboard's own Shift) | not shown in keyboard mode |

The keyboard is routed in two ways:

- **Universal keys** mean the same on every screen. Tab / Shift+Tab are new; `KeyboardModel` now passes Tab on,
  so it also switches Search's filter while typing.
- **Shortcut keys** route only when the screen on top *declares* the action. Each hint bar has an owner screen
  (`HintBar::setOwner`); its shown actions plus `setShortcuts()` are that screen's shortcuts (`HintBar::declared`
  → `actions::route`). A key therefore does exactly what the visible hint says. F is Favorite where Favorite is
  offered, but it never removes a Continue Watching card on Home, where that action is Del. Screens never see a
  hidden alias. The tests also check that no screen offers one key for two different buttons.

**Active device** (`actions::DeviceTracker`):

- The last real input event wins: a controller press or repeat, or a key event.
- Passive polling never switches devices. That covers an idle pad, a stick inside its deadzone or hysteresis, and
  an idle keyboard.
- Unplugging the keyboard falls back to the controller.
- Switching happens only on events, so the hints cannot flicker.

On a switch, `App` redraws every hint bar (`HintBar::refreshAll`) and calls `Screen::onInputDeviceChanged()`.
Screens that embed key names in text then set that text again. These texts are now parameterised with
`screens::keyName(action)`: the empty-Favorites texts, "Press {0} to try again", the Downloads empty state, the
accent picker and Local Media's "press {0}". Settings descriptions became device-neutral ("in Playback options").
The player's end and retry panels and the Live TV *Watch* button use action glyphs.

## 2. Series Continue Watching

**Root cause.** Continue Watching lists progress entries that are *in progress* (≥ 5 s and not watched), one card
per series. That caused three failures:

- An episode counted as watched at **93 %**. For a 45-minute episode that is the last ~3 minutes. Exiting during the
  credits, after "Cancel", or with auto-play off marked it watched.
- Nothing pointed at the next episode until that episode had played **5 s** itself.
- Auto-next recorded the episode as complete and started the next one. Leaving the next one within its first
  seconds (< 1 s writes nothing, < 5 s is not "in progress") left the series with no eligible entry.

In all three cases the series vanished.

**Fix.**

- `progress::isWatched(position, duration, type)` and `inProgress(entry)`: a series episode is complete only in its
  last `COMPLETION_REMAINING` = 45 s. Movies keep 93 %. Exiting with more than 45 s left keeps the exact episode and
  position.
- `autonext::CompletionLatch`: once remaining ≤ 45 s (after ≥ 5 s of playback), the episode is latched as complete
  for this playback, and `VodProgress::setCompleted` makes every later save "watched".
- At that same moment `autonext::persistUpNext` → `LibraryStore::markUpNext` makes the provider's next episode
  (`nextIndex`: catalog order, season transitions, non-contiguous numbers, offline only if downloaded) the series'
  Continue Watching entry at position 0.
  - The entry is flagged `upNext`. That flag is optional in `history.json`; older versions just see a position-0
    entry.
  - It is not added to Recently Watched.
  - It clears any older pointer of that series and ends a removal of it.
- The pointer is persisted when the threshold is crossed, not at EOF, so exit, EOF, Auto-play off, Watch credits
  and cancel all keep it.
- If the next episode is only known later (the catalog arrives after the latch), the pointer is set then. Without a
  valid next episode, nothing is created.
- Seeking back beyond 46 s left releases the latch: the episode is in progress again.
- Opening the pointed episode and leaving it before real progress (5 s) keeps the pointer (`updateProgress`). Real
  progress replaces it.
- **Never backwards** (`advancesContinue`): replaying an older episode does not move the series' entry back, and a
  next episode already in progress keeps its position.
- Series Detail's main button follows the same entry ("Next S02E05"). Home shows "Next episode" with no progress
  bar, and the hero button reads "Play S02E05".

## 3. Next Episode card

The Continue Watching threshold (45 s, above) and the visible card are separate. The card
(`autonext::Countdown`, `CARD_SECONDS` = 7):

- appears at ~7 s left;
- counts its own 7 seconds of playback time (paused: the count waits; buffering or the Options panel: hidden, the
  count held);
- then starts the next episode without waiting for EOF;
- *Play now* (✕ / Enter) starts it at once;
- *Watch credits* (○ / Esc) dismisses it and disables auto-next for this playback;
- has an EOF fallback, and exactly one transition.

Layout (`layout::player::NEXT_*`): 500 × 156 at the lower right (x 1324–1824, y 634–790).

- Card: near-black translucent `overlayPanel`, a 1 px divider outline, `RADIUS` corners.
- Content: `NEXT EPISODE` with a small `7s`, then `S02E05 · title` (one line), then an accent *Play now* pill with
  its glyph and a quiet *Watch credits*, then a 3 px accent bar that fills smoothly.
- It slides in 24 px over 0.22 s (ease-out). No flashing, no scaling. It is text only: no artwork is fetched.
- It ends at y 790, above raised subtitles (~815) and bottom subtitles (~925), right of the centre.

It is never shown for the final episode, with Auto-play off, or offline when the next episode is not downloaded.

## 4. Home hero artwork

**Before:** the provider backdrop (1280 × 720) if `get_*_info` had one; otherwise the portrait HeroPoster (512 × 768)
against the right edge, never enlarged past 1.25×, on a field of its average colour.

**Now** (`images/hero_art.{h,cpp}`, `hero::plan`, `screens/home_screen.cpp`):

1. **The provider backdrop**, when it loads and is usable: landscape (≥ 1.3:1) and ≥ 640 × 300. A portrait poster or
   a thumbnail sent as `backdrop_path` is rejected. Drawn exactly as before.
2. **A generated background** (`ImageKind::HeroAmbient`), made once on a worker from the poster or cover and cached
   like any texture (the last 3 stay in memory). Two modes:
   - **Blurred** (portrait, square, small or very tall posters):
     1. a 16:9 crop (a band centred at 32 % of a taller image, the centre of a wider one);
     2. area-averaged to 160 × 90;
     3. three box-blur passes of radius 4 (≈ Gaussian σ 4.5 → ~37 px on the 1320 px hero);
     4. desaturated 35 %, darkened to 0.82, and darker towards the left (0.45 → 1 over 55 %);
     5. enlarged to **640 × 360** with a 4 × 4 ordered dither, so dark gradients do not band on the TV.
   - **Wide cover**: a cover that is itself a large wide image (≥ 960 px, ≥ 1.5:1) is shown sharp, cropped to 16:9,
     at most 1280 × 720, never enlarged on the CPU.
3. **Ambient only**: when the poster's shorter side is under 80 px, a clean dark 320 × 180 wash of its average
   colour is drawn, with no picture.
4. **No artwork, or an image that fails to decode:** the plain dark hero.

Every hero layer goes through `ui::Backdrop` Landscape: dimmed, shaded, with the strong eased fade into the page on
the left. The portrait poster is no longer pasted on the right. Detail pages are unchanged.

**Cost**, measured on the host (`hero_art_generation_cost`, -O2):

| Input | Output | Texture | Time |
|---|---|---|---|
| Blurred: 400×600 to 1280×1280 posters | 640 × 360 | 900 KiB | 6.8 – 8.7 ms |
| Wide cover: 1920 × 1080 | 1280 × 720 | 3.5 MiB | 26.7 ms |
| Ambient: 60 × 90 | 320 × 180 | 225 KiB | 1.5 ms |

The console's Jaguar CPU is several times slower. Generation runs on a job worker; the main thread only uploads the
finished texture, as for any poster. Each log line gives the generation time measured on the console. Transient
memory is the poster fitted into 1280 × 1280 (≤ 6.25 MiB RGBA) plus 170 KiB of float buffers. The hero still holds
one large texture.

**Async / cache.**

- `bindHeroImages` always asks the loader for the *current* hero's URLs, so a late result for an earlier hero can
  never be shown.
- Entering Home again hits the cache.
- `onPause` withdraws the requests; a job already running finishes into the cache.
- Simulated in `hero_art_cache_stale_results_and_repeated_home_visits`.

## 5. Hardware diagnostics

Each Home hero art change logs one line with no URL, host, credential or path. Example:

```
[home] hero art: series, provider backdrop available no, usable -, fallback poster (blurred poster): source
       600x900, generator input 600x900, crop 16:9 band 600x338, blur yes, generated texture 640x360 in 31.0 ms,
       drawn 1422x800 (visible 1320x800, 2.22x)
```

`available` is `yes`, `no`, or `unknown (details pending)`. `usable` is `yes`, `no (failed to load)`,
`no (portrait or too small)`, or `loading`. `fallback` is `backdrop`, `poster` or `ambient-only`. When nothing
could be drawn, the line ends "- plain dark hero".

The loader also logs `[images] hero background <hash> generated: <mode>, crop ... -> WxH texture (KiB) in X ms on a
worker`. Hardware tests can then tell a bad or missing provider backdrop from a wrong choice by V7.

## Tests

The host suite gains `test_hints`, `test_continue` and `test_hero_art`. `test_autonext` was rewritten for the 7 s
card. `test_keyboard` and `test_layout` were updated. The keyboard fuzz target now also routes every decoded key
through `actions::route` with random declared sets.
