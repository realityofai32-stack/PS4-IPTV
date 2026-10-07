#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

#include "image_decode.h"
// declarations only: the implementation is compiled into libcross2d (and into the host test binary)
#include "cross2d/skeleton/stb_image.h"

namespace images {

    namespace {
        std::mutex g_stbMutex;

        bool has(const std::string &b, size_t at, const char *sig, size_t n) {
            return b.size() >= at + n && memcmp(b.data() + at, sig, n) == 0;
        }
    }

    Format sniff(const std::string &b) {
        if (has(b, 0, "\x89PNG\r\n\x1a\n", 8)) {
            return Format::Png;
        }
        if (has(b, 0, "\xFF\xD8\xFF", 3)) {
            return Format::Jpeg;
        }
        if (has(b, 0, "GIF87a", 6) || has(b, 0, "GIF89a", 6)) {
            return Format::Gif;
        }
        if (has(b, 0, "RIFF", 4) && has(b, 8, "WEBP", 4)) {
            return Format::Webp;
        }
        if (has(b, 0, "BM", 2) && b.size() > 54) {
            return Format::Bmp;
        }
        return Format::Unknown;
    }

    bool decodable(Format f) {
        return f == Format::Png || f == Format::Jpeg || f == Format::Gif || f == Format::Bmp;
    }

    const char *formatName(Format f) {
        switch (f) {
            case Format::Png:
                return "PNG";
            case Format::Jpeg:
                return "JPEG";
            case Format::Gif:
                return "GIF";
            case Format::Bmp:
                return "BMP";
            case Format::Webp:
                return "WebP";
            default:
                return "unknown";
        }
    }

    bool acceptableContentType(const std::string &contentType) {
        std::string ct;
        for (char c: contentType) {
            if (c == ';') {
                break;
            }
            if (c != ' ' && c != '\t') {
                ct += (char) ((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
            }
        }
        if (ct.empty() || ct == "application/octet-stream" || ct == "binary/octet-stream") {
            return true;
        }
        return ct.compare(0, 6, "image/") == 0 && ct != "image/svg+xml";
    }

    bool decode(const std::string &bytes, Image &out, std::string *error) {
        out = Image();
        Format f = sniff(bytes);
        if (!decodable(f)) {
            if (error) {
                *error = std::string("unsupported image format: ") + formatName(f);
            }
            return false;
        }
        if (bytes.size() > (size_t) 0x7FFFFFFF) {
            if (error) {
                *error = "image too large";
            }
            return false;
        }
        std::lock_guard<std::mutex> lock(g_stbMutex);
        const auto *data = (const stbi_uc *) bytes.data();
        int len = (int) bytes.size();
        int w = 0;
        int h = 0;
        int comp = 0;
        if (!stbi_info_from_memory(data, len, &w, &h, &comp)) {
            if (error) {
                *error = std::string("image header: ") + (stbi_failure_reason() ? stbi_failure_reason() : "?");
            }
            return false;
        }
        if (w <= 0 || h <= 0 || w > MAX_DIMENSION || h > MAX_DIMENSION) {
            if (error) {
                *error = "image dimensions out of range: " + std::to_string(w) + "x" + std::to_string(h);
            }
            return false;
        }
        stbi_uc *px = stbi_load_from_memory(data, len, &w, &h, &comp, 4);
        if (px == nullptr) {
            if (error) {
                *error = std::string("image decode: ") + (stbi_failure_reason() ? stbi_failure_reason() : "?");
            }
            return false;
        }
        out.w = w;
        out.h = h;
        out.rgba.assign(px, px + (size_t) w * (size_t) h * 4);
        stbi_image_free(px);
        return true;
    }

    Size fitInside(int srcW, int srcH, int boxW, int boxH, bool allowUpscale) {
        Size s;
        if (srcW <= 0 || srcH <= 0 || boxW <= 0 || boxH <= 0) {
            return s;
        }
        if (!allowUpscale && srcW <= boxW && srcH <= boxH) {
            s.w = srcW;
            s.h = srcH;
            return s;
        }
        double scale = std::min((double) boxW / srcW, (double) boxH / srcH);
        s.w = std::max(1, std::min(boxW, (int) std::lround(srcW * scale)));
        s.h = std::max(1, std::min(boxH, (int) std::lround(srcH * scale)));
        return s;
    }

    Image resizeArea(const Image &src, int w, int h) {
        Image out;
        if (!src.valid() || w <= 0 || h <= 0) {
            return out;
        }
        if (w == src.w && h == src.h) {
            return src;
        }
        out.w = w;
        out.h = h;
        out.rgba.assign((size_t) w * (size_t) h * 4, 0);
        const double sx = (double) src.w / w;
        const double sy = (double) src.h / h;
        for (int dy = 0; dy < h; dy++) {
            double y0 = dy * sy;
            double y1 = std::min((double) src.h, (dy + 1) * sy);
            for (int dx = 0; dx < w; dx++) {
                double x0 = dx * sx;
                double x1 = std::min((double) src.w, (dx + 1) * sx);
                double sum[4] = {0, 0, 0, 0};   // premultiplied r g b, then alpha
                double weight = 0;
                for (int y = (int) y0; y < (int) std::ceil(y1); y++) {
                    double wy = std::min((double) y + 1, y1) - std::max((double) y, y0);
                    if (wy <= 0) {
                        continue;
                    }
                    const uint8_t *row = &src.rgba[(size_t) y * (size_t) src.w * 4];
                    for (int x = (int) x0; x < (int) std::ceil(x1); x++) {
                        double wx = std::min((double) x + 1, x1) - std::max((double) x, x0);
                        if (wx <= 0) {
                            continue;
                        }
                        double wgt = wx * wy;
                        const uint8_t *p = row + (size_t) x * 4;
                        double a = p[3] * wgt;
                        sum[0] += p[0] * a;
                        sum[1] += p[1] * a;
                        sum[2] += p[2] * a;
                        sum[3] += a;
                        weight += wgt;
                    }
                }
                uint8_t *o = &out.rgba[((size_t) dy * (size_t) w + (size_t) dx) * 4];
                if (weight <= 0 || sum[3] <= 0) {
                    continue;   // fully transparent
                }
                for (int c = 0; c < 3; c++) {
                    o[c] = (uint8_t) std::min(255.0, std::round(sum[c] / sum[3]));
                }
                o[3] = (uint8_t) std::min(255.0, std::round(sum[3] / weight));
            }
        }
        return out;
    }

    Image prepare(const Image &src, int boxW, int boxH) {
        Size s = fitInside(src.w, src.h, boxW, boxH, false);
        return resizeArea(src, s.w, s.h);
    }
}
