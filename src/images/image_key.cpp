#include <cstring>

#include "image_key.h"

namespace images {

    namespace {
        bool startsWithNoCase(const std::string &s, const char *prefix) {
            size_t n = strlen(prefix);
            if (s.size() < n) {
                return false;
            }
            for (size_t i = 0; i < n; i++) {
                char c = s[i];
                if (c >= 'A' && c <= 'Z') {
                    c = (char) (c - 'A' + 'a');
                }
                if (c != prefix[i]) {
                    return false;
                }
            }
            return true;
        }

        bool needsEscape(unsigned char c) {
            return c <= 0x20 || c >= 0x7F || strchr("\"<>\\^`{|}", c) != nullptr;
        }
    }

    std::string normalizeUrl(const std::string &raw) {
        size_t b = 0;
        size_t e = raw.size();
        while (b < e && (raw[b] == ' ' || raw[b] == '\t' || raw[b] == '\r' || raw[b] == '\n')) {
            b++;
        }
        while (e > b && (raw[e - 1] == ' ' || raw[e - 1] == '\t' || raw[e - 1] == '\r' || raw[e - 1] == '\n')) {
            e--;
        }
        std::string s = raw.substr(b, e - b);
        if (!startsWithNoCase(s, "http://") && !startsWithNoCase(s, "https://")) {
            return "";
        }
        static const char *hex = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size() + 8);
        for (unsigned char c: s) {
            if (needsEscape(c)) {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 15];
            } else {
                out += (char) c;
            }
        }
        size_t scheme = out.find("://");
        if (out.size() > MAX_URL_LENGTH || out.size() <= scheme + 3) {
            return "";
        }
        return out;
    }

    uint64_t fnv1a64(const std::string &data, uint64_t basis) {
        uint64_t h = basis;
        for (unsigned char c: data) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        return h;
    }

    std::string cacheKey(const std::string &normalizedUrl) {
        static const char *hex = "0123456789abcdef";
        uint64_t h = fnv1a64(normalizedUrl);
        std::string out(16, '0');
        for (int i = 15; i >= 0; i--) {
            out[(size_t) i] = hex[h & 15];
            h >>= 4;
        }
        return out;
    }

    uint64_t verifyHash(const std::string &normalizedUrl) {
        // same function, unrelated basis: independent of the file name hash
        return fnv1a64(normalizedUrl, 0x9E3779B97F4A7C15ULL);
    }
}
