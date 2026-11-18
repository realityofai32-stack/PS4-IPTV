# V7 Player: name, package images and loading screen

## Name

**V7 Player** - *Media & streaming for jailbroken PS4*. The product is no longer IPTV-only.

- Changed: the PS4 title (`TITLE` in param.sfo), `app.name`, the first-run screen (welcome line and tagline), About,
  the exit dialog, Settings descriptions, the start-up log line and the log file header, the system notification
  when the video output cannot start, the client name a Jellyfin / Emby server shows, README / NOTICE / SECURITY /
  CONTRIBUTING.
- Not changed (data and upgrades depend on them): GitHub repository, Title ID `IPTV00002`, content ID
  `IV0001-IPTV00002_00-IPTV000020002000`, `/data/PS4IPTV/` and every file in it, the HTTP User-Agent token
  `PS4IPTV/<version>`. Released as v0.5.0 / `APP_VER` 00.50.

## The monogram

A V and a 7 on a 100 x 54 grid: every slanted edge runs 0.6 across per unit down, the V's right arm is the 7's
parallel twin, and a short curved hook under the 7's bar points into the V. It is described once, in
`src/ui/brand_art.*`, and drawn from there everywhere: page headers, Home's navigation and the first-run lockup
(`ui::BrandMark`), the loading screen (`ui::Splash`) and the package images. Colours: a vertical gradient from the
accent's `glow` (its brightest tone, new in `ui/palette.h`) through the accent to a deeper tone of it.

## Package images

`assets/make_brand.sh` renders both from the same geometry (`assets/brand/brand_render.cpp`, a host-only software
rasterizer, 3 x 3 / 4 x 4 supersampled with an ordered dither) and saves them with Pillow:

| File | Size | Format | Content |
|---|---|---|---|
| `sce_sys/icon0.png` | 512 x 512 | 8-bit RGB, no alpha, not interlaced, 77 KB | near-black page, soft V7 Blue glow, the monogram 370 px wide; no text, no ring, no small details |
| `sce_sys/pic1.png` | 1920 x 1080 | 8-bit RGB, no alpha, not interlaced, 481 KB | the loading screen's first frame in V7 Blue: dark page, the monogram, flowing light, five dots; no ring, no artwork, no text |

The PS4 cannot change a package's images at run time: both always use V7 Blue. `tests/host/test_brand.cpp` checks
the PNG headers and that both files still match what the geometry draws (mean difference 0.21 / 0.28 levels).

## Loading screen

- Drawn as soon as the settings are read (the settings were moved before fonts, network and workers): with the
  saved accent, or V7 Blue when none is saved (first start, unreadable settings).
- Same composition for all nine accents (V7 Blue, Cyan, Teal, Green, Purple, Magenta, Red, Orange, White): one
  implementation, colours from the accent preset. No ring around the monogram.
- Its first frame has exactly the composition of `pic1.png`: when the app takes over from the system's start image,
  only the colour changes (and nothing at all with V7 Blue).
- Motion: the light drifts slowly (at most 10 px, a 9 s cycle) with a faint shimmer, the highlight glides over the
  dots (2.8 s). Every motion is a function of time, so frames drawn at uneven intervals stay coherent.
- Never delays startup: there is no minimum time. A frame is drawn between startup steps, at most 30 per second;
  when the first screen exists the loading screen fades out over it in 0.3 s (that screen already takes input).
- Fatal errors keep their path: without a renderer there is no loading screen (system notification + exit); any
  other startup problem is logged and startup continues to the first screen. The crash guard still counts a start as
  successful only at the main loop's first frame.
- The log says how long startup took after the settings were read: `startup took N s after the settings (<accent>
  accent)`.

### The saved accent and the next launch

Settings > Appearance saves the accent at once (`settings.json`, key `accentColor`, as before). The next launch
reads it before the first in-app frame: the loading screen appears in that accent immediately, with no new choice
needed. Existing preferences are read as they are (an unknown or missing value means V7 Blue).
