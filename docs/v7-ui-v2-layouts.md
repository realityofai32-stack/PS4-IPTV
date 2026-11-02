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
| Left rails | x 96..396 (300 wide), 52 px rows + 4 px gaps, 24 px text; main column starts at x 436 |
| Radii | 8 small (keys, tiles, rows), 12 cards / posters, 18 panels; pills only for buttons, tabs, chips |
| Type | 64 hero / detail title, 44 screen title, 32 section heading, 28 body, 24 card title + metadata, 22 captions |
| Fades | every gradient that meets artwork or video is 4 eased segments (smoothstep alpha), never one linear ramp |

Focus language (one per element type):

| Element | Rest | Focused |
|---|---|---|
| Poster / card | artwork dimmed to 82 %, quiet title | artwork 100 %, scale 1.04-1.06 around its centre, 4 px accent outline, title strong + metadata line |
| List row | no surface, 1 px divider | accent-tinted surface + 4 px accent edge on the left, strong text |
| Rail row (category) | quiet text; the open one: accent edge + primary semibold | accent-tinted surface + accent edge, strong text |
| Button | primary: accent fill; secondary: elevated pill | accent ring 6 px outside the pill |
| Tab / chip | text only (tabs) / elevated pill (chips); selected: accent tint | accent fill, on-accent caption |

## HOME

```
y 46 ┌ [V7] V7 IPTV   Home  Live TV  Movies  Series ........ (⌕)(☆)(↓)(⚙)  15:24 ┐ 118
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
| Navigation | y 46..118 (72 high). Brand: V7 symbol 34 px + "V7 IPTV" 34 px semibold at x 96 |
| Primary tabs | text tabs, 52 high, 22 px padding, 6 px apart, first at brand + 56. Current: strong semibold + 3 px accent underline; focused: accent pill; others: secondary text, no surface |
| Secondary actions | 4 round 52 px icon buttons, 12 px apart, transparent at rest (accent fill when focused), x 1470..1714; focused icon's name 22 px under it |
| Clock | 28 px semibold, right-aligned at 1824 |
| Notices | offline / HTTPS / expiry warning, 22 px, under the brand (only when there is one; no permanent status line) |
| Hero backdrop | poster layer x 600..1920, y 0..800 (page coordinates), cover-cropped from the 340 x 510 poster (anchor 18 % from the top), tint + shade; eased fades: left 640 (600..1240), bottom 300 (500..800), top 240 (under the navigation). No poster tile. |
| Hero text | x 96, max width 1000. Anchored on the buttons, stacked upwards: buttons y 548; progress bar 480 x 6 at y 504 with time 22 px to its right; meta 24 px secondary; episode line 28 px; title 64 px semibold (max 2 lines); eyebrow 24 px semibold accent text. One-line title: eyebrow at ~y 270; two lines: ~y 195 (navigation ends at 118) |
| Hero buttons | Resume 360 x 76 (primary), Details 250 x 76, 24 apart. Start state (no Continue Watching): Live TV / Movies / Series 250 each |
| Rails | first heading at y 688; pitch 420 (heading 56 + card 308 + gap 56) |
| Cards | 4 visible, 408 x 230 artwork (16:9) + title 24 px semibold + one 22 px secondary line; 32 px apart (4 x 408 + 3 x 32 = 1728). Progress bar inset 16 px at the bottom of the artwork |
| Card artwork | movies / episodes: the large poster (340 x 510) cover-cropped to 16:9 at its upper part (1.2x, sharp) - the grid poster cropped the same way until it arrives; channels: the 340 x 190 logo centred on a logo tile |
| Card focus | artwork scale 1.04 (+8 / +5 px), 4 px accent outline, artwork brightens, title strong |
| Page scroll | focused rail's lowest text edge at y 958: rail 0 = 94, rail 1 = 514, rail 2 = 934. Once scrolled: solid band 0..124 + eased fade 124..208 under the navigation |
| Rest view | rail 0 artwork y 744..872 visible, then fades under the hint band (signals more content) |
| Favorites | landscape cards of real favorites (channels; movies / series whose catalog is in memory). None loadable: one quiet line, not focusable. No "See all" tile (the navigation's Favorites button opens the full list) |

## MOVIES / SERIES (one template)

```
y 54   [V7] Movies   All movies  19,826 · Provider order ............... Refreshing…
y 150  Rail 96..396 │ Grid 436..1824: 7 x 166 x 249 posters, 34 px apart
       ▌All movies   │ [poster][poster][poster][poster][poster][poster][poster]   row 1 y 150
        Recently add.│  Title    Title   ...                                             (title y 415)
        Continue…  2 │ [poster][poster] ...                                        row 2 y 505
        ★ Favorites 0│
                     │ [poster][poster] ... (peek row, fades out under the hints)  row 3 y 860
```

| Item | Value |
|---|---|
| Header context | open category in 28 px semibold primary + count and order in 24 px secondary, on the header line; catalog status (refreshing / saved list / error) right-aligned at 1824 |
| Category rail | x 96..396, y 150..930 (14 rows of 52 + 4) |
| Rail states | rest: 24 px, muted when the grid has focus, secondary when the rail has focus; open category: 4 px accent edge + primary semibold; focused: accent-tinted surface + edge, strong; counts 22 px muted |
| Grid | x 436..1824 (1366 + 22 px scrollbar gutter), y 150; 7 columns x 166 px, gap (1366 - 7 x 166) / 6 = 34 |
| Cell | poster 166 x 249 (2:3), title 24 px at +16, metadata 22 px at +48 (focused cell only); cell 325, row gap 30, pitch 355 |
| Rows | 2 whole rows (y 150..475, 505..830) + a peek row at y 860 (about 110 px visible before the fade); 14 full + 7 partial posters |
| Density vs v1 | v1: 24 posters of 140 x 210; v2: 14 + 7 peeking of 166 x 249 (+41 % area each), gaps 34 / 30 instead of 19 / 36 |
| Focus | scale 1.06 around the centre (+5 / +7.5 px), 4 px outline, artwork 100 % (others 82 %), title strong semibold, metadata line (year · rating · progress) |
| Image | the 140 x 210 grid poster drawn at 166 x 249 (1.19x bilinear) - see uncertainties |

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
| Backdrop | x 560..1920, y 0..1080, cover-cropped poster (anchor 18 %), eased fades: left 700 (560..1260), bottom 440, top 200 |
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
| Backdrop | x 560..1920, y 0..680, eased fades: left 700, bottom 320 (360..680), top 200 - the episodes sit on clean background |
| Season chips | y 414, 48 high, width = text + padding (min 150), 12 apart |
| Episodes | y 486..954: 4 rows of 108 + 12 gaps. Row: 160 x 90 still (cover-cropped, the series cover when the provider repeats it), title 28 px, "S02E02 · 58 min · 1080p" 22 px secondary, right: position + 240 px bar or state (✓ watched / ↓ downloaded / ↓ 42 %) |
| Row states | rest: no surface, divider; focused: accent-tinted surface + accent edge, title semibold strong |
| Information height | header area 54..462 = 408 px (v1: 54..540 = 486 px) |

## LIVE TV

| Item | Value |
|---|---|
| Header context | open category + channel count on the header line; playlist notices after it |
| Categories | rail x 96..396, y 150..958 (rows 52 + 4) |
| Channels | x 436..1336 (878 + scrollbar), y 150..948: 10 rows of 78 + 2. Number 22 px muted right-aligned in 56; logo tile 96 x 56 at x 72; name 28 px at x 192; ★ right. Rest: no surface, divider from x 72; focused: accent-tinted surface + edge, name semibold strong, number primary |
| Channel panel | x 1376..1824 (448), y 150..750 (600, compact): logo tile 384 x 216 (padding 32), name 34 px (2 lines), category 24 px secondary, "Channel 12" 24 px muted, ★ in favorites, Watch pill 384 x 64 at the bottom (accent while the channel list has focus) |

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
| Categories | rail x 96..396, y 150, rows 60 + 4, 26 px |
| Section heading | category name 32 px semibold at x 436, y 150 |
| Settings | x 436..1296, y 214..958: rows of 72 without gaps, divider lines; caption 28 px, value 26 px semibold right-aligned, compact switch 64 x 34 |
| Description | no card: thin vertical rule at x 1328 (214..958), title 26 px semibold + text 24 px secondary at x 1360..1824 |
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
