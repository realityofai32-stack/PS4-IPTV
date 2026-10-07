#include <curl/curl.h>

#include "http.h"
#include "../platform/log.h"

namespace http {

    namespace {
        std::string g_caBundle;
        std::string g_userAgent = "PS4IPTV";

        struct Transfer {
            std::string *body;
            size_t maxBytes;
            bool tooLarge = false;
            CancelFlag cancel;
        };

        size_t onWrite(char *data, size_t size, size_t count, void *user) {
            auto *t = (Transfer *) user;
            size_t n = size * count;
            if (t->body->size() + n > t->maxBytes) {
                t->tooLarge = true;
                return 0;  // aborts the transfer (CURLE_WRITE_ERROR)
            }
            t->body->append(data, n);
            return n;
        }

        int onProgress(void *user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
            auto *t = (Transfer *) user;
            return t->cancel && t->cancel->load() ? 1 : 0;  // non-zero aborts (CURLE_ABORTED_BY_CALLBACK)
        }

        // one easy handle per worker thread: keeps connections alive between requests
        CURL *threadHandle() {
            static thread_local CURL *handle = nullptr;
            if (handle == nullptr) {
                handle = curl_easy_init();
            } else {
                curl_easy_reset(handle);
            }
            return handle;
        }
    }

    bool globalInit(const std::string &caBundle, const std::string &userAgent) {
        g_caBundle = caBundle;
        g_userAgent = userAgent;
        CURLcode rc = curl_global_init(CURL_GLOBAL_ALL);
        curl_version_info_data *v = curl_version_info(CURLVERSION_NOW);
        LOG_I("http", "curl_global_init: %d (%s); libcurl %s, %s, features 0x%x", (int) rc, curl_easy_strerror(rc),
              v->version, v->ssl_version ? v->ssl_version : "no TLS", v->features);
        return rc == CURLE_OK;
    }

    void globalShutdown() {
        curl_global_cleanup();
    }

    Response get(const Request &req) {
        Response r;
        CURL *c = threadHandle();
        if (c == nullptr) {
            r.error = Error::Other;
            r.detail = "curl_easy_init failed";
            return r;
        }
        Transfer t;
        t.body = &r.body;
        t.maxBytes = req.maxBytes;
        t.cancel = req.cancel;
        char errbuf[CURL_ERROR_SIZE] = {0};

        curl_easy_setopt(c, CURLOPT_URL, req.url.c_str());
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, req.connectTimeoutMs);
        curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, req.totalTimeoutMs);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, req.stallTimeoutS);
        curl_easy_setopt(c, CURLOPT_USERAGENT, g_userAgent.c_str());
        curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");  // gzip/deflate: Xtream JSON lists shrink a lot
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onWrite);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &t);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &t);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
        curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 64L * 1024);
        if (!g_caBundle.empty()) {
            curl_easy_setopt(c, CURLOPT_CAINFO, g_caBundle.c_str());
        }

        CURLcode rc = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
        char *ct = nullptr;
        if (curl_easy_getinfo(c, CURLINFO_CONTENT_TYPE, &ct) == CURLE_OK && ct) {
            r.contentType = ct;
        }
        char *effective = nullptr;
        if (curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &effective) == CURLE_OK && effective) {
            r.redirectedToHttps = req.url.compare(0, 8, "https://") != 0
                                  && std::string(effective).compare(0, 8, "https://") == 0;
        }
        curl_easy_getinfo(c, CURLINFO_TOTAL_TIME, &r.seconds);

        if (rc == CURLE_OK) {
            return r;
        }
        r.detail = std::string(curl_easy_strerror(rc)) + (errbuf[0] ? std::string(": ") + errbuf : "");
        switch (rc) {
            case CURLE_ABORTED_BY_CALLBACK:
                r.error = Error::Canceled;
                break;
            case CURLE_WRITE_ERROR:
                r.error = t.tooLarge ? Error::TooLarge : Error::Other;
                break;
            case CURLE_OPERATION_TIMEDOUT:
                r.error = Error::Timeout;
                break;
            case CURLE_COULDNT_RESOLVE_HOST:
                r.error = Error::Dns;
                break;
            case CURLE_COULDNT_CONNECT:
                r.error = Error::Connect;
                break;
            case CURLE_UNSUPPORTED_PROTOCOL:
                r.error = Error::HttpsUnsupported;
                break;
            case CURLE_SSL_CONNECT_ERROR:
            case CURLE_PEER_FAILED_VERIFICATION:
            case CURLE_SSL_CACERT_BADFILE:
            case CURLE_SSL_CERTPROBLEM:
                r.error = Error::Tls;
                break;
            default:
                r.error = Error::Other;
                break;
        }
        return r;
    }

    std::string describe(const Response &r) {
        switch (r.error) {
            case Error::None:
                return r.status >= 200 && r.status < 300 ? "OK" : "HTTP error " + std::to_string(r.status);
            case Error::Canceled:
                return "Canceled";
            case Error::Timeout:
                return "Server timeout";
            case Error::Dns:
                return "Server name not found (DNS)";
            case Error::Connect:
                return "Unable to connect to the server";
            case Error::Tls:
                return "Secure connection (HTTPS) failed";
            case Error::TooLarge:
                return "Server response too large";
            case Error::HttpsUnsupported:
                return "Unsupported protocol";
            default:
                return "Network error";
        }
    }
}
