// Local Media formats from the linked FFmpeg (PS4 build only: host tests feed Formats::addDemuxer themselves).

#include "local_media.h"

#if defined(__PS4__) || defined(PS4IPTV_FFMPEG_FORMATS)   // the host media test links the host FFmpeg build

extern "C" {
#include <libavformat/avformat.h>
}

namespace localmedia {

    Formats ffmpegFormats() {
        Formats f;
        void *opaque = nullptr;
        while (const AVInputFormat *fmt = av_demuxer_iterate(&opaque)) {
            f.addDemuxer(fmt->name ? fmt->name : "", fmt->extensions ? fmt->extensions : "");
        }
        return f;
    }
}

#else

namespace localmedia {

    Formats ffmpegFormats() {
        return Formats();
    }
}

#endif
