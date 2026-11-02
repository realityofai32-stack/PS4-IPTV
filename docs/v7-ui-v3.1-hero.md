# V7 IPTV - v3.1: Home hero artwork quality

One targeted fix before v0.3.0. The v3 hardware captures were approved, except that the artwork behind the Home
hero looked low-resolution and enlarged, especially with portrait art such as *The Walking Dead: Daryl Dixon*.
Nothing else changed: layout, navigation, cards, typography, accents, the other screens and the controls are the
same as v3.

## What the v3 capture showed

Source: the v3 hardware Home capture (`docs/screenshots/home.jpg` in the working tree, 1920 x 1080 JPEG).

- **The poster fallback, not a backdrop.** The art is portrait, and the poster's own title block ("THE WALKING
  DEAD / DARYL DIXON") sits at the bottom right, cropped at the screen edge. The provider sends no `backdrop_path`
  for this series, which matches the fixtures: `get_series_info` has `[]`.
- **How it was drawn (v3 code):**
  - Texture: the `PosterLarge` texture, at most 400 x 600. Decoding never enlarges, so a smaller source stays
    at its own size.
  - Placement: 1.1 x the 800 px hero layer, 880 px tall and ~587 px wide, at x 1373..1960 (cropped to
    1373..1920 x 0..800).
  - Enlargement: **1.47x** for a source of 400 x 600 or larger, and more for a smaller one (300 x 450 → 1.96x).
- **Effective enlargement, measured from the capture.** Method: shrink a crop of the hero art by k, enlarge it
  back, and see where the error starts to rise.
  - The error stays flat up to k ≈ 2 and rises after that. That suggests ~2x, so the source was probably about
    300 x 450.
  - Calibration: the 1:1 card art next to it rises from k = 1.25.
  - This is an estimate. The capture is a JPEG of dimmed art, so it cannot prove the source size; the new log
    line below reports it.
- **Faint contours.** Tiny 1-2 level steps ((5,6,8) against (6,7,9) / (7,8,10)) trace the hero's fade
  rectangles at x 600..1400, y 530..660.
  - Cause: the page is a vertical gradient (5,6,8) → (8,9,12), while every hero fade ends in the flat
    `background()` colour.

## v3.1 strategy

| | v3 | v3.1 |
|---|---|---|
| Large textures for the Home hero | backdrop (if any) **and** poster requested | exactly one: the backdrop if the provider has one and it loads, otherwise the hero poster |
| Real backdrop | 1024 x 576 in a 1024 x 1024 page (4 MiB), 1.39x over the hero | **1280 x 720 exact-size texture (3.52 MiB), 1.11x** |
| Poster fallback texture | `PosterLarge` 400 x 600 in a 512 x 1024 page (2 MiB) | **`HeroPoster` 512 x 768 exact (1.5 MiB)**; a smaller source keeps its size |
| Poster fallback size | always 880 tall (1.47x or more) | wanted 800 tall, **never more than 1.25x its texture**, never wider than 560, not drawn under 400 |
| Around the poster | black page | a dim field of the poster's own average colour behind and right of it, fading out before the text column (x 1096) |
| Page under the hero | vertical gradient | flat `#050608`: the fades end in exactly the page colour |

**Poster sizes under the new rule:**

| Texture | Drawn size | Enlargement |
|---|---|---|
| 512 x 768 (source 512 x 768 or larger, e.g. TMDB 600 x 900) | 533 x 800 | 1.04x |
| 400 x 600 | 500 x 750 | 1.25x |
| 300 x 450 | 375 x 562 | 1.25x |
| 240 x 360 | 300 x 450 | 1.25x |
| smaller | not drawn: the hero stays black | - |

In every case the top of the poster is at the screen top and its right edge 40 px past the screen. Its left fade
covers half its visible width and its bottom fade 30 % of its height. Both fades end in its colour field, so there
is no rectangle and no seam.

**The colour field** (`fit::ambientWash`) is computed once per poster on the worker: the mean of 64 x 64 samples.

- It is the poster's average colour as it is drawn (tint, then shade), 35 % desaturated and 70 % of the way from
  the page colour.
- It is capped at 34 per channel and is never darker than the page.
- Daryl Dixon gives about (16, 14, 13); a black poster gives no field.
- Its left end is clamped to x 1096, so the title, buttons and progress stay on pure page black.

**Exact-size textures.** Every other image still uses the power-of-two page. The two large kinds use an
exact-size RGBA8 texture instead:

- Settings: GL_LINEAR, explicitly clamp-to-edge, no mipmaps, which is complete for non-power-of-two sizes in core
  GLES2.
- The player's 1920 x 1080 video texture (`GLTextureBuffer`) has the same format and filters and is sampled on
  the PS4 every frame.
- The power-of-two rule would put 1280 x 720 in a 2048 x 1024 page (8 MiB).

**Detail pages** keep v3's poster treatment (only the Home hero was in scope). Their real backdrop, when there is
one, is now 1280 x 720 as well.

**No upscaling tricks:** no AI upscaling, sharpening, blur, generated art or new libraries. A small source is drawn
smaller, never blown up.

## GPU memory

| | v3 | v3.1 |
|---|---|---|
| Home hero, poster fallback | 2 MiB (PosterLarge page) | ≤ 1.5 MiB |
| Home hero with a provider backdrop | 4 + 2 = 6 MiB | 3.52 MiB |
| Backdrops kept in memory | ≤ 2 x 4 = 8 MiB | ≤ 2 x 3.52 = 7.03 MiB |
| 48 MiB image LRU | unchanged | unchanged |

Decode and upload cost:

- The backdrop decode is unchanged: sources are still limited to 2048 px.
- The resize cost follows the source size, so it is about the same.
- The uploads are smaller: 3.52 instead of 4 MiB, and 1.5 instead of 2 MiB.
- The average colour is 4,096 samples on the worker.

When the hero item's Details page opens, its `PosterLarge` (2 MiB) loads separately, as in v3.

## Log lines (no URLs)

```
[images] large image <hash> kind 4: source 300x450 -> texture 300x450 (page 300x450 exact, 527 KiB)
[home] hero art: poster fallback (provider backdrop none), source 300x450, texture 300x450 (page 300x450),
       drawn 375x562, visible 335x562, upscale 1.25x of the texture, 1.25x of the source (kept smaller: quality limit)
```

The `source` field tells whether a soft picture is the provider's limit or ours. A backdrop logs as
`hero art: backdrop (provider backdrop present)` with kind 3.

Checklist: `docs/checkpoint-v7-ui-3.1.txt`.
