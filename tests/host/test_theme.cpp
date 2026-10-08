// The near-black theme: text contrast on every surface (WCAG 2.x ratios, TV viewing distance), surfaces that
// stay dark, and no colour literals in screens / widgets / app code (everything goes through ui/theme.h).

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
                {"focus", FOCUS},
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
    // state colours are used as text on panels and rows
    for (const Named &bg: {Named{"surface", SURFACE}, Named{"card", CARD}, Named{"focus", FOCUS},
                           Named{"background", BACKGROUND}}) {
        expectContrast("success", SUCCESS, bg, 4.5);
        expectContrast("warning", WARNING, bg, 4.5);
        expectContrast("danger", DANGER, bg, 4.5);
        expectContrast("accent text", ACCENT_TEXT, bg, 4.5);
    }
    // filled controls: focused button (accent), primary button / selected chip, toasts
    expectContrast("textStrong", TEXT_STRONG, {"accent", ACCENT}, 3.0);   // large semibold captions only
    expectContrast("textPrimary", TEXT_PRIMARY, {"accentMuted", ACCENT_MUTED}, 7.0);
    expectContrast("textPrimary", TEXT_PRIMARY, {"successSurface", SUCCESS_SURFACE}, 4.5);
    expectContrast("textPrimary", TEXT_PRIMARY, {"dangerSurface", DANGER_SURFACE}, 4.5);
    expectContrast("textStrong", TEXT_STRONG, {"monogram", MONOGRAM[0]}, 4.5);
    for (const Rgba &m: MONOGRAM) {
        CHECK(contrast(TEXT_STRONG, m) >= 4.5);   // initials on every placeholder tile
    }
}

TEST(theme_focus_is_visible_and_surfaces_stay_dark) {
    // focus border (non-text UI component): >= 3:1 against everything it surrounds
    for (const Named &bg: surfaces()) {
        if (std::string(bg.name).find("bright") == std::string::npos) {
            expectContrast("accent border", ACCENT, bg, 3.0);
        }
    }
    // focused fills differ from what is around them (the border carries most of it)
    CHECK(contrast(FOCUS, SURFACE) >= 1.2);
    CHECK(contrast(CARD_HOVER, CARD) >= 1.3);
    CHECK(contrast(SURFACE, BACKGROUND) >= 1.1);
    // near-black / charcoal: no large bright surfaces (the accent only fills small things)
    for (const Rgba &s: {BACKGROUND, BACKGROUND_SECONDARY, SURFACE, CARD, SURFACE_ELEVATED, CARD_HOVER, FOCUS,
                         LOGO_TILE, ACCENT_MUTED}) {
        CHECK(luminance(s) < 0.06);
    }
    // the hierarchy gets lighter step by step
    CHECK(luminance(BACKGROUND) < luminance(SURFACE));
    CHECK(luminance(SURFACE) < luminance(CARD));
    CHECK(luminance(CARD) < luminance(SURFACE_ELEVATED));
    CHECK(luminance(SURFACE_ELEVATED) < luminance(CARD_HOVER));
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
