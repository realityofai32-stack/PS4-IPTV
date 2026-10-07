// UTF-8 helpers. All text in the app is UTF-8 std::string; rendering decodes to code points.

#ifndef PS4IPTV_CORE_UTF8_H
#define PS4IPTV_CORE_UTF8_H

#include <string>

namespace utf8 {

    // Decodes UTF-8. Invalid or truncated sequences decode to U+FFFD, never throw.
    std::u32string decode(const std::string &text);

    std::string encode(char32_t codePoint);

    // Removes the last code point (for backspace).
    void popBack(std::string &text);

    size_t length(const std::string &text);

    // Lower-cases ASCII and the common Latin-1/Latin Extended-A letters (incl. Turkish) for searching.
    std::u32string foldForSearch(const std::string &text);
}

#endif // PS4IPTV_CORE_UTF8_H
