#include <algorithm>
#include <cctype>

#include "url.h"
#include "../i18n/i18n.h"

namespace url {

    std::string encode(const std::string &s) {
        static const char *HEX = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size() * 3);
        for (unsigned char c: s) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.'
                || c == '_' || c == '~') {
                out += (char) c;
            } else {
                out += '%';
                out += HEX[c >> 4];
                out += HEX[c & 15];
            }
        }
        return out;
    }

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char) std::tolower(c); });
        return s;
    }

    std::string trim(const std::string &s) {
        size_t b = 0;
        size_t e = s.size();
        while (b < e && std::isspace((unsigned char) s[b])) {
            b++;
        }
        while (e > b && std::isspace((unsigned char) s[e - 1])) {
            e--;
        }
        return s.substr(b, e - b);
    }

    Server normalizeServer(const std::string &input) {
        Server r;
        std::string s = trim(input);
        if (s.empty()) {
            r.error = i18n::tr("profile.error.server_empty");
            return r;
        }

        size_t schemeEnd = s.find("://");
        if (schemeEnd == std::string::npos) {
            r.scheme = "http";
        } else {
            r.scheme = lower(s.substr(0, schemeEnd));
            s = s.substr(schemeEnd + 3);
        }
        if (r.scheme != "http" && r.scheme != "https") {
            r.error = i18n::tr("profile.error.scheme");
            return r;
        }

        // drop query / fragment and a pasted Xtream endpoint
        size_t q = s.find_first_of("?#");
        if (q != std::string::npos) {
            s = s.substr(0, q);
        }
        size_t slash = s.find('/');
        std::string authority = slash == std::string::npos ? s : s.substr(0, slash);
        std::string path = slash == std::string::npos ? "" : s.substr(slash);
        for (const char *endpoint: {"/player_api.php", "/get.php", "/xmltv.php", "/panel_api.php"}) {
            size_t pos = lower(path).rfind(endpoint);
            if (pos != std::string::npos) {
                path = path.substr(0, pos);
            }
        }
        while (!path.empty() && path.back() == '/') {
            path.pop_back();
        }

        size_t at = authority.rfind('@');
        if (at != std::string::npos) {
            authority = authority.substr(at + 1);  // never keep user:pass@ in the server address
        }
        if (authority.empty()) {
            r.error = i18n::tr("profile.error.no_host");
            return r;
        }
        size_t colon = authority.rfind(':');
        bool ipv6 = authority.front() == '[';
        if (colon != std::string::npos && (!ipv6 || authority.find(']') < colon)) {
            r.host = authority.substr(0, colon);
            r.port = authority.substr(colon + 1);
            if (r.port.empty() || r.port.size() > 5
                || !std::all_of(r.port.begin(), r.port.end(), [](unsigned char c) { return std::isdigit(c); })) {
                r.error = i18n::tr("profile.error.port_number");
                return r;
            }
            int portNum = std::stoi(r.port);
            if (portNum < 1 || portNum > 65535) {
                r.error = i18n::tr("profile.error.port_range");
                return r;
            }
            if ((r.scheme == "http" && r.port == "80") || (r.scheme == "https" && r.port == "443")) {
                r.port.clear();
            }
        } else {
            r.host = authority;
        }
        r.host = lower(r.host);
        for (unsigned char c: r.host) {
            if (!(std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == '[' || c == ']' || c == ':')) {
                r.error = i18n::tr("profile.error.invalid_chars");
                return r;
            }
        }

        r.displayHost = r.host + (r.port.empty() ? "" : ":" + r.port);
        r.base = r.scheme + "://" + r.displayHost + path;
        r.ok = true;
        return r;
    }
}
