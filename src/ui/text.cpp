#include <cmath>
#include <cstring>
#include <vector>

#include "glyph_cache.h"
#include "text.h"
#include "../core/utf8.h"
#include "../platform/log.h"

namespace ui {

    namespace {
        c2d::Font *g_fonts[2] = {nullptr, nullptr};

        // Inter metrics (units per em 2048): ascender 1984, descender -494
        const float ASCENT = 0.969f;
        const float LINE_HEIGHT = 1.25f;
        const char32_t ELLIPSIS = 0x2026;

        int g_unplaced = 0;

        // GlyphCache backend over the two c2d::Font objects (index = Weight)
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
            return w == Weight::SemiBold ? 1 : 0;
        }

        struct Line {
            std::u32string text;
            float width = 0;
        };

        float advanceOf(c2d::Font *font, int fi, unsigned size, char32_t prev, char32_t cp) {
            float kern = prev ? font->getKerning(prev, cp, size) : 0.0f;
            return kern + g_glyphs.get(fi, size, cp).advance;
        }

        float measure(c2d::Font *font, int fi, unsigned size, const std::u32string &s) {
            float w = 0;
            char32_t prev = 0;
            for (char32_t cp: s) {
                w += advanceOf(font, fi, size, prev, cp);
                prev = cp;
            }
            return w;
        }

        // shortens s until s + "…" fits
        std::u32string ellipsize(c2d::Font *font, int fi, unsigned size, std::u32string s, float maxWidth) {
            if (measure(font, fi, size, s) <= maxWidth) {
                return s;
            }
            float ellipsis = advanceOf(font, fi, size, 0, ELLIPSIS);
            while (!s.empty() && measure(font, fi, size, s) + ellipsis > maxWidth) {
                s.pop_back();
            }
            while (!s.empty() && s.back() == ' ') {
                s.pop_back();
            }
            return s + ELLIPSIS;
        }
    }

    bool loadFonts(const std::string &dir) {
        const char *files[2] = {"Inter-Regular.ttf", "Inter-SemiBold.ttf"};
        bool ok = true;
        for (int i = 0; i < 2; i++) {
            auto *f = new c2d::Font();
            if (!f->loadFromFile(dir + files[i])) {
                LOG_E("ui", "font %s%s failed to load, using the built-in font", dir.c_str(), files[i]);
                f->loadDefault();
                ok = false;
            }
            g_fonts[i] = f;
        }
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
        return t;
    }

    Label::Label(const std::string &text, unsigned size, Weight w, c2d::Color c)
            : utf8Text(text), charSize(size), weight(w), color(c), vertices(c2d::Triangles) {}

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

    void Label::rebuild() {
        dirty = false;
        vertices.clear();
        layoutWidth = 0;
        layoutHeight = 0;
        c2d::Font *f = font(weight);
        int fi = fontIndex(weight);
        if (f == nullptr || utf8Text.empty()) {
            vertices.update();
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
                if (!current.empty() && measure(f, fi, charSize, candidate) > maxWidth) {
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
                // Inter has no U+FFFD: show invalid UTF-8 as '?'
                paragraph.push_back(cp == U'\t' ? U' ' : cp == 0xFFFD ? U'?' : cp);
            }
        }
        flushParagraph(paragraph);

        if ((int) lines.size() > maxLines && maxLines >= 1) {
            lines.resize((size_t) maxLines);
            lines.back().text += ELLIPSIS;  // shortened below if needed
        }
        for (auto &line: lines) {
            if (maxWidth > 0) {
                line.text = ellipsize(f, fi, charSize, line.text, maxWidth);
            }
        }

        // 2. measuring loads every glyph into the page before its size is read (the page can grow)
        for (auto &line: lines) {
            line.width = measure(f, fi, charSize, line.text);
        }
        atlasGeneration = g_glyphs.generation(fi, charSize);
        c2d::Vector2f atlasSize = f->getTexture(charSize)->getSize();
        if (atlasSize.x <= 0 || atlasSize.y <= 0) {
            vertices.update();
            return;
        }

        // 3. quads
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
            for (char32_t cp: line.text) {
                if (prev) {
                    x += f->getKerning(prev, cp, charSize);
                }
                const c2d::Glyph &g = g_glyphs.get(fi, charSize, cp);
                float left = std::round(x) + g.bounds.left;
                float top = y + g.bounds.top;
                float right = left + g.bounds.width;
                float bottom = top + g.bounds.height;
                float u1 = (float) g.textureRect.left / atlasSize.x;
                float v1 = (float) g.textureRect.top / atlasSize.y;
                float u2 = (float) (g.textureRect.left + g.textureRect.width) / atlasSize.x;
                float v2 = (float) (g.textureRect.top + g.textureRect.height) / atlasSize.y;
                bool visible = g.bounds.width > 0 && g.bounds.height > 0;
                bool placed = g.textureRect.width > 0 && g.textureRect.height > 0;
                if (visible && !placed) {
                    g_unplaced++;  // its page reached libcross2d's 1024x1024 limit: skip, don't draw a box
                } else if (visible) {
                    vertices.append(c2d::Vertex({left, top}, color, {u1, v1}));
                    vertices.append(c2d::Vertex({right, top}, color, {u2, v1}));
                    vertices.append(c2d::Vertex({left, bottom}, color, {u1, v2}));
                    vertices.append(c2d::Vertex({left, bottom}, color, {u1, v2}));
                    vertices.append(c2d::Vertex({right, top}, color, {u2, v1}));
                    vertices.append(c2d::Vertex({right, bottom}, color, {u2, v2}));
                }
                x += (float) g.advance;
                prev = cp;
            }
            layoutWidth = std::max(layoutWidth, line.width);
        }
        layoutHeight = lh * (float) lines.size();
        vertices.update();
    }

    bool Label::atlasMoved() const {
        return !utf8Text.empty() && g_glyphs.generation(fontIndex(weight), charSize) != atlasGeneration;
    }

    void Label::onUpdate() {
        if (atlasMoved()) {
            dirty = true;  // another label grew the shared glyph page: texture coordinates moved
        }
        if (dirty) {
            rebuild();
        }
        C2DObject::onUpdate();
    }

    void Label::onDraw(c2d::Transform &transform, bool draw) {
        if (draw) {
            // a label updated after this one in the same frame may have grown the page
            if (dirty || atlasMoved()) {
                rebuild();
            }
            // new glyph pixels exist only in the CPU copy of their page until uploaded
            g_glyphs.flush();
            if (vertices.getVertexCount() > 0) {
                c2d::Transform combined = transform * getTransform();
                c2d_renderer->draw(&vertices, combined, font(weight)->getTexture(charSize));
            }
        }
        C2DObject::onDraw(transform, draw);
    }
}
