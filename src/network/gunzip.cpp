#include <zlib.h>

#include "gunzip.h"

namespace gz {

    bool isGzip(const char *data, size_t size) {
        return size >= 2 && (unsigned char) data[0] == 0x1F && (unsigned char) data[1] == 0x8B;
    }

    Inflater::Inflater(size_t limit) : maxOutput(limit) {
        auto *z = new z_stream();
        // 15 + 32: a zlib or gzip header, detected
        if (inflateInit2(z, 15 + 32) != Z_OK) {
            delete z;
            failed = true;
            message = "zlib unavailable";
            return;
        }
        stream = z;
    }

    Inflater::~Inflater() {
        if (stream != nullptr) {
            auto *z = (z_stream *) stream;
            inflateEnd(z);
            delete z;
        }
    }

    bool Inflater::feed(const char *data, size_t size, const Output &out) {
        if (failed || stream == nullptr) {
            return false;
        }
        auto *z = (z_stream *) stream;
        unsigned char buffer[64 * 1024];
        z->next_in = (Bytef *) data;
        z->avail_in = (uInt) size;
        while (z->avail_in > 0) {
            if (ended) {
                if (*z->next_in != 0x1F) {
                    z->avail_in = 0;   // padding after the last member (some servers add zeros): ignored
                    break;
                }
                // another gzip member follows the one that ended (concatenated .gz files)
                if (inflateReset(z) != Z_OK) {
                    failed = true;
                    message = "damaged compressed data";
                    return false;
                }
                ended = false;
            }
            z->next_out = buffer;
            z->avail_out = sizeof(buffer);
            int rc = inflate(z, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
                failed = true;
                message = "damaged compressed data";
                return false;
            }
            size_t n = sizeof(buffer) - z->avail_out;
            if (n > 0) {
                total += n;
                if (total > maxOutput) {
                    failed = true;
                    message = "too large once decompressed";
                    return false;
                }
                if (!out((const char *) buffer, n)) {
                    failed = true;
                    message = "stopped";
                    return false;
                }
            }
            if (rc == Z_STREAM_END) {
                ended = true;
            } else if (rc == Z_BUF_ERROR && n == 0) {
                break;   // needs more input
            }
        }
        return true;
    }
}
