// Streaming gzip / zlib decompression (zlib) of downloads that arrive as compressed files - an XMLTV guide named
// "guide.xml.gz" - as opposed to HTTP Content-Encoding, which libcurl already undoes. Bounded: the output goes to a
// callback piece by piece and stops after a limit (a small download can expand enormously).

#ifndef PS4IPTV_NETWORK_GUNZIP_H
#define PS4IPTV_NETWORK_GUNZIP_H

#include <cstddef>
#include <functional>
#include <string>

namespace gz {

    // the gzip signature (1f 8b): the data is a .gz file
    bool isGzip(const char *data, size_t size);

    class Inflater {

    public:

        using Output = std::function<bool(const char *data, size_t size)>;   // false: stop

        explicit Inflater(size_t maxOutput);

        ~Inflater();

        Inflater(const Inflater &) = delete;

        Inflater &operator=(const Inflater &) = delete;

        // compressed bytes in, decompressed bytes to `out`. false: damaged data, more than maxOutput bytes, or `out`
        // stopped (error() says which). Concatenated gzip members are read one after the other.
        bool feed(const char *data, size_t size, const Output &out);

        // every member read to its end (a truncated download is not complete)
        bool complete() const { return ended && !failed; }

        const std::string &error() const { return message; }

        size_t produced() const { return total; }

    private:

        void *stream = nullptr;   // z_stream (zlib.h stays out of this header)
        size_t maxOutput;
        size_t total = 0;
        bool ended = false;
        bool failed = false;
        std::string message;
    };
}

#endif // PS4IPTV_NETWORK_GUNZIP_H
