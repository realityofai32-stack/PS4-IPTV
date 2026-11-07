# V7 IPTV - input hints, series Continue Watching, Next Episode card, Home hero artwork (v7-ux, v7-ux-2)

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

The Continue Watching threshold (45 s, above) and the visible card are separate. The card (`autonext::Countdown`,
`CARD_SECONDS` = 7) is shown whenever a valid next episode exists.

### v7-ux hardware bug: no card on the PS4

**Symptom.** Auto-play ON and a valid next episode, yet the episode reached its end with no card at all.

**Root cause.** The card only entered when `duration - time-pos <= 7` while playing. The end of file was only a
direct transition (`reachedEnd` → `startNext`), never a card. Measured with libmpv over HTTP, sampling `time-pos`
every 0.25 s as `Playback::pollStats` does:

| File | Remaining at `END_FILE` |
|---|---|
| clean MKV / TS | 0.1 s |
| MKV whose duration header is longer than its media (data ends early) | **8.2 s** |
| TS without a known duration | the card can never trigger |

So a file whose reported duration overshoots its real end by more than 7 s (the code already had to handle "files
whose position never reaches the duration") reaches EOF before the window opens. The card was skipped, and with
Auto-play ON the next episode started abruptly. The previous 45-second window always caught that overshoot, which
is why it never showed before.

### Fix

- **Post-play phase.** End of file before the card shows (or with no duration) shows the card after the end:
  - Auto-play ON: it counts its full 7 s from the end of file, then starts the next episode.
  - Auto-play OFF: it waits.
  - A card already counting when the file ends finishes its count after the end.
- **Auto-play OFF** is a manual card: the same card near the end and after the end, without the seconds or the bar,
  and never an automatic transition. Play now works.
- **Before the end:**
  - appears at ~7 s left;
  - counts its own 7 s (pause waits; buffering or the Options panel hide it and hold the count);
  - starts the next episode when the count runs out, without waiting for EOF.
- **Keys:** Play now (✕ / Enter) at once. Watch credits (○ / Esc) dismisses the card and disables auto-next for this
  playback. After the end the second action reads Cancel and opens the Up next panel (Play next / Back).
- **No card** for the final episode, or offline without the next episode downloaded.

### Diagnostics

No URLs, hosts or credentials are logged. Per episode:

- `next episode: <id> (SxxEyy, catalog | queue order)`, or `next episode: none - no Next Episode card for this
  episode (<reason>)`. The reason is one of: the series' final episode; offline and the next episode is not
  downloaded; not in the queue; catalog not in memory, no obvious successor.
- `end approach: position P of D s (duration from mpv | provider hint), R s left, state, recovery, options panel,
  next, auto-play -> card due | held | none`, logged once at ~10 s left.
- `next episode card shown: R s left, 7 s countdown | manual`.
- `end of file at P s, reported duration D s (from ..., X s short), next <id>, auto-play on/off: card <was shown |
  shown after the end ... | none: reason>`.

### Layout (unchanged)

`layout::player::NEXT_*`: 500 × 156 at the lower right (x 1324–1824, y 634–790).

- Card: near-black translucent, 1 px outline.
- Content: `NEXT EPISODE` with a small `7s`, then `S02E05 · title`, then the accent *Play now* pill and *Watch
  credits*, then a 3 px bar.
- It slides in 24 px over 0.22 s. Text only.
- It ends above raised (~815) and bottom (~925) subtitles.

## 4. Home hero artwork

**v7-ux** generated a blurred, darkened 16:9 background from the poster. On the hardware it looked bad, so it was
**removed** (generator, `ImageKind::HeroAmbient`, `Backdrop::setLevel`).

Final behaviour (`images/hero_art` `plan()`, `screens/home_screen.cpp`):

1. **A usable provider landscape backdrop.** Landscape ≥ 1.3:1 and ≥ 640 × 300. Unchanged handling: 1280 × 720,
   dimmed, strong left fade. A portrait poster or a thumbnail sent as `backdrop_path` is rejected.
2. **Otherwise the dark V7 hero**, with a subtle field of the poster's average colour and the **sharp portrait
   poster** on the right (`ImageKind::HeroPoster` 512 × 768, `ui::Backdrop` Poster art, the v3.1 style):
   - never stretched (ratio kept);
   - never drawn past 1.25× its texture;
   - under 400 px tall it is omitted and the clean dark hero stays.
3. **No artwork:** the clean dark hero.

No external artwork service, no generated or blurred image. While a backdrop is loading only the backdrop is
requested; a poster still in memory stays on screen meanwhile.

### v7-ux-6: the showcase poster (current)

Approved after the v7-ux-5 analysis (`images/hero_art`, `screens/home_screen.cpp`):

1. **A usable provider backdrop** (landscape ≥ 1.3:1, ≥ 640 × 300) is drawn full-bleed at about 42 % brightness:
   `Backdrop::setLevel(190, 110)` on Home only, versus about 24 % on detail pages. The 720 px eased left fade keeps
   the text column readable.
2. **Otherwise, the showcase:**
   - An ambient field fills the hero box under its fades (`Backdrop::setAmbient`). Its colour is the poster's average,
     40 % desaturated, scaled to 32 %, with no channel above 56, and never darker than the page.
   - The poster sits on the right as a separate object, above the fades, so it is never faded. It is at most 560 px
     tall and 420 px wide, never enlarged, right edge at x 1680, centred at y 395.
   - It has 12 px rounded corners (the poster tiles' proven technique), a soft shadow (black at alpha 120, 10 px
     down) and a 1.5 px frame. Brightness is 240 / 255.
   - Image choice: the detail cover (movies: `cover_big` / `movie_image`; series: the `get_series_info` cover) when
     it differs from the list image, else the list image (`stream_icon` / `cover`). The list image is shown while
     the cover loads, and stays when the cover fails.
   - A texture under 300 px tall is omitted: the ambient field alone.
3. **No artwork:** the clean dark hero.

Texture memory is unchanged: one `HeroPoster` texture (512 × 768 max, 1.5 MiB), or the backdrop (3.5 MiB). There is
no generation and no per-frame work.

### v7-ux-7: the tint stays around the poster

On the TV, the v7-ux-6 ambient field across the whole right half read as grey fog. The poster itself is unchanged.
The tint is now:

- **Subtler colour** (`images::hero::ambient`): only 20 % desaturated, so the hue stays and doesn't turn grey. It
  is scaled to 20 %, with no channel above 36. For example, red ≈ (31, 15, 12) and blue ≈ (14, 22, 31).
- **Localized** (`ambientArea`, `Backdrop::setAmbient`):
  - The solid part is exactly the poster's rectangle, hidden behind it.
  - It eases out (smoothstep, no edge) 220 px to the left (never past the text column at x 1096), 160 px to the
    right, and 80 px above and below. Page-coloured fades round the corners.
  - Including those fades it covers under 55 % of the hero box; the solid part is under 25 %.
- **No poster** (too small): no tint at all.

There are no textures, no blur and no per-frame work.

### v7-ux-11: Movie and Series details in the same language

The detail pages use the same provider-only artwork rules as Home (`screens::bindDetailArt`):

- **A usable landscape backdrop** is drawn on the right, from x 680 with a 700 px fade (it started at 560 before),
  at about 34 % brightness. That is brighter than the old 24 % and dimmer than Home's 42 %, because text runs over it.
- **Otherwise** the page stays clean dark, with the poster's colour as a subtle tint hugging the poster
  (`hero::ambientAround`). The tint stops 24 px before the text column. The dimmed "ghost poster" background is gone.
- **The poster / cover** is framed like Home's showcase: a soft shadow (alpha 120, 10 px down), 12 px corners and a
  1.5 px divider frame. Poster and average colours come from the existing image pipeline (`PosterLarge` / `Poster`
  averages).
- **Series episode rows** show the provider image only when it is a landscape still (≥ 1.3:1). The repeated portrait
  cover becomes a clean "E n" number tile.
- **Series synopsis:** while the episodes have the focus, the header's short text shows the focused episode's synopsis.

Layout, navigation, buttons, resume logic and hints are unchanged.

## 5. Hardware diagnostics

Each Home hero art change logs one line (no URL, host, credential or path):

```
[home] hero art: series, provider backdrop available no, usable -, fallback poster: source 600x900, texture
       512x768, no blur, drawn 533x800 (visible 493x800, 1.04x of the texture)
```

- `available` is `yes`, `no`, or `unknown (details pending)`.
- `usable` is `yes`, `no (failed to load)`, `no (portrait or too small)`, or `loading`.
- `fallback` is `backdrop`, `poster`, or `ambient-only` (the poster was too small and omitted).
- When nothing could be drawn, the line ends "- clean dark hero".

## 7. v7-ux-4: the card appears at 45 s, decides in 7 s

The appearance threshold and the decision countdown are two separate numbers:

- `autonext::CARD_SHOW_REMAINING` = 45 s: when the card appears. It is the same moment as the Continue Watching
  completion latch (`progress::COMPLETION_REMAINING`), so the series' Continue Watching entry has already moved on
  when the card shows.
- `autonext::CARD_SECONDS` = 7 s: the countdown, started as soon as the card appears.

With Auto-play ON and no input, the next episode starts about 38 s before the end. *Watch credits* dismisses the
card, disables auto-next for this playback, and lets the episode play to its end.

- A seek back beyond 46 s left hides the card; reaching 45 s again shows it with a fresh 7 s count.
- The EOF fallback (the card after the end, with 7 s from there) and the Auto-play OFF manual card are unchanged.

## 6. v7-ux-3: choice picker, one focus owner, Recently Watched removal

**Choice picker.**
- One component: `ui::ChoiceModel` holds the logic (host-tested); `screens::makeChoicePicker` draws it, in
  `screens/choice_picker.cpp`.
- It is a compact modal card (640 px wide, at most 7 rows of 72 px) over the scrim. The current value is checked
  and selected first.
- Up / Down move without wrapping and repeat when held. L2 / R2 page by 6. Cross / Enter confirms; Circle / Esc /
  OPTIONS cancels; a held Cross never confirms.
- Keyboard keys reach it through the universal routing. Its hint bar is owned by the picker, so DS4 / keyboard hints
  follow the active device.
- Settings: Cross on a Choice row with 3+ options opens it. Two-value rows and toggles stay inline, and Left / Right
  still step. The value applies after the picker closes, then Settings reloads the row (`onResume`).
- Playback options: Aspect, Crop, Zoom, Position X / Y and Subtitle size open it. The value applies at once through
  the same geometry / appearance path, with no reload.
- `App` ticks screens under a modal that ask for it (`Screen::ticksUnderModal`: the VOD player). Redraws count
  animating screens visible under modals, so the video keeps playing and drawing behind the picker.

**Focus.** `ui/focus_style.h` has three looks, mapped to existing theme tokens by `ui/focus_colors.h` (no colour
changes):

| State | Look |
|---|---|
| Focused | accent fill and/or accent ring or edge, the only "strong" look |
| Primary or selected, at rest | `accentMuted` with strong text, no ring |
| Secondary, at rest | neutral |

- Button, Chip, the navigation tabs, IconButton and RowFrame all use it.
- `Button`: the primary action used to keep the full accent fill while unfocused, so the hero Play looked focused
  while the navigation had focus. Now it is restrained at rest.
- `RowFrame`: a selected row (the open category of an unfocused list) gets a quiet `accentMuted` edge instead of the
  accent edge.
- Live TV's Watch button uses the primary-at-rest look while the channel row holds the focus.

**Recently Watched removal.**
- Remove is DS4 Square, or keyboard Del where Home offers it (the existing `A::Remove` action). On a Recently Watched
  card it calls `LibraryStore::removeFromRecentlyWatched`.
- It forgets the history entries of that card only: the channel or movie, or every history entry of the episode's
  series, so an older episode doesn't take the card's place.
- Progress, Continue Watching, favorites and downloads are untouched.
- Home copies the focused entry before rebuilding the row. The focus stays in place, clamped to the new last card.
  Repeats are ignored, so one press removes one card.

## Tests

The host suite has:

- `test_hints`
- `test_continue`
- `test_hero_art`: backdrop usability, the priority plan, and the async / stale / cache simulation.
- `test_ux_pass`: picker open / confirm / cancel / current-value highlight with DS4 and keyboard; one strong focus
  owner; primary-but-unfocused styling; Recently Watched removal of the first / middle / last card with DS4 and
  keyboard, nothing else deleted.
- `test_autonext`, with the 7 s card and the regressions:
  - Auto-play ON shows the card;
  - end of file before the window (the measured 8.2 s case);
  - unknown or inaccurate duration;
  - Auto-play OFF manual card;
  - no card without a next episode (final episode, offline not downloaded) and its log reasons.

`test_keyboard` and `test_layout` were updated. The keyboard fuzz target also routes keys through `actions::route`.
