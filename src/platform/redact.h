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

    // Registers a MAC address (Stalker / MAG identity) in every spelling it can appear in: 00:1A:79:..,
    // lower case, with dashes, without separators and percent-encoded (00%3A1A..).
    void addMac(const std::string &mac);

    // Removes every registered secret.
    void clearSecrets();

    // Masks registered secrets and credential-shaped URL parts.
    std::string apply(const std::string &text);

    UrlInfo parseUrl(const std::string &url);

    // Every URL in `text` ("scheme://...") cut to its origin: "scheme://host[:port]/[...]" (user information, path,
    // query and fragment dropped), then apply(). For lines of third-party logs (mpv "Playing: <url>", FFmpeg
    // "Opening '<url>' for reading") whose URLs can carry provider tokens in the path that no rule recognises.
    std::string urlsToOrigins(const std::string &text);

    // A local file's names in a line of a third-party log (mpv "Playing: /usb0/Holiday/Kids 2024.mkv", an external
    // subtitle "Kids 2024.en.srt" found next to it): the full path, its folder (when below the storage root: "/usb0"
    // itself stays) and the file name without its extension (4 characters or more) become "[local file]",
    // "[local folder]" and "[local name]". `path`: an absolute local path; a URL or "" leaves the text unchanged.
    std::string maskLocalPath(const std::string &text, const std::string &path);
}

#endif // PS4IPTV_PLATFORM_REDACT_H
