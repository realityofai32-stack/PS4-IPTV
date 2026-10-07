// Glyph cache in front of libcross2d's c2d::Font atlas pages.
//
// Hardware Checkpoint 1 failure: c2d::Font::loadGlyph writes a glyph's pixels only into the CPU copy of
// its page texture (on the PS4 GLES2 backend GLTexture::lock() returns the malloc'd pixel buffer). The GPU
// texture receives them only in GLTexture::unlock() (glTexSubImage2D of the whole page) or when the page
// grows (GLTexture::resize re-uploads everything with glTexImage2D). c2d::Text calls unlock() after it has
// built its geometry; the first ui::Label did not. Glyphs loaded after a page's last resize were therefore
// drawn from stale or uninitialised GPU memory: missing letters and garbage fragments on the PS4, while
// nothing on the host draws at all.
//
// GlyphCache keeps the rule in one place: a page is cleared before its first glyph, every new glyph marks
// its page dirty, and flush() uploads every dirty page (ui::Label calls it right before each draw). A page
// generation counter changes whenever the page texture changes size, so labels rebuild their texture
// coordinates. The cache is templated on its backend so host tests can model the GL CPU/GPU split.
//
// Backend requirements:
//   using Glyph = ...;
//   void preparePage(int font, unsigned size);                // create the page and clear its pixels
//   Glyph load(int font, unsigned size, char32_t codePoint);  // rasterise into the CPU copy of the page
//   void upload(int font, unsigned size);                     // CPU copy -> GPU texture
//   std::pair<int, int> pageSize(int font, unsigned size);

#ifndef PS4IPTV_UI_GLYPH_CACHE_H
#define PS4IPTV_UI_GLYPH_CACHE_H

#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>

namespace ui {

    template<typename Backend>
    class GlyphCache {

    public:

        using Glyph = typename Backend::Glyph;

        struct Stats {
            int pages = 0;
            int glyphs = 0;
            int uploads = 0;
            int resizes = 0;
        };

        explicit GlyphCache(Backend &b) : backend(b) {}

        // the reference stays valid for the cache's lifetime (unordered_map nodes never move)
        const Glyph &get(int font, unsigned size, char32_t codePoint) {
            uint64_t key = ((uint64_t) (font & 0xFF) << 56) | ((uint64_t) (size & 0xFFFFFF) << 32)
                           | (uint64_t) codePoint;
            auto it = glyphs.find(key);
            if (it != glyphs.end()) {
                return it->second;
            }
            Page &page = pageOf(font, size);
            Glyph glyph = backend.load(font, size, codePoint);
            page.dirty = true;
            std::pair<int, int> now = backend.pageSize(font, size);
            if (now != page.size) {
                page.size = now;
                page.generation++;
                stats.resizes++;
            }
            stats.glyphs++;
            return glyphs.emplace(key, glyph).first->second;
        }

        // uploads every page that received glyphs since the last flush; returns the number of uploads
        int flush() {
            int n = 0;
            for (auto &it: pages) {
                if (it.second.dirty) {
                    backend.upload((int) (it.first >> 32), (unsigned) (it.first & 0xFFFFFFFF));
                    it.second.dirty = false;
                    n++;
                }
            }
            stats.uploads += n;
            return n;
        }

        bool pending() const {
            for (const auto &it: pages) {
                if (it.second.dirty) {
                    return true;
                }
            }
            return false;
        }

        // changes whenever the page texture changes size (texture coordinates of its glyphs move)
        unsigned generation(int font, unsigned size) const {
            auto it = pages.find(pageKey(font, size));
            return it == pages.end() ? 0 : it->second.generation;
        }

        Stats getStats() const {
            Stats s = stats;
            s.pages = (int) pages.size();
            return s;
        }

    private:

        struct Page {
            bool dirty = true;
            std::pair<int, int> size{0, 0};
            unsigned generation = 1;
        };

        static uint64_t pageKey(int font, unsigned size) {
            return ((uint64_t) (unsigned) font << 32) | size;
        }

        Page &pageOf(int font, unsigned size) {
            uint64_t key = pageKey(font, size);
            auto it = pages.find(key);
            if (it != pages.end()) {
                return it->second;
            }
            backend.preparePage(font, size);
            Page page;
            page.size = backend.pageSize(font, size);
            return pages.emplace(key, page).first->second;
        }

        Backend &backend;
        std::unordered_map<uint64_t, Glyph> glyphs;
        std::map<uint64_t, Page> pages;
        Stats stats;
    };
}

#endif // PS4IPTV_UI_GLYPH_CACHE_H
