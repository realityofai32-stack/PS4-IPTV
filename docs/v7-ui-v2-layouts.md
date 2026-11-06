# V7 IPTV UI v2 - 1920 x 1080 layout specifications

All values are screen pixels at 1920 x 1080 (the PS4 framebuffer = UI coordinates). They live in
`src/ui/layout.h` (one namespace per screen) and `tests/host/test_layout.cpp` checks the invariants: inside the
TV-safe area, no overlapping blocks, grids and rails add up to their widths, row counts as specified.

The PS4 renderer cannot run on the PC, so these numbers predict the picture; the hardware checklist
(`docs/checkpoint-v7-ui-2.txt`) verifies it. Compositions are kept simple enough to predict: rectangles, text,
cached textures, eased gradients.

## Global frame

| Item | Value |
|---|---|
| Safe area | x 96..1824, y 54..1026 (5 %) |
| Content width | 1728 |
| Hint bar | y 986..1026, x from 96, 4-5 hints, one line |
| Lowest content edge | y 960 |
| Sub-page header | `[V7 30 px] Title 44 px` at y 54 (~54..108); context text on the same line, 36 px after the title; status right-aligned at 1824 |
| First content row | y 150 |
| Scrolling pages | eased fade y 872..968 into the background, solid band 968..1080 behind the hints |
| Left rails | x 96..426 (330 wide), 52 px rows + 4 px gaps, 24 px text; main column starts at x 462 |
| Radii | 8 small (keys, tiles, rows), 12 cards / posters, 18 panels; pills only for buttons, tabs, chips |
| Type | 64 hero / detail title, 44 screen title, 32 section heading, 28 body, 24 card title + metadata, 22 captions |
| Fades | every gradient that meets artwork or video is 4 eased segments (smoothstep alpha), never one linear ramp |

Focus language (one per element type):

| Element | Rest | Focused |
|---|---|---|
| Poster (grid) | artwork dimmed to 82 %, quiet title | artwork 100 %, scale 1.04 around its centre, 4 px accent outline, title strong + metadata line |
| Home media card (v3) | neutral card surface, artwork dimmed to 82 % | raised neutral surface, accent ring 5 px outside the card, artwork 100 %, no scaling |
| List row | no surface, 1 px divider | accent-tinted surface + 4 px accent edge on the left, strong text |
| Rail row (category) | quiet text; the open one: accent edge + primary semibold | accent-tinted surface + accent edge, strong text |
| Button | primary: accent fill; secondary: elevated pill | accent ring 6 px outside the pill |
| Tab / chip | text only (tabs) / elevated pill (chips); selected: accent tint | accent fill, on-accent caption |

## HOME

```
y 46 ┌ [V7]   Home  Live TV  Movies  Series ........ (⌕)(☆)(↓)(⚙)  15:24 ┐ 118
     │                                                      ░░░░ poster layer ░░░│
     │ CONTINUE WATCHING                                    ░ x 600..1920 ░░░░░░░│
     │ The Walking Dead: Daryl Dixon                        ░ y 0..800  ░░░░░░░░░│
     │ S02E02 · Episode 2                                   ░ eased fades ░░░░░░░│
     │ Series · 15 min left                                 ░░░░░░░░░░░░░░░░░░░░░│
     │ ━━━━━━━━━━━━━━━━━━━──────────  42:31 / 57:36 (y 504)                       │
     │ [ ▶ Resume 42:31 ]  [ Details ]                         (y 548..624)       │
y688 │ Continue Watching                                                          │
y744 │ [ 408 x 230 ]  [ 408 x 230 ]  [ 408 x 230 ]  [ 408 x 230 ]   (peek, fades)   │
```

| Item | Value |
|---|---|
| Navigation | y 46..118 (72 high). Brand: the V7 symbol alone, 34 px high at x 96 (the "V7 IPTV" name next to it was removed after the v0.3.0 hardware test) |
| Primary tabs | text tabs, 52 high, 22 px padding, 6 px apart, first at the symbol + 56. Current: strong semibold + 3 px accent underline; focused: accent pill; others: secondary text, no surface |
| Secondary actions | 4 round 52 px icon buttons, 12 px apart, transparent at rest (accent fill when focused), x 1470..1714; focused icon's name 22 px under it |
| Clock | 28 px semibold, right-aligned at 1824 |
| Notices | offline / HTTPS / expiry warning, 22 px, under the navigation (only when there is one; no permanent status line) |
| Hero backdrop (v3.1) | layer x 600..1920, y 0..800 (page coordinates, a flat `#050608` page under it), tint + shade; eased fades: left 720 (600..1320), bottom 300 (500..800), top 240 (under the navigation). One large texture at a time. Real landscape backdrop (provider `backdrop_path`, 1280 x 720 exact-size texture, 3.52 MiB): cover-cropped, 1.11x. Otherwise the hero poster (512 x 768 texture box, never enlarged on the CPU) at its own ratio, top at the screen top, right edge 40 px past the screen: wanted 800 tall but never more than 1.25x its texture and never wider than 560 (512 x 768 -> 533 x 800 at 1.04x; 400 x 600 -> 500 x 750; 300 x 450 -> 375 x 562; under 400 px tall: not drawn). Left fade over half its visible width, bottom fade over 30 % of its height, both into its colour field: the poster's average colour as drawn, dimmed (`fit::ambientWash`, <= 34 per channel), solid behind / right of the poster, fading out over 420 px but never left of x 1096 (the text column stays black). (`ui::Backdrop::PosterStyle`, `fit::posterArt`.) Never stretched. |
| Hero text | x 96, max width 1000. Anchored on the buttons, stacked upwards: buttons y 548; progress bar 480 x 6 at y 504 with time 22 px to its right; meta 24 px secondary; episode line 28 px; title 64 px semibold (max 2 lines); eyebrow 24 px semibold accent text. One-line title: eyebrow at ~y 270; two lines: ~y 195 (navigation ends at 118) |
| Hero buttons | Resume 360 x 76 (primary), Details 250 x 76, 24 apart. Start state (no Continue Watching): Live TV / Movies / Series 250 each |
| Rails | first heading at y 688; pitch 363 (heading 56 + card 255 + gap 52) |
| Cards (v3) | 3 visible landscape media cards, 552 x 255, 36 px apart (3 x 552 + 2 x 36 = 1728). Left: the whole poster 170 x 255 (30.8 %); right, inset 28: title 28 px semibold (2 lines), episode / kind 24 px, time left / year 22 px, 6 px progress bar at the bottom (in-progress items) |
| Card artwork (v3) | movies / episodes / series: the Poster texture (170 x 255) drawn 1:1, `fit::Mode::Poster` (no 16:9 crop); channels: the 340 x 190 logo variant contained (`fit::Mode::Logo`, 22 px inset, <= 1.6x) in a 170 x 255 neutral logo area |
| Card focus | see the focus table: raised neutral surface + accent ring, no scaling (the poster stays 1:1) |
| Page scroll | focused rail's lowest text edge at y 958: rail 0 = 94, rail 1 = 514, rail 2 = 934. Once scrolled: solid band 0..124 + eased fade 124..208 under the navigation |
| Rest view | rail 0 artwork y 744..872 visible, then fades under the hint band (signals more content) |
| Favorites | landscape cards of real favorites (channels; movies / series whose catalog is in memory). None loadable: one quiet line, not focusable. No "See all" tile (the navigation's Favorites button opens the full list) |

## MOVIES / SERIES (one template)

```
y 54   [V7] Movies   All movies  19,826 · Provider order ............... Refreshing…
y 150  Rail 96..426 │ Grid 462..1824: 7 x 166 x 249 posters, ~30 px apart
       ▌All movies   │ [poster][poster][poster][poster][poster][poster][poster]   row 1 y 150
        Recently add.│  Title    Title   ...                                             (title y 415)
        Continue…  2 │ [poster][poster] ...                                        row 2 y 505
        ★ Favorites 0│
                     │ [poster][poster] ... (peek row, fades out under the hints)  row 3 y 860
```

| Item | Value |
|---|---|
| Header context | open category in 28 px semibold primary + count and order in 24 px secondary, on the header line; catalog status (refreshing / saved list / error) right-aligned at 1824 |
| Category rail | x 96..426, y 150..930 (14 rows of 52 + 4); names 24 px, counts 22 px muted |
| Rail states | rest: 24 px, muted when the grid has focus, secondary when the rail has focus; open category: 4 px accent edge + primary semibold; focused: accent-tinted surface + edge, strong; counts 22 px muted |
| Grid | x 462..1824 (1340 + 22 px scrollbar gutter), y 150; 7 columns x 166 px, gap (1340 - 7 x 166) / 6 = 29.7 |
| Cell | poster 166 x 249 (2:3), title 24 px at +16, metadata 22 px at +48 (focused cell only); cell 325, row gap 30, pitch 355 |
| Rows | 2 whole rows (y 150..475, 505..830) + a peek row at y 860 (about 110 px visible before the fade); 14 full + 7 partial posters |
| Density vs v1 | v1: 24 posters of 140 x 210; v2: 14 + 7 peeking of 166 x 249 (+41 % area each), gaps 30 / 30 instead of 19 / 36; rail 330 instead of 400 |
| Focus | scale 1.04 around the centre (+3.3 / +5 px), 4 px outline, artwork 100 % (others 82 %), title strong semibold, metadata line (year · rating · progress) |
| Image (v3) | the 170 x 255 Poster texture (same 256 x 256 GPU page as v2's 140 x 210) drawn at 166 x 249 (0.98x), 173 x 259 focused (1.02x) |

## MOVIE DETAIL

```
[V7] Movies                                              ░░ backdrop x 560..1920 ░░
┌────────┐  TITLE (64, max 2 lines)                       ░░ eased left fade 700 ░░
│ poster │  2026 · 1h 51m · ★ 8.3 · 1080p                 ░░ bottom fade 640..1080 ░
│320x480 │  Genre                                         ░░░░░░░░░░░░░░░░░░░░░░░░░
│        │  Plot, 28 px, max 4 lines, 1040 wide
└────────┘  42 min left · stopped at 1:02:15  ━━━━━━──
            [ ▶ Resume 1:02:15 ] [ Start over ] [ ↓ Download ] [ ☆ Favorite ]
            Director · Cast (24 px muted)      H.264 · AAC 5.1 · MKV (22 px muted)
```

| Item | Value |
|---|---|
| Breadcrumb | V7 symbol 26 px + "Movies" 24 px semibold secondary at y 54 |
| Backdrop | x 560..1920, y 0..1080: the real backdrop once get_vod_info has one (cover), else the poster at its own ratio against the right edge (v3); eased fades: left 700 (560..1260), bottom 440, top 200 |
| Poster | x 96, y 160, 320 x 480, radius 12 (large poster, downscaled = sharp) |
| Text column | x 480, width 1344; plot width 1040 |
| Stack | title y 160 (64 px; 44 px when longer than 1.6 lines), facts 28 px secondary, genre 24 px muted, plot 28 px primary, progress line + 480 px bar, buttons (y >= 540), people 24 px muted, technical 22 px muted |
| Buttons | primary 360 x 76, others 270 x 76, 24 apart: at most 360 + 3 x 270 + 3 x 24 = 1242 < 1344 |

## SERIES DETAIL

```
[V7] Series
┌──────┐ TITLE (44)                                          ░ backdrop x 560..1920, y 0..680 ░
│cover │ 2024 · ★ 8.1 · Drama · 3 seasons (28)               ░ bottom fade 360..680 ░░░░░░░░░░
│180x  │ Plot, 24 px, 2 lines, 1100 wide
│ 270  │ [ ▶ Resume S02E02 · 42:31 ]  [ ☆ Favorite ]         (y 318..382)
└──────┘
( Season 1 ) ( Season 2 ) ( Season 3 )                         y 414..462
[160x90] Episode title                          42:31 / 57:36  y 486  row 108
         S02E02 · 58 min · 1080p                ━━━━━━━──
──────────────────────────────────────────────────────── (divider)
[160x90] ...                                                   4 rows: 486..954
```

| Item | Value |
|---|---|
| Cover | x 96, y 112, 180 x 270 |
| Text | x 324, width 1500: title 44 px (1 line) y 108, facts 28 px secondary, plot 24 px 2 lines (1100 wide) |
| Buttons | y 318..382 (64 high, bottom-aligned with the cover): Resume 470, Favorite 300 |
| Backdrop | x 560..1920, y 0..680: real backdrop from get_series_info when present, else the poster (v3); eased fades: left 700, bottom 320 (360..680), top 200 - the episodes sit on clean background. Cover 180 x 270 from the 170 x 255 Poster texture (v3) |
| Season chips | y 414, 48 high, width = text + padding (min 150), 12 apart |
| Episodes | y 486..954: 4 rows of 108 + 12 gaps. Row: 160 x 90 still (cover-cropped, the series cover when the provider repeats it), title 28 px, "S02E02 · 58 min · 1080p" 22 px secondary, right: position + 240 px bar or state (✓ watched / ↓ downloaded / ↓ 42 %) |
| Row states | rest: no surface, divider; focused: accent-tinted surface + accent edge, title semibold strong |
| Information height | header area 54..462 = 408 px (v1: 54..540 = 486 px) |

## LIVE TV

| Item | Value |
|---|---|
| Header context | open category + channel count on the header line; playlist notices after it |
| Categories | rail x 96..426, y 150..958 (rows 52 + 4) |
| Channels | x 462..1342 (858 + scrollbar), y 150..948: 10 rows of 78 + 2. Number 22 px muted right-aligned in 56; logo tile 96 x 56 at x 72; name 28 px at x 192; ★ right. Rest: no surface, divider from x 72; focused: accent-tinted surface + edge, name semibold strong, number primary |
| Channel panel | x 1382..1824 (442), y 150..750 (600, compact): logo tile 378 x 212 (padding 32), name 34 px (2 lines), category 24 px secondary, "Channel 12" 24 px muted, ★ in favorites, Watch pill 384 x 64 at the bottom (accent while the channel list has focus) |

## SEARCH

```
[V7] Search   Searching 19,838 movies · 2,364 series · 573 channels
[ ⌕  type a title or channel ............................................. ] y 140..220 (full width)
┌ keyboard 96..712 ┐   ( All 412 ) ( Movies 300 ) ( Series 80 ) ( Live TV 32 )   y 252
│ 12 units x 44    │   412 results for "dixon"                                    y 318
│ 5 rows x 60      │   [img] Title                                              y 360
│ y 252..584       │         Movie · 2019 · ★ 7.1                               6 rows of 92
└──────────────────┘   ...                                                       ..932
```

| Item | Value |
|---|---|
| Field | x 96..1824, y 140..220, search icon + query 34 px; accent outline while typing |
| Keyboard | x 96..712 (12 x 44 + 11 x 8 = 616), y 252..584 (5 x 60 + 4 x 8); keys radius 8; dims while the results have focus |
| Results column | x 760..1824 (1064): filter chips y 252 (48 high, fitted widths), summary 24 px at y 318, list y 360..932 (6 rows of 92 + 4) |
| Result row | poster 56 x 84 / logo 96 x 54, title 28 px semibold, "Movie · 2019 · ★ 7.1" 24 px secondary; rest: divider; focused: surface + edge |
| Empty query | compact left-aligned instructions at the top of the results column (34 px title + 28 px text), no centred void |

## SETTINGS

| Item | Value |
|---|---|
| Categories | rail x 96..426, y 150, rows 60 + 4, 26 px |
| Section heading | category name 32 px semibold at x 462, y 150 |
| Settings | x 462..1302, y 214..958: rows of 72 without gaps, divider lines; caption 28 px, value 26 px semibold right-aligned, compact switch 64 x 34 |
| Description | no card: thin vertical rule at x 1334 (214..958), title 26 px semibold + text 24 px secondary at x 1366..1824 |
| Accent palette | 3 rows x 3 presets (V7 Blue Cyan Teal / Green Purple Magenta / Red Orange White): each cell 1/3 of the row, swatch + name, ✓ and a white ring on the active one; D-pad moves inside the grid, X applies, Left on the first column returns to the categories |

## DOWNLOADS

| Item | Value |
|---|---|
| Header context | "0 B downloaded · 347 GB free" on the header line |
| Tabs | compact chips (48 high, fitted width) at y 140, 12 apart |
| List | y 214..958; rows separated by dividers, focused row surface + edge; progress bars unchanged |

## PLAYBACK HUD / OPTIONS

| Item | Value |
|---|---|
| Top | eased fade 0..300 (no solid band), title 44 px at y 60, episode line 28 px at y 120, clock right |
| Bottom | eased fade 700..1080, progress bar 6 px at y 894 (thumb 20), time 28 px semibold + track info right at y 918, hints at y 986: Pause, D-pad seek, Options, (Next episode), Back |
| Options panel | x 1240..1920, eased 80 px left edge; title 44 px at x 1288, y 70; list x 1288..1824 (inside the safe area), rows 80 + 4; selected row: surface + accent edge; chevrons 20 px inside the row's right edge |
