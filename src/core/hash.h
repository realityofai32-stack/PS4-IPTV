// MD5 and SHA-256 digests as upper-case hex (Stalker / MAG portal device identifiers derived from the MAC).
// Not used for anything security related.

#ifndef PS4IPTV_CORE_HASH_H
#define PS4IPTV_CORE_HASH_H

#include <string>

namespace hash {

    std::string md5Hex(const std::string &data);

    std::string sha256Hex(const std::string &data);
}

#endif // PS4IPTV_CORE_HASH_H
