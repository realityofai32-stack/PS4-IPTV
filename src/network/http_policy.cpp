#include <cctype>
#include <cstdlib>

#include "http_policy.h"

namespace httppolicy {

    namespace {
        std::string lower(std::string s) {
            for (char &c: s) {
                c = (char) std::tolower((unsigned char) c);
            }
            return s;
        }
    }

    double backoffSeconds(int attempt) {
        return attempt <= 1 ? 0.75 : 2.0;
    }

    bool schemeAllowed(const std::string &url) {
        size_t sep = url.find("://");
        if (sep == std::string::npos) {
            return false;
        }
        std::string scheme = lower(url.substr(0, sep));
        return scheme == "http" || scheme == "https";
    }

    bool parseOrigin(const std::string &url, Origin &out) {
        size_t sep = url.find("://");
        if (sep == std::string::npos || sep == 0) {
            return false;
        }
        out.scheme = lower(url.substr(0, sep));
        size_t start = sep + 3;
        size_t end = url.find_first_of("/?#", start);
        std::string authority = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t at = authority.rfind('@');
        if (at != std::string::npos) {
            authority = authority.substr(at + 1);
        }
        std::string host = authority;
        std::string port;
        if (!authority.empty() && authority[0] == '[') {   // IPv6 literal
            size_t close = authority.find(']');
            if (close == std::string::npos) {
                return false;
            }
            host = authority.substr(0, close + 1);
            if (close + 1 < authority.size() && authority[close + 1] == ':') {
                port = authority.substr(close + 2);
            }
        } else {
            size_t colon = authority.rfind(':');
            if (colon != std::string::npos) {
                host = authority.substr(0, colon);
                port = authority.substr(colon + 1);
            }
        }
        out.host = lower(host);
        if (out.host.empty()) {
            return false;
        }
        if (port.empty()) {
            out.port = out.scheme == "https" ? 443 : out.scheme == "http" ? 80 : 0;
        } else {
            char *e = nullptr;
            long p = strtol(port.c_str(), &e, 10);
            if (e == nullptr || *e != 0 || p <= 0 || p > 65535) {
                return false;
            }
            out.port = (int) p;
        }
        return true;
    }

    Redirect checkRedirect(const std::string &from, const std::string &to, bool credentials, bool sameHostOnly) {
        if (!schemeAllowed(to)) {
            return to.find("://") == std::string::npos ? Redirect::Invalid : Redirect::BadScheme;
        }
        Origin a, b;
        if (!parseOrigin(from, a) || !parseOrigin(to, b)) {
            return Redirect::Invalid;
        }
        if (credentials && a.scheme == "https" && b.scheme == "http") {
            return Redirect::Downgrade;
        }
        // "example.com" and "www.example.com" are the same site (portals often redirect between the two)
        auto site = [](const std::string &h) { return h.compare(0, 4, "www.") == 0 ? h.substr(4) : h; };
        if (sameHostOnly && site(a.host) != site(b.host)) {
            return Redirect::OtherHost;
        }
        return Redirect::Follow;
    }

    bool shouldRetry(Failure failure, long status, double elapsedSeconds, long totalTimeoutMs, int retriesDone,
                     int maxRetries, bool canceled) {
        if (canceled || retriesDone >= maxRetries) {
            return false;
        }
        // a slow server is not asked again: only failures that happened in the first half of the time budget
        if (totalTimeoutMs > 0 && elapsedSeconds * 1000.0 > (double) totalTimeoutMs / 2) {
            return false;
        }
        switch (failure) {
            case Failure::None:
                return status == 502 || status == 503 || status == 504;
            case Failure::Dns:
            case Failure::Connect:
            case Failure::Interrupted:
                return true;
            default:
                // timeouts (a dead or stalled server: asking again only makes the user wait longer), canceled,
                // TLS, certificate, too large, unsafe redirect, other
                return false;
        }
    }
}
