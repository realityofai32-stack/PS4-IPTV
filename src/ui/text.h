// UTF-8 text drawable.
//
// libcross2d's c2d::Text iterates bytes, so any non-ASCII character (Turkish, German, Cyrillic channel
// names...) renders as garbage. Label decodes UTF-8 to code points and builds glyph quads with the
// same libcross2d Font atlas and renderer draw call c2d::Text uses. It also adds what TV lists need:
// ellipsis truncation, word wrap with a line limit, alignment in a box, and cached geometry
// (rebuilt only when the text/style changes or the shared glyph atlas grows).

#ifndef PS4IPTV_UI_TEXT_H
#define PS4IPTV_UI_TEXT_H

#include <string>

#include "cross2d/c2d.h"

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

    // Loads the UI fonts (Inter) from the romfs. Must be called once after the renderer exists.
    bool loadFonts(const std::string &fontDir);

    c2d::Font *font(Weight weight);

    class Label : public c2d::Transformable {

    public:

        Label(const std::string &text, unsigned size, Weight weight = Weight::Regular,
              c2d::Color color = c2d::Color::White);

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

        std::string utf8Text;
        unsigned charSize;
        Weight weight;
        c2d::Color color;
        float maxWidth = 0;
        int maxLines = 1;
        Align align = Align::Left;
        float boxWidth = 0;

        c2d::VertexArray vertices;
        c2d::Vector2f atlasSize;
        float layoutWidth = 0;
        float layoutHeight = 0;
        bool dirty = true;
    };
}

#endif // PS4IPTV_UI_TEXT_H
