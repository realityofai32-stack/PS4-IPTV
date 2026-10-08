// UTF-8 text drawable.
//
// libcross2d's c2d::Text iterates bytes, so any non-ASCII character (Turkish, German, Cyrillic channel
// names...) renders as garbage. Label decodes UTF-8 to code points and builds glyph quads with the
// same libcross2d Font atlas and renderer draw call c2d::Text uses. Glyphs come through ui::GlyphCache,
// which uploads new glyph pixels to the GPU before every draw (see glyph_cache.h for the PS4 failure that
// made this explicit). It also adds what TV lists need: ellipsis truncation, word wrap with a line limit,
// alignment in a box, and cached geometry (rebuilt only when the text/style changes or the shared glyph
// atlas page grows).
//
// Code points the UI font (Inter: Latin, Turkish, Greek, Cyrillic) does not have are drawn with fallback fonts,
// glyph by glyph (font_fallback.h): DejaVu Sans (Arabic, Hebrew, Armenian, Georgian...) and Droid Sans Fallback
// (Japanese kana, Hangul, CJK). Their glyphs go through the same GlyphCache (own atlas pages, same dirty-page
// upload before every draw); a label draws one vertex array per font it uses. Right-to-left lines are reordered
// and Arabic is joined with FriBidi (text_bidi.h). Code points no font has show the missing-glyph box.

#ifndef PS4IPTV_UI_TEXT_H
#define PS4IPTV_UI_TEXT_H

#include <memory>
#include <string>

#include "cross2d/c2d.h"
#include "theme.h"

namespace ui {

    enum class Weight {
        Regular,
        SemiBold
    };

    enum class Align {
        Left,
        Center,
        Right
    };

    // Loads the UI fonts (Inter) and the fallback fonts from the romfs. Must be called once after the renderer
    // exists. A missing fallback font only reduces script coverage.
    bool loadFonts(const std::string &fontDir);

    c2d::Font *font(Weight weight);

    // glyph atlas counters, shown by the text rendering test screen
    struct TextStats {
        int pages = 0;      // atlas pages (font weight x pixel size)
        int glyphs = 0;     // glyphs rasterised
        int uploads = 0;    // page uploads to the GPU
        int resizes = 0;    // page texture growths
        int unplaced = 0;   // glyph quads skipped because their page was full (should stay 0)
        int fallbackFonts = 0;        // fallback fonts loaded (0 - 2)
        int codePointsLookedUp = 0;   // code points beyond Latin whose font was looked up
        int missingGlyphs = 0;        // of those, in no font (drawn as the missing-glyph box)
    };

    TextStats textStats();

    class Label : public c2d::Transformable {

    public:

        Label(const std::string &text, unsigned size, Weight weight = Weight::Regular,
              c2d::Color color = theme::textPrimary());

        ~Label() override;

        void setText(const std::string &text);

        const std::string &getText() const { return utf8Text; }

        void setColor(const c2d::Color &color);

        void setCharSize(unsigned size);

        void setWeight(Weight weight);

        // > 0: single line truncated with an ellipsis, or wrapped when maxLines > 1
        void setMaxWidth(float width);

        void setMaxLines(int lines);

        // alignment inside a box of the given width (0 = no box: left aligned at x)
        void setAlign(Align align, float boxWidth);

        float lineHeight() const;

        // measured size of the current layout
        float width();

        float height();

        c2d::FloatRect getLocalBounds() const override;

        // vertical offset that centres one line of this size in a box of height h
        static float centerOffset(unsigned size, float h);

    protected:

        void onUpdate() override;

        void onDraw(c2d::Transform &transform, bool draw) override;

    private:

        void rebuild();

        // a glyph page this label uses grew since its geometry was built
        bool atlasMoved() const;

        // the vertex array of the glyphs drawn with `font` (fallback arrays are created on first use)
        c2d::VertexArray &arrayFor(int font);

        static const int FALLBACK_FONTS = 2;

        std::string utf8Text;
        unsigned charSize;
        Weight weight;
        c2d::Color color;
        float maxWidth = 0;
        int maxLines = 1;
        Align align = Align::Left;
        float boxWidth = 0;

        c2d::VertexArray vertices;                                      // UI font glyphs
        std::unique_ptr<c2d::VertexArray> fallbackVertices[FALLBACK_FONTS];   // other scripts
        unsigned atlasGeneration[2 + FALLBACK_FONTS] = {0, 0, 0, 0};
        unsigned usedFonts = 0;                                         // bit per font index
        float layoutWidth = 0;
        float layoutHeight = 0;
        bool dirty = true;
    };
}

#endif // PS4IPTV_UI_TEXT_H
