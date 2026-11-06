#include "subtitle_font.h"

#include "../platform/fs.h"

namespace subfont {

    namespace {
        const char *const SOURCES[] = {"V7Subtitles.ttf", "Inter-SemiBold.ttf"};
        const size_t MAX_FONT_BYTES = 8u * 1024 * 1024;
    }

    Result install(const std::string &fontsDir, const std::string &mpvDir, const std::string &version) {
        Result r;
        std::string src;
        int64_t size = -1;
        for (const char *name: SOURCES) {
            std::string path = fs::join(fontsDir, name);
            size = fs::fileSize(path);
            if (size > 0) {
                src = path;
                r.source = name;
                break;
            }
        }
        if (src.empty()) {
            r.error = "no subtitle font in the package";
            return r;
        }
        std::string dst = fs::join(mpvDir, "subfont.ttf");
        std::string markerPath = fs::join(mpvDir, "subfont.id");
        std::string marker = r.source + " " + std::to_string((long long) size) + " " + version + "\n";
        std::string current;
        bool upToDate = fs::fileSize(dst) == size && fs::readFile(markerPath, current, 4096) && current == marker;
        if (upToDate) {
            r.ok = true;
            return r;
        }
        std::string font;
        if (!fs::readFile(src, font, MAX_FONT_BYTES, &r.error) || (int64_t) font.size() != size
            || !fs::writeFileReplace(dst, font, &r.error)) {
            if (r.error.empty()) {
                r.error = "short read";
            }
            return r;
        }
        // the marker last: an interrupted copy is repeated at the next start
        fs::writeFileReplace(markerPath, marker, nullptr);
        r.installed = true;
        r.ok = true;
        return r;
    }
}
