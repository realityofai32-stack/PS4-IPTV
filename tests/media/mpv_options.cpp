// Prints, as JSON lines, the mpv options the movie player applies (the same functions VodPlayerScreen::open calls
// for streams, downloads and Local Media alike): video geometry for every aspect / crop and some zoom / position
// values, and the subtitle style for every size / position / shadow. Used by tests/media/media_it.py.
#include <cstdio>
#include <string>

#include "../../src/player/display.h"
#include "../../src/player/tracks.h"

static void print(const std::vector<std::pair<std::string, std::string>> &opts) {
    std::string line = "[";
    for (size_t i = 0; i < opts.size(); i++) {
        line += std::string(i ? "," : "") + "[\"" + opts[i].first + "\",\"" + opts[i].second + "\"]";
    }
    std::printf("%s]\n", line.c_str());
}

int main() {
    for (int a = 0; a < display::ASPECT_COUNT; a++) {
        display::Geometry g;
        g.aspect = (display::Aspect) a;
        print(display::mpvOptions(g, 1920, 1080));
    }
    for (int c = 0; c < display::CROP_COUNT; c++) {
        display::Geometry g;
        g.crop = (display::Crop) c;
        g.zoom = c % 2 ? 125 : 100;
        g.posX = c % 3 - 1;
        g.posY = 1 - c % 3;
        print(display::mpvOptions(g, 1920, 1080));
    }
    for (int size = 0; size < 3; size++) {
        for (int pos = 0; pos < 2; pos++) {
            print(tracks::appearanceOptions(size, pos, (size + pos) % 2 == 0));
        }
    }
    return 0;
}
