// V7 IPTV UI v2 layout: the measured 1920 x 1080 geometry of every reconstructed screen in one place.
//
// docs/v7-ui-v2-layouts.md explains each block; tests/host/test_layout.cpp checks the invariants (inside the
// TV-safe area, no overlaps, the grids and rails add up to their widths, four episode rows fit). Screens read these
// values instead of scattering literals through their render code. Plain constants only: no libcross2d types,
// so the host tests compile this header as it is.

#ifndef PS4IPTV_UI_LAYOUT_H
#define PS4IPTV_UI_LAYOUT_H

namespace layout {

    // ---------------------------------------------------------------- frame
    constexpr float SCREEN_W = 1920;
    constexpr float SCREEN_H = 1080;
    constexpr float SAFE_X = 96;                    // ~5% horizontal TV-safe margin (left and right)
    constexpr float SAFE_Y = 54;                    // ~5% vertical TV-safe margin (top and bottom)
    constexpr float SAFE_RIGHT = SCREEN_W - SAFE_X;  // 1824
    constexpr float SAFE_BOTTOM = SCREEN_H - SAFE_Y; // 1026
    constexpr float CONTENT_W = SAFE_RIGHT - SAFE_X; // 1728

    // controller hints: one line at the bottom of the safe area
    constexpr float HINTS_H = 40;
    constexpr float HINTS_Y = SAFE_BOTTOM - HINTS_H;   // 986
    constexpr float CONTENT_BOTTOM = HINTS_Y - 26;     // 960: the lowest content edge above the hints

    // contextual header of sub-pages: [V7] Title   context ......... status
    constexpr float HEADER_Y = SAFE_Y;                 // top of the 44 px title
    constexpr float HEADER_MARK_H = 30;                // V7 symbol height next to the title
    constexpr float HEADER_CONTEXT_GAP = 36;           // title -> context text
    constexpr float CONTENT_TOP = 150;                 // first content row under the header

    // scrolling pages (Home, catalogs): content slides under an eased fade and a solid band behind the hints
    constexpr float BOTTOM_FADE_Y = 872;
    constexpr float BOTTOM_SOLID_Y = 968;

    // ---------------------------------------------------------------- left rails (catalogs, Live TV, Settings)
    constexpr float RAIL_X = SAFE_X;
    constexpr float RAIL_W = 330;
    constexpr float RAIL_GAP = 36;                     // rail -> main column
    constexpr float MAIN_X = RAIL_X + RAIL_W + RAIL_GAP;   // 462
    constexpr float RAIL_ROW_H = 52;
    constexpr float RAIL_ROW_GAP = 4;

    // list rows (separators on the background; only the focused row gets a surface)
    constexpr float ROW_EDGE_W = 4;                    // accent edge of the focused / selected row
    constexpr float ROW_PAD = 20;                      // text inset inside a row

    // ---------------------------------------------------------------- Home
    namespace home {
        constexpr float NAV_Y = SAFE_Y - 8;            // 46
        constexpr float NAV_H = 72;                    // 46..118
        constexpr float BRAND_MARK_H = 34;
        constexpr float TAB_H = 52;
        constexpr float TAB_PAD = 22;
        constexpr float TAB_GAP = 6;
        constexpr float TABS_GAP = 56;                 // V7 symbol -> first tab
        constexpr float ICON = 52;
        constexpr float ICON_GAP = 12;
        constexpr float CLOCK_W = 110;                 // reserved right of the icons ("23:59" at 28 px ~ 76 px)
        constexpr float NAV_BAND_H = 124;              // solid band under the navigation once the page scrolled
        constexpr float NAV_FADE_H = 84;

        // hero: one composition over a full-bleed poster layer
        constexpr float BACKDROP_X = 600;
        constexpr float BACKDROP_H = 800;
        constexpr float BACKDROP_FADE_LEFT = 720;      // 600..1320: background -> image (darker behind the text)
        constexpr float BACKDROP_FADE_BOTTOM = 300;    // 500..800: image -> background
        constexpr float BACKDROP_FADE_TOP = 240;       // darker under the navigation
        constexpr float TEXT_X = SAFE_X;
        constexpr float TEXT_W = 1000;
        // hero poster fallback (v3.1, no landscape backdrop): decorative art, quality first. The v3 capture drew the
        // poster >= 1.47x its texture and it read as soft on the TV; now it is never enlarged past
        // HERO_POSTER_MAX_UPSCALE: a 512 x 768 texture fills the box height at 1.04x, a 400 x 600 one is drawn 750
        // tall, a 300 x 450 one 562 tall - black space instead of a blow-up. Under HERO_POSTER_MIN_H nothing is drawn.
        // Its own dimmed average colour fills the box behind / right of it and fades out before the text column.
        // v7-ux-6 hero artwork (images/hero_art.h): a real backdrop brighter than on detail pages (~42 % instead of
        // ~24 %: 190 / 255 x (1 - 110 / 255)); the showcase poster (sharp, framed, rounded corners like the poster
        // tiles) at 240 / 255 brightness on a soft shadow
        constexpr int HERO_BACKDROP_TINT = 190;
        constexpr int HERO_BACKDROP_SHADE = 110;
        constexpr int SHOWCASE_TINT = 240;
        constexpr float SHOWCASE_RADIUS = 12;
        constexpr float SHOWCASE_SHADOW_Y = 10;
        constexpr int SHOWCASE_SHADOW_ALPHA = 120;
        constexpr float HERO_POSTER_SCALE = 1.0f;          // wanted height / BACKDROP_H (v3: 1.1)
        constexpr float HERO_POSTER_MAX_UPSCALE = 1.25f;
        constexpr float HERO_POSTER_MAX_W = 560;           // square / wide images used as posters
        constexpr float HERO_POSTER_MIN_H = 400;
        constexpr float HERO_POSTER_BLEED = 40;            // cropped at the screen's right edge
        constexpr float HERO_POSTER_LEFT_FADE = 0.5f;      // x its visible width
        constexpr float HERO_POSTER_BOTTOM_FADE = 0.3f;    // x its height (a short poster has no hard bottom edge)
        constexpr float HERO_WASH_LENGTH = 420;            // the colour field's fade left of the poster
        constexpr float HERO_TEXT_RIGHT = TEXT_X + TEXT_W; // 1096: the field never reaches the text column
        // the text stack is anchored on the buttons and grows upwards (a two-line title moves the eyebrow up)
        constexpr float BUTTONS_Y = 548;
        constexpr float BUTTON_H = 76;
        constexpr float RESUME_W = 360;
        constexpr float SECONDARY_W = 250;
        constexpr float BUTTON_GAP = 24;
        constexpr float PROGRESS_W = 480;
        constexpr float PROGRESS_TO_BUTTONS = 44;      // bar top -> buttons top

        // rails of landscape media cards (v3): 3 x 552 + 2 x 36 = 1728 = the content width. A card keeps the whole
        // portrait poster on its left (170 x 255 = the Poster texture box, drawn 1:1: never cropped to 16:9, never
        // upscaled) and puts title, episode, time left and progress on a neutral surface beside it. Channels: their
        // logo contained in a neutral logo area of the same size.
        constexpr float RAILS_Y = 688;                 // first rail heading
        constexpr float RAIL_HEADING_H = 56;           // heading line + gap to the cards
        constexpr float CARD_W = 552;
        constexpr float CARD_H = 255;
        constexpr float CARD_ART_W = 170;              // 2:3 poster / logo area: 30.8 % of the card
        constexpr float CARD_GAP = 36;
        constexpr float CARD_PAD = 28;                 // text inset inside the card
        constexpr float CARD_TEXT_X = CARD_ART_W + CARD_PAD;               // 198
        constexpr float CARD_TEXT_W = CARD_W - CARD_TEXT_X - CARD_PAD;     // 326
        constexpr float CARD_LOGO_PAD = 22;            // a channel logo's inset in its area
        constexpr float CARD_RING_GAP = 5;             // focus ring: this far outside the card
        constexpr float RAIL_GAP = 52;
        constexpr float RAIL_PITCH = RAIL_HEADING_H + CARD_H + RAIL_GAP;   // 363
        constexpr int CARDS_VISIBLE = 3;
        // the "more" chevron (Continue Watching / Recently Watched): on the rail's heading line, right-aligned to the
        // rail's right edge = the TV-safe edge (v7-ux-10: it was 18 px outside it)
        constexpr float RAIL_MORE_RIGHT = SAFE_RIGHT;  // 1824
        constexpr float RAIL_MORE_CENTER_Y = 22;       // below the rail's top: the middle of its heading line
        constexpr float VIEW_BOTTOM = 958;             // a focused rail's lowest text edge
    }

    // ---------------------------------------------------------------- Movies / Series
    namespace catalog {
        constexpr float GRID_X = MAIN_X;               // 462
        constexpr float GRID_Y = CONTENT_TOP;          // 150
        constexpr float GRID_W = SAFE_RIGHT - GRID_X;  // 1362 (incl. the 22 px scrollbar gutter)
        constexpr float SCROLL_GUTTER = 22;
        constexpr int COLUMNS = 7;
        constexpr int ROWS = 2;                        // whole rows; a third row peeks under the bottom fade
        constexpr int PEEK_ROWS = 1;
        constexpr float POSTER_W = 166;
        constexpr float POSTER_H = 249;                // 2:3
        constexpr float TITLE_GAP = 16;                // poster -> title (room for the focus scale)
        constexpr float CELL_H = POSTER_H + TITLE_GAP + 30 + 2 + 28;   // poster, title, meta = 325
        constexpr float ROW_GAP = 30;
        constexpr float GRID_H = ROWS * CELL_H + (ROWS - 1) * ROW_GAP;   // 680
        // 166 x 249 x 1.04 = 173 x 259: the 170 x 255 Poster texture is drawn at ~1:1 even when focused (1.06 made it
        // a visible 1.26x upscale with the v2 140 x 210 texture)
        constexpr float POSTER_FOCUS_SCALE = 1.04f;
        constexpr float RAIL_H = CONTENT_BOTTOM - CONTENT_TOP - 10;     // 800
    }

    // ---------------------------------------------------------------- details
    namespace detail {
        // v7-ux-11 artwork (screens::bindDetailArt): the provider's usable landscape backdrop on the right, faded into
        // the page (starts further right than before: the plot column stays on the clean page), at ~34 % brightness
        // (175 / 255 x (1 - 128 / 255)); without one the page stays clean dark with the poster's colour as a subtle
        // tint hugging the poster (images/hero_art.h ambientAround). The poster is framed like Home's showcase.
        constexpr float BACKDROP_X = 680;
        constexpr float BACKDROP_FADE_LEFT = 700;      // 680..1380
        constexpr int BACKDROP_TINT = 175;
        constexpr int BACKDROP_SHADE = 128;
        constexpr float TINT_TEXT_GAP = 24;            // the poster's tint stops this far before the text column
        constexpr float POSTER_SHADOW_Y = 10;          // the poster's soft shadow (as Home's showcase)
        constexpr int POSTER_SHADOW_ALPHA = 120;
        constexpr float BACKDROP_FADE_TOP = 200;
        constexpr float CRUMB_MARK_H = 26;

        // movie
        constexpr float M_POSTER_X = SAFE_X;
        constexpr float M_POSTER_Y = 160;
        constexpr float M_POSTER_W = 320;
        constexpr float M_POSTER_H = 480;
        constexpr float M_TEXT_X = M_POSTER_X + M_POSTER_W + 64;   // 480
        constexpr float M_TEXT_W = SAFE_RIGHT - M_TEXT_X;          // 1344
        constexpr float M_PLOT_W = 1040;
        constexpr float M_BUTTONS_MIN_Y = 540;
        constexpr float M_BACKDROP_FADE_BOTTOM = 440;  // 640..1080

        // series: a compact top so four episode rows are on screen at once
        constexpr float S_COVER_X = SAFE_X;
        constexpr float S_COVER_Y = 112;
        constexpr float S_COVER_W = 180;
        constexpr float S_COVER_H = 270;               // 112..382
        constexpr float S_TEXT_X = S_COVER_X + S_COVER_W + 48;     // 324
        constexpr float S_TEXT_W = SAFE_RIGHT - S_TEXT_X;          // 1500
        constexpr float S_PLOT_W = 1100;
        constexpr float S_BUTTONS_Y = 318;
        constexpr float S_BUTTON_H = 64;               // 318..382, bottom-aligned with the cover
        constexpr float S_RESUME_W = 470;
        constexpr float S_FAVORITE_W = 300;
        constexpr float S_BACKDROP_H = 680;
        constexpr float S_BACKDROP_FADE_BOTTOM = 320;  // 360..680
        constexpr float S_SEASONS_Y = 414;
        constexpr float S_CHIP_H = 48;
        constexpr float S_CHIP_MIN_W = 150;
        constexpr float S_EPISODES_Y = 486;
        constexpr float S_ROW_H = 108;
        constexpr float S_ROW_GAP = 12;
        constexpr float S_EPISODES_H = 4 * S_ROW_H + 3 * S_ROW_GAP;   // 468: four rows, 486..954
        constexpr float S_THUMB_W = 160;               // 16:9: a real landscape still, else the episode number tile
        constexpr float S_THUMB_H = 90;
    }

    // ---------------------------------------------------------------- Live TV
    namespace live {
        constexpr float LIST_Y = CONTENT_TOP;
        constexpr float LIST_H = 808;                  // 150..958
        constexpr float CH_X = MAIN_X;                 // 462
        constexpr float CH_W = 880;                    // 462..1342
        constexpr float CH_ROW_H = 78;
        constexpr float CH_ROW_GAP = 2;                // ten rows
        constexpr float NUM_W = 56;
        constexpr float LOGO_X = 72;
        constexpr float LOGO_W = 96;
        constexpr float LOGO_H = 56;
        constexpr float NAME_X = LOGO_X + LOGO_W + 24; // 192
        constexpr float PANEL_X = CH_X + CH_W + 40;    // 1382
        constexpr float PANEL_W = SAFE_RIGHT - PANEL_X; // 442
        constexpr float PANEL_H = 600;                 // 150..750: compact, no empty tail
        constexpr float PANEL_PAD = 32;
        constexpr float PANEL_LOGO_H = 212;            // 378 x 212 tile
        constexpr float WATCH_H = 64;
    }

    // ---------------------------------------------------------------- Search
    namespace search {
        constexpr float FIELD_Y = 140;
        constexpr float FIELD_H = 80;                  // full content width: 96..1824
        constexpr float KEYS_X = SAFE_X;
        constexpr float KEYS_Y = 252;
        constexpr float KEY_UNIT = 44;
        constexpr float KEY_GAP = 8;
        constexpr float KEY_H = 60;
        constexpr int KEY_COLUMNS = 12;                // units per keyboard row
        constexpr float KEYS_W = KEY_COLUMNS * KEY_UNIT + (KEY_COLUMNS - 1) * KEY_GAP;   // 616
        constexpr float RESULTS_X = 760;
        constexpr float RESULTS_W = SAFE_RIGHT - RESULTS_X;   // 1064
        constexpr float CHIPS_Y = KEYS_Y;
        constexpr float CHIP_H = 48;
        constexpr float SUMMARY_Y = 318;
        constexpr float LIST_Y = 360;
        constexpr float ROW_H = 92;
        constexpr float ROW_GAP = 4;
        constexpr float LIST_H = 6 * ROW_H + 5 * ROW_GAP;   // 572: six rows, 360..932
    }

    // ---------------------------------------------------------------- Settings
    namespace settings {
        constexpr float CAT_ROW_H = 60;
        constexpr float SECTION_Y = CONTENT_TOP;       // category name above the settings
        constexpr float LIST_X = MAIN_X;               // 462
        constexpr float LIST_Y = 214;
        constexpr float LIST_W = 840;                  // 462..1302
        constexpr float LIST_H = CONTENT_BOTTOM - LIST_Y - 2;   // 744
        constexpr float ROW_H = 72;
        constexpr float DIVIDER_X = 1334;              // light vertical rule before the description
        constexpr float PANEL_X = 1366;
        constexpr float PANEL_W = SAFE_RIGHT - PANEL_X;   // 458
        constexpr int PALETTE_COLUMNS = 3;             // accent presets as a 3 x 3 grid
    }

    // ---------------------------------------------------------------- Downloads
    namespace downloads {
        constexpr float TABS_Y = 140;
        constexpr float TAB_H = 48;
        constexpr float LIST_Y = 214;
        constexpr float LIST_H = CONTENT_BOTTOM - LIST_Y - 2;   // 744
    }

    // ---------------------------------------------------------------- playback
    namespace player {
        constexpr float TOP_FADE_H = 300;              // eased: no band edge over bright video
        constexpr float TITLE_Y = 60;
        constexpr float SUBTITLE_Y = 120;
        constexpr float BOTTOM_Y = 700;                // eased fade 700..1080
        constexpr float BAR_Y = 894;
        constexpr float BAR_H = 6;
        constexpr float TIME_Y = 918;
        constexpr float OPTIONS_W = 680;               // Playback Options panel (right edge of the screen)
        constexpr float OPTIONS_X = SCREEN_W - OPTIONS_W;      // 1240
        constexpr float OPTIONS_PAD = 48;
        constexpr float OPTIONS_LIST_W = OPTIONS_W - OPTIONS_PAD - SAFE_X;   // 536: ends at the safe edge
        constexpr float OPTIONS_ROW_H = 80;
        constexpr float OPTIONS_EDGE_W = 80;           // soft fade on the panel's left edge
        // the Next Episode card (the last ~7 s of an episode, independent of the HUD): a compact card in the lower
        // right that leaves the video dominant. It ends 100 px above the progress bar, so the bar, time, chips and
        // hints stay readable when the HUD is shown, and clear of subtitles: bottom position (mpv sub-pos 100) puts two
        // lines at ~ y 925 .. 1045, raised (88) at ~ y 815 .. 950 - the card ends at y 790, right of the centre.
        constexpr float NEXT_W = 500;
        constexpr float NEXT_H = 156;
        constexpr float NEXT_X = SAFE_RIGHT - NEXT_W;          // 1324 .. 1824
        constexpr float NEXT_Y = 790 - NEXT_H;                 // 634 .. 790
        constexpr float NEXT_PAD = 24;
        constexpr float NEXT_BAR_H = 3;                        // the countdown: a thin bar along the card's bottom
        constexpr float NEXT_BUTTON_H = 44;
        constexpr float NEXT_SLIDE = 24;                       // px the card slides in from the right
        constexpr double NEXT_SLIDE_SECONDS = 0.22;
    }
}

#endif // PS4IPTV_UI_LAYOUT_H
