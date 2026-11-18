// Renders the V7 Player package images from the app's own brand geometry (src/ui/brand_art.h) as binary PPM files;
// assets/make_brand.sh turns them into the PNGs the PKG carries:
//   pic1.ppm   1920 x 1080: the system's start image = the in-app loading screen's first frame in V7 Blue
//   icon0.ppm  512 x 512:   the home-screen icon (V7 Blue)
//   preview-<accent>.ppm  (with --previews) the loading screen's first frame in every accent, for review only
// Usage: brand_render <output dir> [--previews]

#include <cstdio>
#include <cstring>
#include <string>

#include "raster.h"

namespace {
    bool writePpm(const std::string &path, const brandraster::Image &img) {
        FILE *f = std::fopen(path.c_str(), "wb");
        if (!f) {
            std::perror(path.c_str());
            return false;
        }
        std::fprintf(f, "P6\n%d %d\n255\n", img.w, img.h);
        bool ok = std::fwrite(img.rgb.data(), 1, img.rgb.size(), f) == img.rgb.size();
        return std::fclose(f) == 0 && ok;
    }

    brandart::Mesh scaled(brandart::Mesh m, float k) {
        for (auto &v: m) {
            v.x *= k;
            v.y *= k;
        }
        return m;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: brand_render <output dir> [--previews]\n");
        return 2;
    }
    const std::string out = argv[1];
    const bool previews = argc > 2 && std::strcmp(argv[2], "--previews") == 0;
    const brandart::Colors blue = brandart::colorsOf(palette::ACCENTS[palette::DEFAULT_ACCENT]);

    brandart::Mesh splash;
    brandart::splash::frame(splash, blue, 0.0);
    if (!writePpm(out + "/pic1.ppm", brandraster::render(splash, 1920, 1080, blue.background, 3, true))) {
        return 1;
    }
    brandart::Mesh icon;
    brandart::icon::frame(icon, blue);
    if (!writePpm(out + "/icon0.ppm", brandraster::render(icon, brandart::icon::SIZE, brandart::icon::SIZE,
                                                          blue.background, 4, true))) {
        return 1;
    }
    if (previews) {
        for (int i = 0; i < palette::ACCENT_COUNT; i++) {
            const brandart::Colors c = brandart::colorsOf(palette::ACCENTS[i]);
            brandart::Mesh m;
            brandart::splash::frame(m, c, 0.0);
            if (!writePpm(out + "/preview-" + palette::ACCENTS[i].key + ".ppm",
                          brandraster::render(scaled(m, 0.5f), 960, 540, c.background, 2, true))) {
                return 1;
            }
            brandart::Mesh later;
            brandart::splash::frame(later, c, 1.3);
            writePpm(out + "/preview-" + palette::ACCENTS[i].key + "-t1.3.ppm",
                     brandraster::render(scaled(later, 0.5f), 960, 540, c.background, 2, true));
        }
    }
    return 0;
}
