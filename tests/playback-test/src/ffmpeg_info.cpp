#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
}

#include "ffmpeg_info.h"

namespace {
    std::string versionString(unsigned v) {
        return std::to_string(AV_VERSION_MAJOR(v)) + "." + std::to_string(AV_VERSION_MINOR(v)) + "."
               + std::to_string(AV_VERSION_MICRO(v));
    }
}

FfmpegInfo queryFfmpegInfo() {
    FfmpegInfo info;
    info.version = av_version_info();
    info.libVersions = "avformat " + versionString(avformat_version())
                       + ", avcodec " + versionString(avcodec_version())
                       + ", avutil " + versionString(avutil_version());

    void *opaque = nullptr;
    const char *name;
    while ((name = avio_enum_protocols(&opaque, 0)) != nullptr) {
        info.inputProtocols.emplace_back(name);
        if (strcmp(name, "http") == 0) {
            info.hasHttp = true;
        } else if (strcmp(name, "https") == 0) {
            info.hasHttps = true;
        } else if (strcmp(name, "tls") == 0) {
            info.hasTls = true;
        }
    }

    for (const char *fmt: {"mpegts", "hls", "mov", "matroska"}) {
        info.demuxers.push_back(std::string(fmt) + ":" + (av_find_input_format(fmt) != nullptr ? "yes" : "NO"));
    }
    for (const char *dec: {"h264", "hevc", "mpeg2video", "aac", "aac_latm", "ac3", "eac3", "mp2", "mp3"}) {
        info.decoders.push_back(std::string(dec) + ":" + (avcodec_find_decoder_by_name(dec) != nullptr ? "yes" : "NO"));
    }
    return info;
}
