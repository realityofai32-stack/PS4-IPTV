// The V7 IPTV v2 layout (src/ui/layout.h, docs/v7-ui-v2-layouts.md): everything inside the TV-safe area, no
// overlapping blocks, grids and rails that add up to their widths, the row counts the layout promises, and room
// for the focus scale. These are the geometric facts that can be checked without the PS4 renderer; how the
// screens look is checked on hardware (docs/checkpoint-v7-ui-2.txt).

#include "check.h"
#include "../../src/ui/layout.h"
#include "../../src/ui/palette.h"

using namespace layout;

namespace {
    const float FOCUS_BORDER = 4;   // theme::FOCUS_BORDER (theme.h needs libcross2d)
    const float LINE_24 = 30;       // line heights (Label: size x 1.25)
    const float LINE_28 = 35;

    // rows a ListView shows for a height (ui::ListView: (h + gap) / (row + gap))
    int listRows(float h, float row, float gap) {
        return (int) ((h + gap) / (row + gap));
    }

    bool inside(float x, float w) {
        return x >= SAFE_X && x + w <= SAFE_RIGHT + 0.01f;
    }
}

TEST(layout_frame_and_rails) {
    CHECK_EQ(SAFE_RIGHT, 1824.0f);
    CHECK_EQ(SAFE_BOTTOM, 1026.0f);
    CHECK_EQ(HINTS_Y + HINTS_H, SAFE_BOTTOM);
    CHECK(CONTENT_BOTTOM < HINTS_Y);
    CHECK(CONTENT_TOP > HEADER_Y + 44 * 1.25f);   // the header line ends before the content
    CHECK_EQ(MAIN_X, RAIL_X + RAIL_W + RAIL_GAP);
    CHECK(BOTTOM_FADE_Y < BOTTOM_SOLID_Y);
    CHECK(BOTTOM_SOLID_Y <= HINTS_Y);            // the hints always sit on the solid band
}

TEST(layout_home) {
    using namespace layout::home;
    // four cards fill the content width exactly
    CHECK_EQ(CARDS_VISIBLE * CARD_W + (CARDS_VISIBLE - 1) * CARD_GAP, CONTENT_W);
    // 16:9 artwork
    CHECK(CARD_ART_H / CARD_W > 0.55f && CARD_ART_H / CARD_W < 0.58f);
    // navigation: the icons end before the clock column, inside the safe area
    float iconsRight = CONTENT_W - CLOCK_W;
    float iconsLeft = iconsRight - 4 * ICON - 3 * ICON_GAP;
    CHECK(iconsLeft > CONTENT_W / 2);
    CHECK(NAV_Y >= SAFE_Y - 10 && NAV_Y + NAV_H < NAV_BAND_H);
    // hero: buttons and progress inside the text column, above the first rail
    CHECK(RESUME_W + BUTTON_GAP + SECONDARY_W <= TEXT_W);
    CHECK(3 * SECONDARY_W + 2 * BUTTON_GAP <= TEXT_W);   // start state: three section buttons
    CHECK(BUTTONS_Y + BUTTON_H + 40 < RAILS_Y);
    CHECK(BUTTONS_Y - PROGRESS_TO_BUTTONS > NAV_Y + NAV_H);
    // a two-line hero title still clears the navigation: eyebrow, 2 x 80 title, episode, meta above the bar
    float stackTop = BUTTONS_Y - PROGRESS_TO_BUTTONS - 22 - LINE_24 - 6 - LINE_28 - 8 - 2 * 80 - 6 - LINE_24;
    CHECK(stackTop > NAV_Y + NAV_H + 30);
    // backdrop: the fades fit in the layer and the left fade covers the text column's right part
    CHECK(BACKDROP_FADE_LEFT + BACKDROP_X <= SCREEN_W);
    CHECK(BACKDROP_X < TEXT_X + TEXT_W);
    CHECK(BACKDROP_X + BACKDROP_FADE_LEFT > TEXT_X + TEXT_W);
    CHECK(BACKDROP_FADE_BOTTOM + BACKDROP_FADE_TOP <= BACKDROP_H);
    // rest view: the first rail's artwork peeks out above the bottom fade
    CHECK(RAILS_Y + RAIL_HEADING_H < BOTTOM_FADE_Y - 100);
    // a focused rail is fully above the hints
    CHECK(VIEW_BOTTOM <= CONTENT_BOTTOM);
    CHECK_EQ(CARD_H, CARD_ART_H + CARD_TITLE_GAP + CARD_TEXT_H);
    // focus scale stays inside the card gap (both neighbours may not touch)
    CHECK(CARD_W * (CARD_FOCUS_SCALE - 1) / 2 + FOCUS_BORDER < CARD_GAP / 2);
    CHECK(CARD_ART_H * (CARD_FOCUS_SCALE - 1) / 2 + FOCUS_BORDER < CARD_TITLE_GAP);
}

TEST(layout_catalog) {
    using namespace layout::catalog;
    CHECK_EQ(GRID_X + GRID_W, SAFE_RIGHT);
    CHECK(GRID_X > RAIL_X + RAIL_W);
    const float usable = GRID_W - SCROLL_GUTTER;
    const float gap = (usable - COLUMNS * POSTER_W) / (COLUMNS - 1);
    CHECK(gap >= 28);                                  // more breathing room than v1's ~19 px
    CHECK(POSTER_H / POSTER_W > 1.49f && POSTER_H / POSTER_W < 1.51f);   // 2:3
    // the focused poster's scale + outline fits in the gaps and above its title
    CHECK(POSTER_W * (POSTER_FOCUS_SCALE - 1) / 2 + FOCUS_BORDER < gap / 2);
    CHECK(POSTER_H * (POSTER_FOCUS_SCALE - 1) / 2 + FOCUS_BORDER <= TITLE_GAP);
    CHECK(POSTER_H * (POSTER_FOCUS_SCALE - 1) / 2 + FOCUS_BORDER < ROW_GAP);
    // two whole rows end before the bottom fade; the peek row starts inside the visible area
    CHECK(GRID_Y + GRID_H <= BOTTOM_FADE_Y);
    CHECK(GRID_Y + GRID_H + ROW_GAP < BOTTOM_SOLID_Y - 60);
    // density: fewer, larger posters than v1 (8 x 3 of 140 x 210)
    const int v1Cells = 8 * 3;
    CHECK(COLUMNS * ROWS < v1Cells * 0.8f);
    CHECK(COLUMNS * (ROWS + PEEK_ROWS) <= v1Cells);
    CHECK(POSTER_W * POSTER_H > 140.0f * 210.0f * 1.3f);
    CHECK(listRows(RAIL_H, RAIL_ROW_H, RAIL_ROW_GAP) >= 12);
    CHECK(CONTENT_TOP + RAIL_H <= CONTENT_BOTTOM);
}

TEST(layout_details) {
    using namespace layout::detail;
    // movie: poster and text column side by side, all actions on one line
    CHECK(M_POSTER_X + M_POSTER_W < M_TEXT_X);
    CHECK_EQ(M_TEXT_X + M_TEXT_W, SAFE_RIGHT);
    CHECK(360 + 3 * 270 + 3 * 24 <= M_TEXT_W);
    CHECK(M_PLOT_W <= M_TEXT_W);
    CHECK(M_POSTER_Y + M_POSTER_H < CONTENT_BOTTOM);
    CHECK(M_BUTTONS_MIN_Y + 76 < CONTENT_BOTTOM);
    CHECK(BACKDROP_X + BACKDROP_FADE_LEFT < SCREEN_W);
    CHECK(M_BACKDROP_FADE_BOTTOM + BACKDROP_FADE_TOP < SCREEN_H);
    // series: compact top, actions bottom-aligned with the cover, four episode rows on screen
    CHECK_EQ(S_TEXT_X + S_TEXT_W, SAFE_RIGHT);
    CHECK_EQ(S_BUTTONS_Y + S_BUTTON_H, S_COVER_Y + S_COVER_H);
    CHECK(S_RESUME_W + 24 + S_FAVORITE_W <= S_TEXT_W);
    CHECK(S_SEASONS_Y >= S_COVER_Y + S_COVER_H + 24);
    CHECK(S_EPISODES_Y >= S_SEASONS_Y + S_CHIP_H + 16);
    CHECK_EQ(listRows(S_EPISODES_H, S_ROW_H, S_ROW_GAP), 4);
    CHECK(S_EPISODES_Y + S_EPISODES_H <= CONTENT_BOTTOM);
    CHECK(S_THUMB_H < S_ROW_H);
    CHECK(S_THUMB_W / S_THUMB_H > 1.77f && S_THUMB_W / S_THUMB_H < 1.78f);   // 16:9
    CHECK(S_BACKDROP_FADE_BOTTOM + BACKDROP_FADE_TOP <= S_BACKDROP_H);
    // the backdrop has dissolved before the episode list starts
    CHECK(S_BACKDROP_H <= S_EPISODES_Y + 200);
    // the information area is smaller than v1's (header to the episode list: 560 px)
    CHECK(S_EPISODES_Y < 560);
}

TEST(layout_live) {
    using namespace layout::live;
    CHECK_EQ(CH_X, MAIN_X);
    CHECK(CH_X + CH_W < PANEL_X);
    CHECK_EQ(PANEL_X + PANEL_W, SAFE_RIGHT);
    CHECK(inside(PANEL_X, PANEL_W));
    CHECK_EQ(listRows(LIST_H, CH_ROW_H, CH_ROW_GAP), 10);
    CHECK(LIST_Y + LIST_H <= CONTENT_BOTTOM);
    CHECK(LIST_Y + PANEL_H <= CONTENT_BOTTOM);
    // the card holds logo, two-line name, category, number and favorite above the Watch pill
    CHECK(PANEL_PAD + PANEL_LOGO_H + 26 + 2 * 43 + 10 + LINE_24 * 3 + 16 < PANEL_H - PANEL_PAD - WATCH_H);
    CHECK(NAME_X > LOGO_X + LOGO_W);
    CHECK(LOGO_X > NUM_W);
}

TEST(layout_search) {
    using namespace layout::search;
    CHECK(inside(SAFE_X, CONTENT_W));
    CHECK(FIELD_Y + FIELD_H < KEYS_Y);
    CHECK_EQ(KEYS_W, 616.0f);
    CHECK(KEYS_X + KEYS_W + 40 <= RESULTS_X);
    CHECK_EQ(RESULTS_X + RESULTS_W, SAFE_RIGHT);
    CHECK(KEYS_Y + 5 * KEY_H + 4 * KEY_GAP <= CONTENT_BOTTOM);
    CHECK(CHIPS_Y + CHIP_H < SUMMARY_Y);
    CHECK(SUMMARY_Y + LINE_24 < LIST_Y);
    CHECK_EQ(listRows(LIST_H, ROW_H, ROW_GAP), 6);
    CHECK(LIST_Y + LIST_H <= CONTENT_BOTTOM);
}

TEST(layout_settings) {
    using namespace layout::settings;
    CHECK_EQ(LIST_X, MAIN_X);
    CHECK(LIST_X + LIST_W < DIVIDER_X);
    CHECK(DIVIDER_X < PANEL_X);
    CHECK_EQ(PANEL_X + PANEL_W, SAFE_RIGHT);
    CHECK(SECTION_Y + 32 * 1.25f < LIST_Y);
    CHECK_EQ(listRows(LIST_H, ROW_H, 0), 10);
    CHECK(LIST_Y + LIST_H <= CONTENT_BOTTOM);
    // the accent presets fill the palette grid exactly (3 x 3)
    CHECK_EQ(palette::ACCENT_COUNT % PALETTE_COLUMNS, 0);
    CHECK_EQ(palette::ACCENT_COUNT / PALETTE_COLUMNS, 3);
    // nine categories fit the rail without scrolling
    CHECK(listRows(CONTENT_BOTTOM - CONTENT_TOP, CAT_ROW_H, RAIL_ROW_GAP) >= 9);
}

TEST(layout_downloads_and_player) {
    using namespace layout::downloads;
    CHECK(TABS_Y + TAB_H < LIST_Y);
    CHECK(LIST_Y + LIST_H <= CONTENT_BOTTOM);
    {
        using namespace layout::player;
        // Playback Options rows end at the safe edge (v1: ~1870)
        CHECK_EQ(OPTIONS_X + OPTIONS_PAD + OPTIONS_LIST_W, SAFE_RIGHT);
        CHECK_EQ(OPTIONS_X + OPTIONS_W, SCREEN_W);
        // HUD: title in the top fade, progress / time / hints in the bottom one, in this order
        CHECK(TITLE_Y >= SAFE_Y);
        CHECK(SUBTITLE_Y + LINE_28 < TOP_FADE_H);
        CHECK(BOTTOM_Y < BAR_Y);
        CHECK(BAR_Y + BAR_H < TIME_Y);
        CHECK(TIME_Y + LINE_28 < HINTS_Y);
    }
}
