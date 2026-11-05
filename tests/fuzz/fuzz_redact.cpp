// libFuzzer target: log redaction (registration of URLs / MAC addresses and masking), incl. nested percent-encoded
// URLs. Invariant: a credential registered through a URL never survives in the masked text.
#include <cctype>
#include <cstdint>
#include <string>

#include "../../src/platform/redact.h"

#define REQUIRE(c) do { if (!(c)) { __builtin_trap(); } } while (0)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::string in((const char *) data, size);
    redact::clearSecrets();
    redact::addUrl(in);
    redact::addMac(in);
    std::string out = redact::apply(in);
    REQUIRE(out.size() <= in.size() * 12 + 64);
    redact::parseUrl(in);
    // a password made from the input's letters and digits, in a query, a nested encoded URL and a path
    std::string secret;
    for (char c: in) {
        if (std::isalnum((unsigned char) c) && secret.size() < 24) {
            secret += c;
        }
    }
    if (secret.size() >= 3) {
        redact::clearSecrets();
        std::string url = "http://h.example.com/x?username=a&password=" + secret + "&t=1";
        std::string nested = "http://r.example.com/go?to=http%3A%2F%2Fh%2Fg.php%3Fpassword%3D" + secret + "%26a%3D1";
        redact::addUrl(url);
        redact::addUrl(nested);
        for (const std::string &line: {url, nested, "Opening '" + url + "' for reading", in + url}) {
            // the mask text itself ("[REDACTED]") may contain the letters of a short secret: look outside it
            std::string masked = redact::apply(line);
            std::string rest;
            size_t at = 0, m;
            while ((m = masked.find("[REDACTED]", at)) != std::string::npos) {
                rest += masked.substr(at, m - at) + "|";
                at = m + 10;
            }
            rest += masked.substr(at);
            REQUIRE(rest.find(secret) == std::string::npos);
        }
    }
    return 0;
}
