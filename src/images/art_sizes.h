// The large artwork kinds: their texture boxes and what they cost on the GPU (host-testable, no libcross2d).
//
// Every other image is uploaded into a power-of-two page with the image in its top-left corner. For the two
// large kinds that wastes up to half the page (1280 x 720 would take a 2048 x 1024 page: 8 MiB for 3.5 MiB of
// image), so they get an exact-size texture instead: RGBA8, GL_LINEAR, clamp-to-edge, no mipmaps - legal for
// non-power-of-two sizes in core GLES2, and the same texture type the player's 1920 x 1080 video frame
// (GLTextureBuffer) already uses on the PS4 every frame.
//
//   Backdrop     1280 x 720  exact, 3.52 MiB   a real landscape backdrop (provider backdrop_path): the Home hero
//                                              (1320 x 800 box, cover: 1.11x) and the detail pages; at most
//                                              MAX_BACKDROPS in memory (<= 7.03 MiB, v3: 2 x 4 MiB pages)
//   HeroPoster    512 x 768  exact, 1.50 MiB   the Home hero's poster fallback (v3: the 400 x 600 PosterLarge
//                                              in a 512 x 1024 page, 2 MiB, drawn 1.47x)
// Sources are never enlarged on the CPU: a smaller source keeps its size (and the hero draws it smaller).

#ifndef PS4IPTV_IMAGES_ART_SIZES_H
#define PS4IPTV_IMAGES_ART_SIZES_H

#include <cstdint>

#include "image_pipeline.h"

namespace images {
    namespace art {
        constexpr Box BACKDROP = {1280, 720};
        constexpr Box HERO_POSTER = {512, 768};

        // a 2048 px source decodes into <= 16 MiB of RGBA for a moment (worker); larger art falls back to the poster
        constexpr int BACKDROP_MAX_SOURCE = 2048;

        // backdrops in memory at once (the Home hero + one detail page); the oldest leaves first
        constexpr int MAX_BACKDROPS = 2;

        struct Page {
            int w = 0;
            int h = 0;
            int64_t bytes = 0;
        };

        inline int pow2(int v) {
            int p = 8;
            while (p < v) {
                p <<= 1;
            }
            return p;
        }

        // the GPU texture an image of w x h is uploaded into (RGBA8)
        inline Page page(int w, int h, bool exact) {
            Page p;
            if (w <= 0 || h <= 0) {
                return p;
            }
            p.w = exact ? w : pow2(w);
            p.h = exact ? h : pow2(h);
            p.bytes = (int64_t) p.w * p.h * 4;
            return p;
        }
    }
}

#endif // PS4IPTV_IMAGES_ART_SIZES_H
