// Image URL normalization and cache keys (host-testable).
//
// Cache file names are a 64-bit FNV-1a hash of the normalized URL in hex: no part of the URL (which could
// carry credentials on some panels) ever reaches the file system. A second, independently seeded hash is
// stored inside the cache file and checked on read, so a hash collision reads as a miss, never as the
// wrong logo.

#ifndef PS4IPTV_IMAGES_IMAGE_KEY_H
#define PS4IPTV_IMAGES_IMAGE_KEY_H

#include <cstdint>
#include <string>

namespace images {

    const size_t MAX_URL_LENGTH = 2048;

    // Trims whitespace and percent-encodes bytes that are not valid in a URL (space, control and non-ASCII
    // bytes, "<>\^`{|}). Existing %XX escapes are kept. Returns "" for anything that is not http(s).
    std::string normalizeUrl(const std::string &raw);

    uint64_t fnv1a64(const std::string &data, uint64_t basis = 14695981039346656037ULL);

    // 16 lowercase hex digits
    std::string cacheKey(const std::string &normalizedUrl);

    // stored in the cache file header
    uint64_t verifyHash(const std::string &normalizedUrl);
}

#endif // PS4IPTV_IMAGES_IMAGE_KEY_H
