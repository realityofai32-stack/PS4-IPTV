#include <curl/curl.h>

#include <cstdlib>
#include <cstring>

#include "curl_transport.h"

namespace dl {

    namespace {
        struct Context {
            TransferSink *sink;
            const TransferRequest *request;
            ResponseHead head;
            bool headDelivered = false;
            bool headAccepted = true;
            bool aborted = false;
        };

        std::string trim(const std::string &s) {
            size_t b = s.find_first_not_of(" \t\r\n");
            if (b == std::string::npos) {
                return "";
            }
            return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
        }

        bool headerIs(const std::string &line, const char *name, std::string &value) {
            size_t n = strlen(name);
            if (line.size() <= n || line[n] != ':') {
                return false;
            }
            for (size_t i = 0; i < n; i++) {
                char a = line[i];
                char b = name[i];
                if ((a >= 'A' && a <= 'Z' ? a - 'A' + 'a' : a) != (b >= 'A' && b <= 'Z' ? b - 'A' + 'a' : b)) {
                    return false;
                }
            }
            value = trim(line.substr(n + 1));
            return true;
        }

        size_t onHeader(char *data, size_t size, size_t count, void *user) {
            auto *c = (Context *) user;
            size_t n = size * count;
            std::string line(data, n);
            if (line.compare(0, 5, "HTTP/") == 0) {
                // a new response (redirect hop / interim response): start over
                c->head = ResponseHead();
                size_t sp = line.find(' ');
                if (sp != std::string::npos) {
                    c->head.status = strtol(line.c_str() + sp + 1, nullptr, 10);
                }
                return n;
            }
            std::string value;
            if (headerIs(line, "content-length", value)) {
                char *end = nullptr;
                long long v = strtoll(value.c_str(), &end, 10);
                c->head.contentLength = end != value.c_str() && v >= 0 ? (int64_t) v : -1;
            } else if (headerIs(line, "content-range", value)) {
                c->head.range = parseContentRange(value);   // "bytes */N" (416): only the total is set
            } else if (headerIs(line, "etag", value)) {
                c->head.etag = value;
            } else if (headerIs(line, "last-modified", value)) {
                c->head.lastModified = value;
            }
            return n;
        }

        bool deliverHead(Context *c) {
            if (!c->headDelivered) {
                c->headDelivered = true;
                c->headAccepted = c->sink->onHead(c->head);
            }
            return c->headAccepted;
        }

        size_t onBody(char *data, size_t size, size_t count, void *user) {
            auto *c = (Context *) user;
            size_t n = size * count;
            if (c->head.status >= 300 && c->head.status < 400) {
                return n;   // body of a redirect hop
            }
            if (!deliverHead(c)) {
                c->aborted = true;
                return 0;
            }
            if (c->head.status < 200 || c->head.status >= 300) {
                return n;   // error page: discarded
            }
            if (!c->sink->onData(data, n)) {
                c->aborted = true;
                return 0;
            }
            return n;
        }

        int onProgress(void *user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
            auto *c = (Context *) user;
            if (c->request->cancel && c->request->cancel->load()) {
                c->aborted = true;
                return 1;
            }
            return 0;
        }
    }

    CurlTransport::CurlTransport(std::string ua, std::string ca) : userAgent(std::move(ua)), caBundle(std::move(ca)) {}

    TransferResult CurlTransport::run(const TransferRequest &req, TransferSink &sink) {
        TransferResult r;
        CURL *c = curl_easy_init();
        if (c == nullptr) {
            r.detail = "curl_easy_init failed";
            return r;
        }
        Context ctx;
        ctx.sink = &sink;
        ctx.request = &req;
        char errbuf[CURL_ERROR_SIZE] = {0};
        struct curl_slist *headers = nullptr;
        std::string range;

        curl_easy_setopt(c, CURLOPT_URL, req.url.c_str());
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, req.connectTimeoutMs);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, req.stallTimeoutS);
        curl_easy_setopt(c, CURLOPT_USERAGENT, userAgent.c_str());
        curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, onHeader);
        curl_easy_setopt(c, CURLOPT_HEADERDATA, &ctx);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onBody);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &ctx);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
        curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 256L * 1024);
        if (!caBundle.empty()) {
            curl_easy_setopt(c, CURLOPT_CAINFO, caBundle.c_str());
        }
        if (req.offset > 0) {
            range = std::to_string((long long) req.offset) + "-";
            curl_easy_setopt(c, CURLOPT_RANGE, range.c_str());
            if (!req.ifRange.empty()) {
                headers = curl_slist_append(headers, ("If-Range: " + req.ifRange).c_str());
                curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
            }
        }

        CURLcode rc = curl_easy_perform(c);
        long status = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        r.status = status;
        if (ctx.head.status == 0) {
            ctx.head.status = status;
        }
        if (rc == CURLE_OK) {
            // an empty body (e.g. 416, or a 0-byte file) still has a head to judge
            bool accepted = deliverHead(&ctx);
            if (status >= 200 && status < 300) {
                r.outcome = accepted ? TransferResult::Outcome::Complete : TransferResult::Outcome::Aborted;
            } else {
                r.outcome = TransferResult::Outcome::HttpError;
            }
        } else if (ctx.aborted || rc == CURLE_ABORTED_BY_CALLBACK || rc == CURLE_WRITE_ERROR) {
            r.outcome = TransferResult::Outcome::Aborted;
        } else {
            switch (rc) {
                case CURLE_COULDNT_RESOLVE_HOST:
                case CURLE_COULDNT_RESOLVE_PROXY:
                case CURLE_COULDNT_CONNECT:
                case CURLE_OPERATION_TIMEDOUT:
                case CURLE_RECV_ERROR:
                case CURLE_SEND_ERROR:
                case CURLE_PARTIAL_FILE:
                case CURLE_GOT_NOTHING:
                case CURLE_SSL_CONNECT_ERROR:
                case CURLE_HTTP2:
                case CURLE_HTTP2_STREAM:
                    r.outcome = TransferResult::Outcome::Network;
                    break;
                default:
                    r.outcome = TransferResult::Outcome::Other;
                    break;
            }
            r.detail = std::string(curl_easy_strerror(rc)) + (errbuf[0] ? std::string(": ") + errbuf : "");
        }
        if (headers) {
            curl_slist_free_all(headers);
        }
        curl_easy_cleanup(c);
        return r;
    }
}
