#include <cmath>
#include <cstring>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "font_fallback.h"
#include "glyph_cache.h"
#include "text.h"
#include "text_bidi.h"
#include "../core/utf8.h"
#include "../platform/log.h"

namespace ui {

    namespace {
        // font indices: the two UI weights, then the fallback fonts (glyph by glyph, see font_fallback.h)
        enum FontIndex {
            FONT_REGULAR = 0,
            FONT_SEMIBOLD = 1,
            FONT_FALLBACK_SCRIPTS = 2,   // DejaVu Sans: Arabic, Hebrew, Armenian, Georgian, U+FFFD...
            FONT_FALLBACK_CJK = 3,       // Droid Sans Fallback: kana, Hangul, CJK
            FONT_COUNT = 4
        };
        const char *const FONT_FILES[FONT_COUNT] = {"Inter-Regular.ttf", "Inter-SemiBold.ttf", "DejaVuSans.ttf",
                                                    "DroidSansFallback.ttf"};

        c2d::Font *g_fonts[FONT_COUNT] = {nullptr, nullptr, nullptr, nullptr};
        // FreeType faces used only to ask which font has a code point
        FT_Library g_ft = nullptr;
        FT_Face g_faces[FONT_COUNT] = {nullptr, nullptr, nullptr, nullptr};
        FontChooser g_chooser;

        // Inter metrics (units per em 2048): ascender 1984, descender -494
        const float ASCENT = 0.969f;
        const float LINE_HEIGHT = 1.25f;
        const char32_t ELLIPSIS = 0x2026;

        int g_unplaced = 0;

        // GlyphCache backend over the c2d::Font objects (index = FontIndex)
        struct C2dGlyphBackend {
            using Glyph = c2d::Glyph;

            void preparePage(int font, unsigned size) {
                // creates the page: a 128x128 GLTexture whose CPU pixels are uninitialised malloc memory
                c2d::Texture *tex = g_fonts[font]->getTexture(size);
                void *pixels = nullptr;
                int pitch = 0;
                if (tex != nullptr && tex->lock(nullptr, &pixels, &pitch) == 0 && pixels != nullptr && pitch > 0) {
                    memset(pixels, 0, (size_t) pitch * (size_t) tex->getTextureRect().height);
                }
            }

            Glyph load(int font, unsigned size, char32_t cp) {
                return g_fonts[font]->getGlyph(cp, size, false);
            }

            void upload(int font, unsigned size) {
                c2d::Texture *tex = g_fonts[font]->getTexture(size);
                if (tex != nullptr) {
                    tex->unlock();  // GLTexture: glTexSubImage2D of the whole page from its CPU copy
                }
                g_fonts[font]->setDirtyTex(false);
            }

            std::pair<int, int> pageSize(int font, unsigned size) {
                c2d::Texture *tex = g_fonts[font]->getTexture(size);
                if (tex == nullptr) {
                    return {0, 0};
                }
                return {tex->getTextureRect().width, tex->getTextureRect().height};
            }
        };

        C2dGlyphBackend g_backend;
        GlyphCache<C2dGlyphBackend> g_glyphs(g_backend);

        int fontIndex(Weight w) {
            return w == Weight::SemiBold ? FONT_SEMIBOLD : FONT_REGULAR;
        }

        // the font that draws `cp` in a label of UI font `primary`
        int fontOf(int primary, char32_t cp) {
            return g_chooser.pick(primary, cp);
        }

        struct Line {
            std::u32string text;
            float width = 0;
        };

        float measure(int primary, unsigned size, const std::u32string &s) {
            float w = 0;
            char32_t prev = 0;
            int prevFont = -1;
            for (char32_t cp: s) {
                int f = fontOf(primary, cp);
                // kerning only between two glyphs of the same font
                float kern = prev && f == prevFont ? g_fonts[f]->getKerning(prev, cp, size) : 0.0f;
                w += kern + g_glyphs.get(f, size, cp).advance;
                prev = cp;
                prevFont = f;
            }
            return w;
        }

        // shortens s until s + "…" fits
        std::u32string ellipsize(int primary, unsigned size, std::u32string s, float maxWidth) {
            if (measure(primary, size, s) <= maxWidth) {
                return s;
            }
            float ellipsis = g_glyphs.get(primary, size, ELLIPSIS).advance;
            while (!s.empty() && measure(primary, size, s) + ellipsis > maxWidth) {
                s.pop_back();
            }
            while (!s.empty() && s.back() == ' ') {
                s.pop_back();
            }
            return s + ELLIPSIS;
        }

        bool openCoverageFace(int i, const std::string &path) {
            if (g_ft == nullptr && FT_Init_FreeType(&g_ft) != 0) {
                g_ft = nullptr;
                return false;
            }
            return FT_New_Face(g_ft, path.c_str(), 0, &g_faces[i]) == 0;
        }
    }

    bool loadFonts(const std::string &dir) {
        bool ok = true;
        for (int i = FONT_REGULAR; i <= FONT_SEMIBOLD; i++) {
            auto *f = new c2d::Font();
            if (!f->loadFromFile(dir + FONT_FILES[i])) {
                LOG_E("ui", "font %s%s failed to load, using the built-in font", dir.c_str(), FONT_FILES[i]);
                f->loadDefault();
                ok = false;
            }
            g_fonts[i] = f;
        }
        // fallback fonts: optional. Without them (or without coverage data) every code point uses the UI font
        // and missing scripts show the missing-glyph box, exactly as before.
        std::vector<int> fallbacks;
        bool coverage = openCoverageFace(FONT_REGULAR, dir + FONT_FILES[FONT_REGULAR]);
        for (int i = FONT_FALLBACK_SCRIPTS; i < FONT_COUNT; i++) {
            auto *f = new c2d::Font();
            if (coverage && f->loadFromFile(dir + FONT_FILES[i]) && openCoverageFace(i, dir + FONT_FILES[i])) {
                g_fonts[i] = f;
                fallbacks.push_back(i);
            } else {
                LOG_W("ui", "fallback font %s not available", FONT_FILES[i]);
                delete f;
            }
        }
        if (coverage && !fallbacks.empty()) {
            g_chooser = FontChooser(FONT_REGULAR, fallbacks, [](int font, char32_t cp) {
                return g_faces[font] != nullptr && FT_Get_Char_Index(g_faces[font], (FT_ULong) cp) != 0;
            });
        }
        LOG_I("ui", "fonts: Inter + %d fallback font(s), right-to-left shaping %s", (int) fallbacks.size(),
              bidiAvailable() ? "FriBidi" : "off");
        return ok;
    }

    c2d::Font *font(Weight w) {
        return g_fonts[fontIndex(w)];
    }

    TextStats textStats() {
        auto s = g_glyphs.getStats();
        TextStats t;
        t.pages = s.pages;
        t.glyphs = s.glyphs;
        t.uploads = s.uploads;
        t.resizes = s.resizes;
        t.unplaced = g_unplaced;
        t.fallbackFonts = (g_fonts[FONT_FALLBACK_SCRIPTS] != nullptr) + (g_fonts[FONT_FALLBACK_CJK] != nullptr);
        t.codePointsLookedUp = g_chooser.lookups();
        t.missingGlyphs = g_chooser.missingGlyphs();
        return t;
    }

    Label::Label(const std::string &text, unsigned size, Weight w, c2d::Color c)
            : utf8Text(text), charSize(size), weight(w), color(c), vertices(c2d::Triangles) {}

    Label::~Label() = default;

    void Label::setText(const std::string &text) {
        if (text != utf8Text) {
            utf8Text = text;
            dirty = true;
        }
    }

    void Label::setColor(const c2d::Color &c) {
        if (c != color) {
            color = c;
            dirty = true;
        }
    }

    void Label::setCharSize(unsigned size) {
        if (size != charSize) {
            charSize = size;
            dirty = true;
        }
    }

    void Label::setWeight(Weight w) {
        if (w != weight) {
            weight = w;
            dirty = true;
        }
    }

    void Label::setMaxWidth(float w) {
        if (w != maxWidth) {
            maxWidth = w;
            dirty = true;
        }
    }

    void Label::setMaxLines(int lines) {
        if (lines != maxLines) {
            maxLines = lines < 1 ? 1 : lines;
            dirty = true;
        }
    }

    void Label::setAlign(Align a, float box) {
        if (a != align || box != boxWidth) {
            align = a;
            boxWidth = box;
            dirty = true;
        }
    }

    float Label::lineHeight() const {
        return std::round((float) charSize * LINE_HEIGHT);
    }

    float Label::centerOffset(unsigned size, float h) {
        return std::round((h - std::round((float) size * LINE_HEIGHT)) / 2.0f);
    }

    float Label::width() {
        if (dirty) {
            rebuild();
        }
        return layoutWidth;
    }

    float Label::height() {
        if (dirty) {
            rebuild();
        }
        return layoutHeight;
    }

    c2d::FloatRect Label::getLocalBounds() const {
        return {0, 0, boxWidth > 0 ? boxWidth : layoutWidth, layoutHeight};
    }

    c2d::VertexArray &Label::arrayFor(int font) {
        if (font == fontIndex(weight)) {
            return vertices;
        }
        auto &slot = fallbackVertices[font - FONT_FALLBACK_SCRIPTS];
        if (!slot) {
            slot.reset(new c2d::VertexArray(c2d::Triangles));   // only labels that show another script
        }
        return *slot;
    }

    void Label::rebuild() {
        dirty = false;
        vertices.clear();
        for (auto &v: fallbackVertices) {
            if (v) {
                v->clear();
            }
        }
        usedFonts = 0;
        layoutWidth = 0;
        layoutHeight = 0;
        c2d::Font *f = font(weight);
        int fi = fontIndex(weight);
        if (f == nullptr || utf8Text.empty()) {
            vertices.update();
            for (auto &v: fallbackVertices) {
                if (v) {
                    v->update();
                }
            }
            return;
        }

        // 1. layout into lines (explicit newlines, word wrap, ellipsis)
        std::u32string all = utf8::decode(utf8Text);
        std::vector<Line> lines;
        std::u32string paragraph;
        auto flushParagraph = [&](const std::u32string &para) {
            if (maxWidth <= 0 || maxLines <= 1) {
                lines.push_back({para, 0});
                return;
            }
            std::u32string current;
            size_t i = 0;
            while (i <= para.size()) {
                size_t next = para.find(U' ', i);
                if (next == std::u32string::npos) {
                    next = para.size();
                }
                std::u32string word = para.substr(i, next - i);
                std::u32string candidate = current.empty() ? word : current + U' ' + word;
                if (!current.empty() && measure(fi, charSize, candidate) > maxWidth) {
                    lines.push_back({current, 0});
                    current = word;
                } else {
                    current = candidate;
                }
                i = next + 1;
            }
            lines.push_back({current, 0});
        };
        for (char32_t cp: all) {
            if (cp == U'\n') {
                flushParagraph(paragraph);
                paragraph.clear();
            } else if (cp != U'\r') {
                // invalid UTF-8 shows as U+FFFD (from a fallback font), else '?'
                paragraph.push_back(cp == U'\t' ? U' ' : cp == 0xFFFD && fontOf(fi, cp) == fi ? U'?' : cp);
            }
        }
        flushParagraph(paragraph);

        if ((int) lines.size() > maxLines && maxLines >= 1) {
            lines.resize((size_t) maxLines);
            lines.back().text += ELLIPSIS;  // shortened below if needed
        }
        for (auto &line: lines) {
            if (maxWidth > 0) {
                line.text = ellipsize(fi, charSize, line.text, maxWidth);
            }
            // right-to-left lines: visual order and joined Arabic letters (text_bidi.h)
            if (hasRtl(line.text)) {
                line.text = visualLine(line.text);
            }
        }

        // 2. measuring loads every glyph into its page before the page sizes are read (pages can grow)
        for (auto &line: lines) {
            line.width = measure(fi, charSize, line.text);
        }
        c2d::Vector2f atlasSize[FONT_COUNT];
        for (int i = 0; i < FONT_COUNT; i++) {
            if (g_fonts[i] != nullptr && (i == fi || i >= FONT_FALLBACK_SCRIPTS)) {
                atlasGeneration[i] = g_glyphs.generation(i, charSize);
            }
        }

        // 3. quads, one vertex array per font (each is drawn with its own atlas page)
        float lh = lineHeight();
        float baseline = std::round((float) charSize * ASCENT) + std::round((lh - (float) charSize * 1.211f) / 2.0f);
        for (size_t li = 0; li < lines.size(); li++) {
            const Line &line = lines[li];
            float x = 0;
            if (boxWidth > 0 && align == Align::Center) {
                x = std::round((boxWidth - line.width) / 2.0f);
            } else if (boxWidth > 0 && align == Align::Right) {
                x = std::round(boxWidth - line.width);
            }
            float y = baseline + (float) li * lh;
            char32_t prev = 0;
            int prevFont = -1;
            for (char32_t cp: line.text) {
                int gf = fontOf(fi, cp);
                if (prev && gf == prevFont) {
                    x += g_fonts[gf]->getKerning(prev, cp, charSize);
                }
                const c2d::Glyph &g = g_glyphs.get(gf, charSize, cp);
                if (!(usedFonts & (1u << gf))) {
                    usedFonts |= 1u << gf;
                    atlasSize[gf] = g_fonts[gf]->getTexture(charSize)->getSize();
                    atlasGeneration[gf] = g_glyphs.generation(gf, charSize);
                }
                c2d::Vector2f atlas = atlasSize[gf];
                float left = std::round(x) + g.bounds.left;
                float top = y + g.bounds.top;
                float right = left + g.bounds.width;
                float bottom = top + g.bounds.height;
                bool visible = g.bounds.width > 0 && g.bounds.height > 0;
                bool placed = g.textureRect.width > 0 && g.textureRect.height > 0 && atlas.x > 0 && atlas.y > 0;
                if (visible && !placed) {
                    g_unplaced++;  // its page reached libcross2d's 1024x1024 limit: skip, don't draw a box
                } else if (visible) {
                    float u1 = (float) g.textureRect.left / atlas.x;
                    float v1 = (float) g.textureRect.top / atlas.y;
                    float u2 = (float) (g.textureRect.left + g.textureRect.width) / atlas.x;
                    float v2 = (float) (g.textureRect.top + g.textureRect.height) / atlas.y;
                    c2d::VertexArray &va = arrayFor(gf);
                    va.append(c2d::Vertex({left, top}, color, {u1, v1}));
                    va.append(c2d::Vertex({right, top}, color, {u2, v1}));
                    va.append(c2d::Vertex({left, bottom}, color, {u1, v2}));
                    va.append(c2d::Vertex({left, bottom}, color, {u1, v2}));
                    va.append(c2d::Vertex({right, top}, color, {u2, v1}));
                    va.append(c2d::Vertex({right, bottom}, color, {u2, v2}));
                }
                x += (float) g.advance;
                prev = cp;
                prevFont = gf;
            }
            layoutWidth = std::max(layoutWidth, line.width);
        }
        layoutHeight = lh * (float) lines.size();
        vertices.update();
        for (auto &v: fallbackVertices) {
            if (v) {
                v->update();
            }
        }
    }

    bool Label::atlasMoved() const {
        if (utf8Text.empty()) {
            return false;
        }
        int fi = fontIndex(weight);
        if (g_glyphs.generation(fi, charSize) != atlasGeneration[fi]) {
            return true;
        }
        for (int f = FONT_FALLBACK_SCRIPTS; f < FONT_COUNT; f++) {
            if ((usedFonts & (1u << f)) && g_glyphs.generation(f, charSize) != atlasGeneration[f]) {
                return true;
            }
        }
        return false;
    }

    void Label::onUpdate() {
        if (atlasMoved()) {
            dirty = true;  // another label grew a shared glyph page: texture coordinates moved
        }
        if (dirty) {
            rebuild();
        }
        C2DObject::onUpdate();
    }

    void Label::onDraw(c2d::Transform &transform, bool draw) {
        if (draw) {
            // a label updated after this one in the same frame may have grown a page
            if (dirty || atlasMoved()) {
                rebuild();
            }
            // new glyph pixels exist only in the CPU copy of their page until uploaded
            g_glyphs.flush();
            c2d::Transform combined = transform * getTransform();
            if (vertices.getVertexCount() > 0) {
                c2d_renderer->draw(&vertices, combined, font(weight)->getTexture(charSize));
            }
            for (int k = 0; k < FALLBACK_FONTS; k++) {
                const auto &v = fallbackVertices[k];
                if (v && v->getVertexCount() > 0) {
                    c2d_renderer->draw(v.get(), combined, g_fonts[FONT_FALLBACK_SCRIPTS + k]->getTexture(charSize));
                }
            }
        }
        C2DObject::onDraw(transform, draw);
    }
}
