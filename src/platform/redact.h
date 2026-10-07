// Credential redaction for logs and on-screen text.
//
// Every string that can contain a stream URL (our own messages, mpv/ffmpeg/SDL log lines, mpv error
// text) goes through redact::apply() before it is written anywhere.

#ifndef PS4IPTV_PLATFORM_REDACT_H
#define PS4IPTV_PLATFORM_REDACT_H

#include <string>

namespace redact {

    struct UrlInfo {
        bool valid = false;
        std::string scheme;     // lower case, e.g. "http"
        std::string host;
        std::string port;       // empty if not given
        std::string extension;  // of the last path segment, lower case, e.g. "ts", "m3u8"
        std::string sanitized;  // scheme://host[:port]/path with credentials replaced
    };

    // Registers the credentials contained in a URL (Xtream "/live/<user>/<pass>/" path segments,
    // "user:pass@" userinfo and credential-like query values) so they are masked everywhere.
    void addUrl(const std::string &url);

    // Registers a literal secret (e.g. a profile password) to be masked everywhere (>= 3 chars).
    void addSecret(const std::string &secret);

    // Removes every registered secret.
    void clearSecrets();

    // Masks registered secrets and credential-shaped URL parts.
    std::string apply(const std::string &text);

    UrlInfo parseUrl(const std::string &url);
}

#endif // PS4IPTV_PLATFORM_REDACT_H
