// Image format sniffing, decoding and downscaling (host-testable).
//
// Decoding uses stb_image v2.23, the decoder libcross2d already compiles in (gl_texture.cpp) - no new
// library. Supported: PNG (incl. palette/tRNS), JPEG (baseline/progressive), GIF (first frame), BMP.
// Dimensions are checked with stbi_info before the full decode, so a huge image is rejected without
// allocating for it. stb_image keeps its failure reason in a global, so decodes are serialized.

#ifndef PS4IPTV_IMAGES_IMAGE_DECODE_H
#define PS4IPTV_IMAGES_IMAGE_DECODE_H

#include <cstdint>
#include <string>
#include <vector>

namespace images {

    enum class Format {
        Unknown,
        Png,
        Jpeg,
        Gif,
        Bmp,
        Webp      // recognised, not decodable with this stack
    };

    Format sniff(const std::string &bytes);

    bool decodable(Format f);

    const char *formatName(Format f);

    // HTTP Content-Type values worth decoding (the bytes are sniffed regardless): image/*, a generic
    // binary type, or none at all. Rejects e.g. text/html error pages.
    bool acceptableContentType(const std::string &contentType);

    struct Image {
        int w = 0;
        int h = 0;
        std::vector<uint8_t> rgba;   // straight (non-premultiplied) alpha, rows of w*4 bytes

        bool valid() const { return w > 0 && h > 0 && rgba.size() == (size_t) w * (size_t) h * 4; }
    };

    const int MAX_DIMENSION = 4096;

    bool decode(const std::string &bytes, Image &out, std::string *error = nullptr);

    struct Size {
        int w = 0;
        int h = 0;
    };

    // largest size with the source aspect ratio that fits in the box (at least 1x1);
    // without allowUpscale the source size is kept when it already fits
    Size fitInside(int srcW, int srcH, int boxW, int boxH, bool allowUpscale);

    // area-average resample to w x h (for shrinking). Colour is averaged with alpha weighting, so
    // transparent pixels never darken the edges of a logo.
    Image resizeArea(const Image &src, int w, int h);

    // fit into the box without upscaling (the GPU scales small images up)
    Image prepare(const Image &src, int boxW, int boxH);
}

#endif // PS4IPTV_IMAGES_IMAGE_DECODE_H
