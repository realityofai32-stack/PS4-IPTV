# V7 IPTV UI v2 - hardware screenshot audit of v7-ui-1

Source: 11 physical PS4 screenshots of checkpoint `v7-ui-1` (build `9a3061e`, Turkish UI, V7 Blue), stored in
`docs/v7-ui-hardware/v1/`. Coordinates below are measured on the 1920 x 1080 captures. These captures are the
authority; code and coordinates were only used to explain what the captures show.

| File | Screen |
|---|---|
| `V7 IPTV_20261102152459.jpg` | Home, first view (Settings icon focused) |
| `V7 IPTV_20261102152528.jpg` | Home, scrolled to Favorites ("See all" focused) |
| `V7 IPTV_20261102152520.jpg` | Series detail (Resume focused) |
| `V7 IPTV_20261102152538.jpg` | Live TV (categories focused) |
| `V7 IPTV_20261102152548.jpg` | Movies (categories focused) |
| `V7 IPTV_20261102152559.jpg` | Series catalog (categories focused) |
| `V7 IPTV_20261102152607.jpg` | Search, empty query |
| `V7 IPTV_20261102152619.jpg` | Downloads, empty |
| `V7 IPTV_20261102152628.jpg` | Settings > Appearance |
| `V7 IPTV_20261102152732.jpg` | Playback HUD |
| `V7 IPTV_20261102152741.jpg` | Playback Options |

## Verdict

v7-ui-1 is technically sound (fonts, gradients, texture tinting, accent switching and the vertex-alpha fades all
render correctly on hardware) but the composition is still v0.2.0's: a column of rails / lists / slabs with a new
palette, rounded corners and a logo. Only Playback Options and the HUD are close to the target. Home, catalogs,
details, Live TV and Search need structural rebuilding, not polish.

What the hardware **proved works** and v2 can rely on:

- Two-colour vertex gradients with alpha render smoothly (U1 of the v1 checklist), but every linear fade has a
  visible **Mach band at both ends** (HUD at y ~190 and ~254, Home backdrop at x ~1120, series backdrop at x ~1280
  and y ~600). Fades need an eased (multi-segment) profile, not one linear ramp.
- Texture modulation (tinting) and a 4x bilinear-upscaled poster produce a soft, blur-like wash (U2). Usable as
  ambience, never as a recognisable image.
- Accent colours, focus rings and the vector V7 symbol are crisp at TV distance.

## Global findings

1. **Old structure under a new skin.** Every content screen is still "left column + dense list/grid + boxes".
   The accent and rounded corners are new; the layouts are v0.2.0's.
2. **Slab overload.** Each list item is a filled rounded rectangle with an 8 px gap: 11 slabs on Live TV, 7 on
   Settings, 4 full-width slabs on Series detail, 50 keys on Search. Rows read as an admin table.
3. **Focus = outline.** Categories, rows, cards and the Favorites tile all signal focus with a 3-4 px accent
   outline on a slightly lighter box. Nothing scales, nothing brightens; from 3 m the focused poster in a grid of
   24 is hard to find.
4. **Flat typography.** Screen titles 44 px, almost everything else 28 / 24 / 22 px with similar weight. Catalog
   captions (22 px, muted) are the weakest text on screen although they name the content. Settings rows are all
   one size and colour.
5. **Inconsistent headers.** Home: logo + wordmark + pills; lists: symbol + 44 px title; details: a small
   breadcrumb. Captions like "KATEGORİLER" / "TÜM KANALLAR" and duplicated counts add noise.
6. **Backdrops are separate rectangles.** Home and Series detail draw the enlarged poster in a box whose edges are
   visible (end of a linear fade), and Home additionally shows the same poster again as a 280 x 420 tile:
   "left UI, right poster".
7. **Hints compete.** 6-7 hints on Search, HUD, catalogs; Home's hint bar is drawn *over* the Recently Watched
   rail (see Home).
8. **Safe area.** Playback Options rows and chevrons reach x ~1870 (safe edge 1824). Everything else respects
   the 96 / 54 px margins.

## Screen by screen

### Home (152459, 152528) - structural failure

- **Split hero.** Text block x 96-1050; backdrop box starts with a hard vertical edge at x ~1120 (end of a 560 px
  linear fade) and is a ~6x zoom of the poster's hair/face; a second copy of the poster sits at x 1544-1824,
  y 180-600. Two images of the same thing, both boxed.
- **Dead band.** Buttons are pinned at y 556 while the text stack ends at y ~400: a 150 px void between meta and
  progress bar.
- **Admin status line.** "● ps4-iptv" under the logo (y 146) reads like a server dashboard.
- **Navigation competes with content.** Four equal pills (Ana Sayfa filled), four 64 px filled circles, a 34 px
  clock and a floating "Ayarlar" caption under the focused icon. The bar is the busiest part of the screen.
- **Cards are list items.** Continue Watching cards are 408 x 150: a 100 x 150 poster squeezed against three text
  lines and a bar; titles truncate at ~18 characters ("The Walking Dead:...", "How I Met Your Mot...").
- **Hint bar over content.** First view: the "Son İzlenenler" heading is at y ~970 and its cards continue below
  the hints (channel names visible at y ~1040). The 160 px bottom fade is not enough; hints and rail overlap.
- **Scrolled state.** A strip of the backdrop remains visible at x >= 1120, y 190-320 under the navigation
  (bottom of the backdrop box with a visible lower edge).
- **Recently Watched (live).** Logo tile 136 x 84 + two lines in a 408 x 150 box: mostly empty card.
- **Favorites.** A focused 408 x 150 "Tümünü gör / Tüm favoriler" tile is the only item, plus a long sentence:
  the rail shows a utility button instead of content.

### Movies / Series catalog (152548, 152559) - structural failure

- 400 px category column (x 96-496) with 28 px names: visually as heavy as the grid.
- 8 x 3 = 24 posters of 140 x 210 with ~19 px gaps: a poster wall. 22 px muted titles truncate after ~10
  characters ("1 Kezban 1...", "7 Kogustak...").
- Duplicated counts: "19826 film · Yenileniyor..." top right **and** "Tüm filmler 19826 film · Sağlayıcı
  sırası" in accent under the header; an empty band at y 160-200 reserved for the focused title.
- Square poster corners inside rounded focus outlines.
- No visual separation between "where I am" (category) and "what I browse" (grid) except the outline box.

### Movie detail (not captured; same construction as Series detail)

- Same backdrop box (x 640-1920) with linear fades; poster 360 x 540; text column; four buttons in one row.

### Series detail (152520) - structural failure

- Backdrop rectangle x 1280-1920, y 0-600 with visible left and bottom edges and a third edge where the two fades
  meet (x ~640, y ~560): the "giant right-side poster enlargement".
- Dense information block: 44 px title, genre line, a two-line director + full cast list, two-line plot, all
  starting at the same x and similar size.
- Resume (accent fill + focus ring = double outline) and Favorite are 76 px pills.
- Episode rows: four full-width (1728 px) filled slabs. The provider sends the series cover as the episode image,
  so every row shows the same tiny pillar-boxed poster in a grey 150 x 84 tile. The right ~1000 px of each row is
  empty except the progress of the episode in progress. Generic names ("1. Bölüm") dominate; the code is in accent.
- Density is right (four rows visible) but the rows are flat and box-heavy.

### Live TV (152538) - structural failure

- Three columns: categories 96-476, channels 532-1330, details 1388-1824.
- Every channel is a 76 px filled slab with a filled logo tile inside: box-in-box, 9 per screen.
- Detail card 436 x 760: logo tile at the top, three short lines, then ~400 px of empty card above "İzle".
- Uppercase captions "KATEGORİLER", "TÜM KANALLAR · 573" add a second header row.
- Two scrollbars of the same brightness as the content.

### Search (152607) - structural failure

- Keyboard 652 px wide, field above it; filter chips 4 x 236 px at the right.
- Empty state: the right area x 800-1824, y 220-960 is one void with a centred two-line message; below the
  keyboard y 660-980 is empty too. Keyboard and results do not form one composition.
- "Aranan: 19.838 film · 2.364 dizi · 573 kanal" floats under the keyboard.
- Seven hints.

### Downloads (152619) - acceptable, refine

- Empty state reads well. Header, size line and two 320 px tab pills are crowded at the top (y 54-216); the tabs
  are as large as buttons.

### Settings (152628) - refine

- The three-column idea works. Appearance uses nine 878 x 88 slabs for the colours; the two toggles are below the
  fold. The description panel is a 364 x 760 card holding two lines.
- Category names and rows share one size and weight: uniform, dashboard-like.

### Playback HUD (152732) - close, polish

- Top: solid overlay to y 190, then a 64 px linear fade: a visible horizontal band at y ~190 and ~254 over bright
  video. Bottom: same at y ~790-850.
- Title at y 44 sits close to the top edge; six hints (Pause, D-PAD, Options, Info, Next episode, Back).
- Progress bar, time and track chips read well.

### Playback Options (152741) - close, polish

- Five groups read well. Rows and chevrons reach x ~1870 (outside the safe area); the selected row is an outline
  box; title 44 px at y 64; three hints at the bottom.

## What v2 changes (summary, details in docs/v7-ui-v2-layouts.md)

| Problem | v2 answer |
|---|---|
| Boxed backdrops, linear fade bands | One full-bleed poster layer per screen, eased multi-segment fades on every edge, no second poster on Home |
| Old list/rail structure | Home: cinematic hero + landscape rails; catalogs: 7 large posters per row with a peek row; details: backdrop + poster + text composition |
| Slabs everywhere | Rows are separators on the background; only the focused row gets a surface |
| Outline-only focus | Posters/cards scale and brighten (others dim); rows get a surface + accent edge; chips/tabs fill |
| Flat type | 64 hero / 44 screen / 32 section / 28 body / 24 card / 22 meta, primary vs. secondary vs. muted |
| Heavy navigation | Text tabs + small transparent icon buttons on Home; contextual one-line headers elsewhere |
| Hint clutter, overlap | 4-5 essential hints; a solid fade band under the hint bar on scrolling screens |
| Search void | Full-width field, compact keyboard, results own the right side; compact instructions when empty |
| Settings slabs | Divider rows, section heading, light description column, 3 x 3 accent palette |
