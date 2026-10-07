// URL helpers: percent-encoding and Xtream server address normalization.

#ifndef PS4IPTV_CORE_URL_H
#define PS4IPTV_CORE_URL_H

#include <string>

namespace url {

    // RFC 3986 percent-encoding: unreserved characters (A-Z a-z 0-9 - . _ ~) are kept.
    std::string encode(const std::string &s);

    struct Server {
        bool ok = false;
        std::string error;       // user-facing reason when !ok
        std::string base;        // scheme://host[:port][/prefix] without trailing slash
        std::string scheme;      // "http" / "https"
        std::string host;
        std::string port;        // empty = scheme default
        std::string displayHost; // host[:port] for UI
    };

    // Accepts "host", "host:port", "http(s)://host[:port][/path]" and pasted Xtream links such as
    // ".../player_api.php?..." or ".../get.php?...". Defaults to http.
    Server normalizeServer(const std::string &input);

    std::string lower(std::string s);

    std::string trim(const std::string &s);
}

#endif // PS4IPTV_CORE_URL_H
