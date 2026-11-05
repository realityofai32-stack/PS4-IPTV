#include <algorithm>
#include <atomic>
#include <cctype>

#include "protocols.h"

namespace {
    std::atomic<bool> g_https{false};

    bool contains(const std::string &s, const char *p) {
        return s.find(p) != std::string::npos;
    }
}

namespace protocols {

    std::string scheme(const std::string &url) {
        if (!url.empty() && url[0] == '/') {
            return "file";
        }
        size_t sep = url.find("://");
        if (sep == std::string::npos || sep == 0) {
            return "";
        }
        std::string s;
        for (size_t i = 0; i < sep; i++) {
            unsigned char c = (unsigned char) url[i];
            if (!std::isalnum(c) && c != '+' && c != '-' && c != '.') {
                return "";
            }
            s += (char) std::tolower(c);
        }
        return s;
    }

    void setAvailable(const std::vector<std::string> &names) {
        bool https = std::find(names.begin(), names.end(), "https") != names.end();
        bool tls = std::find(names.begin(), names.end(), "tls") != names.end();
        g_https = https && tls;
    }

    bool httpsPlayable() {
        return g_https;
    }

    Verdict check(const std::string &url, bool https) {
        std::string s = scheme(url);
        if (s == "https") {
            return https ? Verdict::Ok : Verdict::HttpsUnavailable;
        }
        // unchanged since v0.1: what the FFmpeg build opens besides HTTPS (plain HTTP behaves exactly as before)
        static const char *const SUPPORTED[] = {"http", "rtmp", "rtp", "udp", "tcp", "ftp", "file"};
        for (const char *p: SUPPORTED) {
            if (s == p) {
                return Verdict::Ok;
            }
        }
        return Verdict::Unsupported;
    }

    std::string shownScheme(const std::string &url) {
        std::string s = scheme(url);
        if (s.empty()) {
            return "?";
        }
        for (char &c: s) {
            c = (char) std::toupper((unsigned char) c);
        }
        return s;
    }

    TlsFailure classifyTls(const std::string &line) {
        // texts of FFmpeg 5.0 libavformat/tls_mbedtls.c; -0x2700 = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED, the code
        // mbedtls_ssl_handshake returns when tls_verify is on and the chain does not check out
        if (contains(line, "x509_crt_parse_file for CA cert")) {
            return TlsFailure::CaBundle;
        }
        if (contains(line, "-0x2700") || contains(line, "verify_result reported problems")
            || contains(line, "not correctly signed by the trusted CA") || contains(line, "No CA chain is set")) {
            return TlsFailure::Certificate;
        }
        if (contains(line, "mbedtls_ssl_handshake returned") || contains(line, "fatal alert message")
            || contains(line, "TLS handshake was aborted") || contains(line, "None of the common ciphersuites")
            || contains(line, "mbedtls_ctr_drbg_seed returned") || contains(line, "mbedtls_ssl_set_hostname returned")) {
            return TlsFailure::Handshake;
        }
        return TlsFailure::None;
    }

    std::string origin(const std::string &url) {
        std::string s = scheme(url);
        if (s.empty() || s == "file") {
            return s;
        }
        size_t start = url.find("://") + 3;
        size_t end = url.find_first_of("/?#", start);
        std::string authority = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t at = authority.rfind('@');
        if (at != std::string::npos) {
            authority = authority.substr(at + 1);   // never user:password
        }
        return s + "://" + authority;
    }
}
