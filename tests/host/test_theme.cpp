// The V7 IPTV dark theme: text contrast on every surface (WCAG 2.x ratios, TV viewing distance) for every accent
// preset, surfaces that stay dark, and no colour literals in screens / widgets / app code (everything goes
// through ui/theme.h).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "../../src/ui/palette.h"

using namespace palette;

namespace {
    struct Named {
        const char *name;
        Rgba color;
    };

    // every surface text is drawn on (translucent ones composited the way they appear)
    std::vector<Named> surfaces() {
        return {{"background", BACKGROUND},
                {"backgroundSecondary", BACKGROUND_SECONDARY},
                {"surface", SURFACE},
                {"card", CARD},
                {"surfaceElevated", SURFACE_ELEVATED},
                {"cardHover", CARD_HOVER},
                {"logoTile", LOGO_TILE},
                {"panel", over(PANEL, BACKGROUND)},
                {"overlay over video", over(OVERLAY, VIDEO_BACKGROUND)},
                {"overlay over a bright frame", over(OVERLAY, rgb(235, 235, 235))},
                {"overlayPanel over a bright frame", over(OVERLAY_PANEL, rgb(235, 235, 235))},
                {"scrim over a card", over(SCRIM, CARD)}};
    }

    void expectContrast(const char *what, Rgba fg, const Named &bg, double minimum) {
        double c = contrast(fg, bg.color);
        if (c < minimum) {
            std::printf("  %s on %s: contrast %.2f < %.2f\n", what, bg.name, c, minimum);
        }
        CHECK(c >= minimum);
    }
}

TEST(theme_text_contrast_on_every_surface) {
    for (const Named &bg: surfaces()) {
        expectContrast("textStrong", TEXT_STRONG, bg, 7.0);
        expectContrast("textPrimary", TEXT_PRIMARY, bg, 7.0);
        expectContrast("textSecondary", TEXT_SECONDARY, bg, 4.5);
        expectContrast("textMuted", TEXT_MUTED, bg, 3.0);       // metadata, never body text
    }
    // state colours are used as text on panels and rows (never accent-coloured)
    for (const Named &bg: {Named{"surface", SURFACE}, Named{"card", CARD}, Named{"background", BACKGROUND}}) {
        expectContrast("success", SUCCESS, bg, 4.5);
        expectContrast("warning", WARNING, bg, 4.5);
        expectContrast("danger", DANGER, bg, 4.5);
    }
    expectContrast("textPrimary", TEXT_PRIMARY, {"successSurface", SUCCESS_SURFACE}, 4.5);
    expectContrast("textPrimary", TEXT_PRIMARY, {"dangerSurface", DANGER_SURFACE}, 4.5);
    expectContrast("textStrong", TEXT_STRONG, {"monogram", MONOGRAM[0]}, 4.5);
    for (const Rgba &m: MONOGRAM) {
        CHECK(contrast(TEXT_STRONG, m) >= 4.5);   // initials on every placeholder tile
    }
}

// every accent preset, on every surface it appears with
TEST(theme_every_accent_preset_is_readable) {
    CHECK(ACCENT_COUNT >= 8);
    CHECK_EQ(DEFAULT_ACCENT, 0);
    CHECK_EQ(std::string(ACCENTS[DEFAULT_ACCENT].key), std::string("blue"));
    for (int i = 0; i < ACCENT_COUNT; i++) {
        const Accent &a = ACCENTS[i];
        // focused row fill: text stays readable, and it differs from the rows around it
        expectContrast("textPrimary", TEXT_PRIMARY, {a.key, a.focus}, 7.0);
        expectContrast("textSecondary", TEXT_SECONDARY, {a.key, a.focus}, 4.5);
        CHECK(contrast(a.focus, SURFACE) >= 1.15);
        // selected chip / tab at rest
        expectContrast("textPrimary", TEXT_PRIMARY, {a.key, a.muted}, 7.0);
        // captions on an accent fill: large semibold text only (primary button, focused chip)
        expectContrast("onAccent", a.onAccent, {a.key, a.accent}, 3.0);
        // the accent as text (eyebrows, links, brand) on the dark surfaces
        for (const Named &bg: {Named{"surface", SURFACE}, Named{"card", CARD}, Named{"background", BACKGROUND},
                               Named{"focus", a.focus}}) {
            expectContrast("accent text", a.text, bg, 4.5);
        }
        // focus border / progress (non-text UI component): >= 3:1 against everything it surrounds
        for (const Named &bg: surfaces()) {
            if (std::string(bg.name).find("bright") == std::string::npos) {
                expectContrast("accent border", a.accent, bg, 3.0);
            }
        }
        expectContrast("accent border", a.accent, {"focus", a.focus}, 3.0);
        // the accent tints stay dark: no large bright surfaces
        CHECK(luminance(a.focus) < 0.06);
        CHECK(luminance(a.muted) < 0.06);
        // presets are distinct and addressable by their settings key
        CHECK_EQ(accentIndex(a.key), i);
        for (int j = 0; j < i; j++) {
            CHECK(std::string(ACCENTS[j].key) != a.key);
        }
    }
    CHECK_EQ(accentIndex("no-such-colour"), -1);
    CHECK_EQ(accentIndex(""), -1);
    CHECK_EQ(accentIndex(nullptr), -1);
}

TEST(theme_focus_is_visible_and_surfaces_stay_dark) {
    // focused fills differ from what is around them (the border carries most of it)
    CHECK(contrast(CARD_HOVER, CARD) >= 1.3);
    CHECK(contrast(SURFACE, BACKGROUND) >= 1.1);
    // near-black / charcoal: no large bright surfaces (the accent only fills small things)
    for (const Rgba &s: {BACKGROUND, BACKGROUND_SECONDARY, SURFACE, CARD, SURFACE_ELEVATED, CARD_HOVER,
                         LOGO_TILE}) {
        CHECK(luminance(s) < 0.06);
    }
    // the hierarchy gets lighter step by step
    CHECK(luminance(BACKGROUND) < luminance(SURFACE));
    CHECK(luminance(SURFACE) < luminance(CARD));
    CHECK(luminance(CARD) < luminance(SURFACE_ELEVATED));
    CHECK(luminance(SURFACE_ELEVATED) < luminance(CARD_HOVER));
}

// v2 lists are separators on the background: the focused row is only its tinted surface and a 4 px accent edge
// (no outline), so both must stand out from the page for every preset; dividers stay quiet; dimmed artwork
// (the posters / cards that are not focused) stays clearly visible
TEST(theme_v2_row_focus_dividers_and_artwork_dim) {
    for (const Accent &a: ACCENTS) {
        double surface = contrast(a.focus, BACKGROUND);
        if (surface < 1.3) {
            std::printf("  %s: focused row surface contrast %.2f < 1.30\n", a.key, surface);
        }
        CHECK(surface >= 1.3);
        CHECK(contrast(a.accent, a.focus) >= 3.0);         // the edge reads on the focused surface
        CHECK(contrast(a.accent, BACKGROUND) >= 4.5);      // and next to unfocused rows
    }
    double divider = contrast(DIVIDER_SOFT, BACKGROUND);
    CHECK(divider >= 1.1 && divider <= 1.7);
    CHECK(ARTWORK_DIM.r >= 190 && ARTWORK_DIM.r <= 230);
    CHECK(ARTWORK_DIM.g >= 190 && ARTWORK_DIM.g <= 230);
    CHECK(ARTWORK_DIM.b >= 190 && ARTWORK_DIM.b <= 230);
}

// v3: the base reads as professional black + charcoal, never dark navy; the accent stays on small things. The
// focus / selected surfaces derived from an accent are raised neutral with a barely visible tint, so a focused row
// is never a large red / purple / green slab (v7-ui-2 Playback Options with Red).
TEST(theme_v3_neutral_black_base_and_restrained_accent) {
    auto chroma = [](Rgba c) { return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b}); };
    auto coolCast = [](Rgba c) { return (int) c.b - (int) c.r; };
    for (const Named &bg: {Named{"background", BACKGROUND}, Named{"backgroundSecondary", BACKGROUND_SECONDARY},
                           Named{"surface", SURFACE}, Named{"card", CARD}, Named{"surfaceElevated", SURFACE_ELEVATED},
                           Named{"cardHover", CARD_HOVER}, Named{"logoTile", LOGO_TILE}, Named{"divider", DIVIDER},
                           Named{"dividerSoft", DIVIDER_SOFT}, Named{"panel", PANEL}, Named{"focusNeutral", FOCUS_NEUTRAL},
                           Named{"overlay", OVERLAY}, Named{"overlayPanel", OVERLAY_PANEL}}) {
        if (coolCast(bg.color) > 13 || chroma(bg.color) > 13) {
            std::printf("  %s (%d, %d, %d) has a blue cast\n", bg.name, bg.color.r, bg.color.g, bg.color.b);
        }
        CHECK(coolCast(bg.color) <= 13);   // v2 was 9..31 (navy)
        CHECK(chroma(bg.color) <= 13);
    }
    // the page itself is nearly black
    CHECK(luminance(BACKGROUND) < 0.003);
    CHECK(BACKGROUND.r <= 8 && BACKGROUND.g <= 8 && BACKGROUND.b <= 10);
    // texture modulation of backdrops is grey (dims the artwork without tinting it)
    CHECK(chroma(BACKDROP_TINT) <= 4 && chroma(ARTWORK_DIM) <= 12);
    for (const Accent &a: ACCENTS) {
        // the focused / selected surfaces stay within a few levels of the neutral base, whatever the accent
        auto distance = [](Rgba x, Rgba y) {
            return std::max({std::abs(x.r - y.r), std::abs(x.g - y.g), std::abs(x.b - y.b)});
        };
        if (distance(a.focus, FOCUS_NEUTRAL) > 12) {
            std::printf("  %s focus (%d, %d, %d) is too tinted\n", a.key, a.focus.r, a.focus.g, a.focus.b);
        }
        CHECK(distance(a.focus, FOCUS_NEUTRAL) <= 12);
        CHECK(distance(a.muted, rgb(38, 42, 49)) <= 24);
        CHECK(chroma(a.focus) <= 22);
        // ...and are no brighter than a raised surface: the edge / ring carries the accent
        CHECK(luminance(a.focus) < luminance(CARD_HOVER) * 1.25);
    }
}

TEST(theme_no_colour_literals_outside_the_theme) {
    const char *src = std::getenv("PS4IPTV_SOURCE_DIR");
    if (!src) {
        std::printf("  (skipped: PS4IPTV_SOURCE_DIR not set)\n");
        return;
    }
    namespace fsys = std::filesystem;
    const std::regex literal(R"(\bColor\s*\(\s*\d|\bColor::[A-Z]|static\s+const\s+Color\b)");
    int hits = 0;
    int files = 0;
    for (const char *dir: {"screens", "ui", "app"}) {
        for (const auto &e: fsys::recursive_directory_iterator(fsys::path(src) / dir)) {
            std::string name = e.path().filename().string();
            if (!e.is_regular_file() || name == "theme.h" || name == "palette.h") {
                continue;
            }
            if (e.path().extension() != ".cpp" && e.path().extension() != ".h") {
                continue;
            }
            files++;
            std::ifstream in(e.path());
            std::string line;
            int n = 0;
            while (std::getline(in, line)) {
                n++;
                if (std::regex_search(line, literal)) {
                    std::printf("  %s:%d colour literal: %s\n", name.c_str(), n, line.c_str());
                    hits++;
                }
            }
        }
    }
    CHECK(files > 20);
    CHECK_EQ(hits, 0);
}

// Focus treatment is one design-system rule: every focused outline uses a theme thickness token (no per-screen
// numbers), and every accent fill on a text control picks its caption colour from the preset (onAccent).
TEST(theme_focus_outlines_use_tokens) {
    const char *src = std::getenv("PS4IPTV_SOURCE_DIR");
    if (!src) {
        std::printf("  (skipped: PS4IPTV_SOURCE_DIR not set)\n");
        return;
    }
    namespace fsys = std::filesystem;
    const std::regex rawFocus(R"(setOutlineThickness\(\s*(focused|f|foc|focus\s*==\s*\w+)\s*\?\s*\d)");
    int hits = 0;
    for (const char *dir: {"screens", "ui"}) {
        for (const auto &e: fsys::recursive_directory_iterator(fsys::path(src) / dir)) {
            if (!e.is_regular_file() || e.path().extension() != ".cpp") {
                continue;
            }
            std::ifstream in(e.path());
            std::string line;
            int n = 0;
            while (std::getline(in, line)) {
                n++;
                if (std::regex_search(line, rawFocus)) {
                    std::printf("  %s:%d focus outline without a token: %s\n", e.path().filename().string().c_str(), n,
                                line.c_str());
                    hits++;
                }
            }
        }
    }
    CHECK_EQ(hits, 0);
}
