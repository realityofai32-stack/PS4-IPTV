# V7 IPTV - v3 audit of the v7-ui-2 hardware screenshots

Source: the ten physical PS4 captures in `docs/v7-ui-hardware/v2/` (build 3a56e24, 1920 x 1080 JPEG). They are
the reference; source coordinates were only used to explain what the captures show. Colours below were sampled
from the captures (JPEG, so +-2 per channel).

| Capture | Screen |
|---|---|
| `…163934` | Home, hero (Continue Watching: The Walking Dead: Daryl Dixon), first rail peeking |
| `…163943` | Series detail, season 2, episode rows |
| `…163953` | Live TV, All channels (573), categories / channels / channel card |
| `…164004` | Movies, All movies (19,838) |
| `…164013` | Series, All series (2,364) |
| `…164022` | Search, empty query |
| `…164035` | Settings > Appearance (3 x 3 accent grid) |
| `…164117` | Player HUD (Red accent) |
| `…164127` | Playback Options (Red accent) |
| `…164145` | Home scrolled: Continue Watching + Recently Watched (Red accent) |

V2's structure holds up on the TV: navigation, hero + rails, catalog rail + grid, three-column Live TV, Settings
two-pane with the palette grid, HUD. The problems are artwork treatment, the base colour, focus fills and hold
navigation.

## 1. Artwork fit and cropping

- **Home cards force portrait posters into 16:9.** Every Continue Watching / Recently Watched card is a
  408 x 230 cover crop of a 2:3 poster (anchor 22 % from the top). The captures show what that costs: *How I Met
  Your Mother* loses the top half of its title ("met yo… mother"), *Saplantı* keeps a strip with the title but no
  image, *Daryl Dixon* is an unreadable hair/face fragment, *Spider-Man* is a mask detail. Nothing identifies the
  title except the text under the card. (`…163934`, `…164145`)
- **Channel logos on Home are fine** (contained on a tile, `…164145`), but the tile is a large flat slab with a
  small logo in it, and it does not match the poster cards next to it.
- **Episode stills** (`…163943`): the provider repeats the series cover as each episode's image; the 16:9 cover
  crop turns it into the same face fragment on every row.
- **Live TV logos** (`…163953`) are contained correctly (no crop, aspect kept).

## 2. Artwork sharpness

- **Hero / detail backdrop**: a 340 x 510 poster cover-cropped into a 1320 x 800 (Home) or 1360 x 1080 (detail)
  layer, i.e. a ~340 x 206 region enlarged ~3.9x. On the TV it reads as a stretched, soft photo, not as ambience:
  the hair texture in `…163934` and `…163943` is visibly blocky-soft.
- **Catalog posters** (`…164004`, `…164013`): drawn at 166 x 249 from a 140 x 210 texture (1.19x, 1.26x when
  focused at scale 1.06). Small poster text ("ADIM ADIM", credits blocks) is soft compared with the logos on Live TV,
  which are drawn 1:1.
- **Home cards**: 408 x 230 from a 340-wide large poster crop (1.2x) - acceptable sharpness, wrong composition.

## 3. Home card composition

The rails read as a wall of four unrelated photo crops with text underneath (`…164145`). The focused card (red
ring) is the only structure. There is no card surface, so the progress bar sits on the artwork and the text
floats on the page.

## 4. Home hero / background quality

- The enlarged poster is the most visible element of the page and competes with the title.
- Seams: the backdrop layer starts at x = 600 and ends at y = 800; the fades end in the flat background colour
  while the page underneath is a navy gradient, so a vertical edge at x ~600 (y 500-800) and a horizontal edge at
  y ~800 (x > 1390) are visible in `…163934`.

## 5. Black / navy balance

The permanent base reads as dark navy on the TV, not black:

| Where | Sampled | Palette value |
|---|---|---|
| Page background (Live TV, Settings) | `#090D16` | (7, 10, 16) |
| Bottom of the page / hint band | `#0C101B` | (12, 16, 26) |
| Channel card panel | `#121824` | (18, 24, 36) |
| Secondary button / Watch | `#1E2430`, `#212639` | (31, 39, 56) |
| Options panel over video | `#0C0F16` | (11, 14, 22) |

Blue minus red is 9-31 on every surface step; on a TV this is a clear blue cast. Text greys (168, 177, 194) are
blue-grey too.

## 6. Accent overuse

- **Playback Options with Red** (`…164127`): the focused row is a 536 x 80 slab of `#461D21` (dark red) - the
  accent-tinted `focus` token. With Purple / Green it would be a purple / green slab.
- Settings category "Görünüm" (`#1B2C4A`), Live TV "Tüm kanallar" (`#222836`) and the selected season chip
  (`#21314A`) are accent-tinted surfaces as well; with V7 Blue they add to the navy impression.
- The Search "SONUÇLAR" key and selected filter chip use the accent's dark tint (`#1A2A4E`, `#21314A`).
- Backdrops without artwork glow in the accent's dark tint.
- Appropriate uses (keep): nav underline, primary CTA, focus rings, progress, toggles, the V7 mark.

## 7. List navigation / repeat behaviour

Reported on hardware: holding the d-pad on Live TV categories / channels moves one row per press; hold-to-scroll
does not start. Code audit:

- The repeat engine (`input::KeyRepeater`, 0.30 s delay, 90 / 60 / 40 / 30 ms stages) is unchanged since 3.2 and
  every list screen (Live TV, Movies / Series, Settings, Search results, episodes, Playback Options) already
  handles `InputEvent::repeat`.
- **Root cause: the clock.** `clockx::monotonic()` used `std::chrono::steady_clock`. The PS4 toolchain's libc++
  calls `clock_gettime(1)` (disassembled from `openorbis/lib/libc++.a`: `movl $1, %edi`) because its musl headers
  carry the Linux value of `CLOCK_MONOTONIC`. The PS4 kernel is FreeBSD's: clock 1 is `CLOCK_VIRTUAL`, the
  process's user CPU time; `CLOCK_MONOTONIC` is 4. While a direction is held the UI mostly sleeps between frames
  (8 ms idle sleeps, no redraw unless something moves), so the "clock" barely advances and the 0.30 s delay takes
  many seconds. Most likely explanation for "it worked before": with Smooth scrolling **on** (the default) every
  step starts a glide whose continuous redraws use CPU, so the CPU clock moves and repeats eventually start; the
  v2 capture shows Smooth scrolling **off**, where the loop idles and the stall is complete. (Inferred from the
  code; the new clock log lines confirm it on the console.)
  The same clock drives smooth-scroll easing, toasts, debounces and the download speed clock.
- Secondary: several held directions cross zones (episodes -> seasons -> buttons, grid -> categories, results ->
  keyboard, Home rails -> navigation bar), which is confusing once repeat works.

## 8. Focus treatment

Focus is always visible (accent ring or edge), which is good. What reads badly is the accent-tinted fill of
focused rows (see 6) and the 1.06 poster scale that enlarges an already upscaled texture.

## 9. Remaining inconsistencies

- Home rail cards (photo + text below) vs. every other surface (cards / rows with text inside).
- Recently Watched mixes poster crops and logo slabs of the same size with different visual weight.
- Monogram placeholder colours are saturated navy / purple tiles.

## v3 decisions (implemented on `feature/v7-ui-v3-polish`)

1. Clock: `clockx::monotonic()` reads FreeBSD `CLOCK_MONOTONIC` (4) on PS4 (fallback `sceKernelGetProcessTime`);
   one log line at startup and after 10 s compares UI clock, process time and CPU clock. Held directions never
   switch zones. Each hold >= 1 s is logged with its repeat count.
2. Centralised artwork rules (`ui/image_fit.h`): Cover / Contain / Poster / Logo + the poster backdrop.
3. Home media cards: whole poster (170 x 255, 1:1 texture) + text / progress on a neutral card; channels: logo
   contained in a neutral area. Three larger cards per rail.
4. Hero / details: the provider's `backdrop_path` (new `ImageKind::Backdrop`, 1024 x 576, at most two in memory)
   when present; otherwise the large poster at its own ratio against the right edge, dissolving into the page.
5. Neutral black base (`#050608` page, charcoal steps, blue - red <= 13), accent-derived surfaces 4.5 % / 10 %
   tinted, a neutral glow, neutral HUD overlays.
6. Poster texture 170 x 255 (same 256 x 256 GPU page as 140 x 210), focus scale 1.04.
