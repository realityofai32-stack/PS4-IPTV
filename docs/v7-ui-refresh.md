# V7 IPTV UI refresh - implementation map

Branch `feature/v7-ui-refresh`, started from `kubuntu-native` @ `cd2b07d` (application code identical to the
public v0.2.0 tag; only Linux build scripts and docs differ). Local only: no push, merge, tag or release until
the physical PS4 checkpoint has been reviewed.

Scope: the UI layer (`src/ui/`, `src/screens/`, theme, settings UI, branding, localization). The media stack is
frozen: Xtream / M3U, catalogs, search ranking, favorites / progress / history data, playback, mpv/FFmpeg,
tracks, downloads, resume, offline, caches, storage formats, authentication and reconnect logic are not
changed. The only data-layer change is one new optional settings field (`accentColor`).

## 1. Audit (Phase 0)

### Rendering framework

- libcross2d (pPlay) on SDL2 + OpenGL ES 2 (Piglet). Retained object tree, on-demand redraw
  (`App::run`: draws only when dirty, animating, scrolling, or once a second).
- Available primitives: `RectangleShape` (rounded corners, outline, texture), `CircleShape`, `ConvexShape`,
  `GradientRectangle` (2-colour linear, 4 directions, vertex alpha), textured rectangles with sub-rects.
  No clipping (GL scissor is used only while a list glides), no shaders, no blur, no render targets.
- Text: `ui::Label` (UTF-8, Inter Regular/SemiBold, DejaVu Sans + Droid Sans Fallback per glyph, FriBidi
  RTL). Every (weight x pixel size) pair is a glyph atlas page: new font sizes cost GPU memory, so the type
  scale stays small.
- Lists / grids are virtualized (`ui::ListView`, `ui::GridView`): rows exist only for the visible window and
  are re-bound on scroll. Smooth scrolling = `scroll::Smooth` glide + scissor.
- Screens are built once when created; colours are read when shapes are built or rows are bound.
  `App::applyLanguage` already rebuilds the whole screen stack - the same mechanism applies an accent change.

### Theme / widgets (reusable today)

- `ui/palette.h` (constexpr RGBA + WCAG contrast helpers) -> `ui/theme.h` (`theme::accent()` ... tokens,
  layout and type constants). `tests/host/test_theme.cpp` checks contrast and forbids colour literals in
  `screens/`, `ui/`, `app/`. This is a good base: the refresh extends it instead of replacing it.
- Widgets: `Button`, `HintBar` + `ButtonGlyph` (vector DS4 symbols, no Sony assets), `Monogram`, `LogoView`,
  `PosterView`, `ScrollBar`, `Spinner`, `ListView`, `GridView`; `screens::header`, `screens::FieldRow`;
  modal `Dialog`, `Menu` (side panel), `Keyboard`.
- Gaps: the accent is a compile-time constant; focus treatment is re-implemented per screen (fill + outline
  in every adapter); no shared chip/tab, progress bar, panel, empty-state, rail-card or brand components;
  hint pairs such as `L1 / R1 Category` leave a wide empty gap after `L1`.

### Navigation / focus

- Screen stack with modal overlays; DS4 input through `InputManager` (repeat + hold acceleration).
- Each screen owns its focus model (zones + indices). Home: three zones (7 equal tiles, Continue Watching,
  Recently Watched). Circle = back, Triangle = Search, OPTIONS = Settings/Options, Square = favorite /
  contextual action. These conventions are kept.

### Images

- `ImageLoader`: kinds `Logo` (96x52 + 340x190), `Poster` (140x210), `PosterLarge` (340x510); disk cache
  96 MB, GPU LRU 48 MB / 320 entries, at most 2 uploads per frame, screens declare what they want.
- There is no backdrop/fanart image kind. `MovieInfo::backdrop` is parsed but never loaded; history entries,
  catalog movies and series only carry a poster/cover URL. Episodes may carry `image` (a still).
- Decision: no new image kind in this phase (frozen pipeline, GPU budget). Hero and detail backdrops are
  derived from the poster that is already loaded (`PosterLarge`, 1 MB texture): an enlarged, centre-cropped,
  heavily darkened poster with a gradient. Bilinear upscaling of the 340x510 texture gives a soft, blur-like
  wash for free - no blur shader. Episode stills use the existing `Poster` kind for visible rows only.

### Metadata actually available

| Where | Fields |
|---|---|
| Live channel | name, number (`num`, 0 = none), logo, category, favorite. **No EPG** (epg id only). |
| Movie (catalog) | title, year, rating, poster, added |
| Movie (get_vod_info, detail only) | plot, genre, director, cast, release date, duration, video/audio codec, resolution, container |
| Series (catalog) | title, year, rating, cover, plot, cast, director, genre, episode runtime |
| Episode | season/number, title, duration, resolution, still (`image`, optional), progress, download state |
| History / progress | title, poster/logo URL, position, duration, episode code, series name |

Not available, therefore not shown: EPG / now-next, trailers, recommendations / trending / popular, age
ratings, HDR/4K flags (resolution is shown only when the provider reports it), backdrops on Home.

### Unsuitable for PS4 (rejected)

Real-time blur, full-screen translucent stacks, drop shadows on every card, per-frame texture generation,
animated gradients, loading backdrops for every grid cell, auto-playing live previews, focus scale animations
that move layout. Replacements: pre-darkened solid panels, two-stop gradients, a single poster-derived backdrop
per screen, a fixed 6 px focus lift (existing), outline + fill focus.

## 2. Design system (Phase 1)

- **Palette** (`ui/palette.h`): deep-navy near-black surfaces (background < surface < card < elevated < hover),
  blue-grey text; all text/surface pairs contrast-tested.
- **Accent presets** (`palette::ACCENTS`): V7 Blue (default), Cyan, Teal, Green, Purple, Magenta, Red, Orange,
  White. Each preset = accent (borders, fills, progress), accentText (accent as text), accentMuted (selected
  chip / tab at rest), focus (dark tinted row fill), onAccent (text on an accent fill: white or near-black,
  whichever passes). Host tests check every preset on every surface.
- **Runtime accent** (`theme::setAccent`): `theme::accent()` & co. read the active preset; nothing else in the
  call sites changes. Applying a new accent rebuilds the screen stack like a language change.
- **Tokens** (`ui/theme.h`): safe area, spacing scale, radii, focus border, card sizes, type scale
  (Display 56/64, Title 44, Heading 32/34, Body 28, Label 24, Caption 22).
- **Components** (`ui/components.*`): `BrandMark` (vector V7 symbol), `Wordmark`, `Chip` (tabs, filters, season
  chips), `ProgressBar`, `IconButton` (search / favorites / downloads / settings, vector), `EmptyState`,
  `Backdrop` (poster-derived ambient image + gradients), `TopNav`. `Button` gains primary/secondary focus rules,
  `HintBar` pairs shoulder buttons.
- **Focus rule**: focused = accent outline (4 px cards, 3 px rows) + one step lighter fill; primary buttons are
  accent-filled at rest and get a white focus ring. No scaling animation; posters keep the fixed 6 px lift.

## 3. Brand

- Product-facing name **V7 IPTV**: `app.name`, exit dialog, onboarding, About, settings descriptions, PKG
  `TITLE` (param.sfo), icon0.png, startup log line.
- Kept for compatibility: Title ID `IPTV00002`, content ID, `/data/PS4IPTV/` (profiles, settings, history,
  downloads, caches, playlists folder), HTTP User-Agent `PS4IPTV/<version>` (providers may filter on it), CMake
  project / binary names, kernel log tag, script names.
- Logo: geometric "V7" ligature - a V whose right arm is the stem of a 7, closed by the 7's top bar. Three convex
  polygons on a 100 x 80 grid; drawn in code (`ui::BrandMark`) at any size (no new romfs file: the PKG
  allow-list stays unchanged) and rasterised 4x-supersampled into `assets/icon0.png` by `assets/make_icon.py`.

## 4. Screen map

| Phase | Screen | Change |
|---|---|---|
| 2 | Home | Top navigation (logo, Home / Live TV / Movies / Series, Search / Favorites / Downloads / Settings icons, clock); hero = most recent Continue Watching item (poster backdrop, progress, Resume / Details) or a "start watching" hero with section buttons; rails: Continue Watching, Recently Watched, Favorites (only items whose catalog is loaded; never fabricated) |
| 3 | Movies / Series | Same template: branded header, category rail, poster grid with consistent focus |
| 3 | Movie detail | Poster-derived backdrop, poster left, info right, actions directly under the plot, Play/Resume dominant |
| 3 | Series detail | Same top composition, season chips, episode rows with still (when the provider has one) or text-first |
| 4 | Live TV | Category rail, channel rows (number, logo, name, favorite), channel panel; no EPG |
| 4 | Search | Large field, compact keyboard, filter chips, result rows; keyboard dims when results have focus |
| 5 | Settings | Categories (Appearance, Playback, Audio & subtitles, Video, Downloads & storage, Sources, Language, Diagnostics, About) / options / description |
| 5 | Appearance | Accent color list with swatches, applied immediately and saved |
| 5 | Downloads, Favorites, Sources | Shared chips, progress bars, state colours, empty states |
| 6 | Player | HUD spacing/hierarchy, hints; Options grouped: Audio, Subtitles, Video, Subtitle style, Technical info |
| 7 | All | Consistency pass, hardware checklist, checkpoint build |

## 5. Performance and memory safeguards

- No new image kinds; one `PosterLarge` request for the Home hero / detail backdrop (already used by details).
- Rails and lists stay virtualized or bounded (Continue Watching 10, Recently Watched 10, Favorites 12).
- No per-frame allocation: rails are rebuilt only on data generation changes (as before).
- Home vertical scroll uses the existing smooth-scroll setting; with it off, it jumps.
- No new font sizes beyond the token scale.

## 6. Tests

- Theme: every accent preset against every surface (text, borders, on-accent captions), surfaces stay dark,
  no colour literals (existing scan extended to the new files).
- Settings: `accentColor` round trip, missing field (older files) -> V7 Blue, invalid value -> V7 Blue,
  unchanged other fields.
- i18n: new keys in English and Turkish, placeholders, the hard-coded text scan covers the new UI files.
- Branding: `app.name` is V7 IPTV in both languages; no user-facing "PS4 IPTV" left in the tables.

## 7. Progress log

- Phase 0 audit: this document.
