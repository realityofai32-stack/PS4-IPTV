// Read-only capability report of the FFmpeg build linked into the app (the same PacBrew FFmpeg 5.0
// libraries mpv uses). Nothing here decodes or opens media.

#ifndef PS4IPTV_TEST_FFMPEG_INFO_H
#define PS4IPTV_TEST_FFMPEG_INFO_H

#include <string>
#include <vector>

struct FfmpegInfo {
    std::string version;          // av_version_info()
    std::string libVersions;      // avformat/avcodec/avutil
    std::vector<std::string> inputProtocols;
    bool hasHttp = false;
    bool hasHttps = false;
    bool hasTls = false;
    std::vector<std::string> demuxers;   // "name:yes/no" for the formats we care about
    std::vector<std::string> decoders;   // "name:yes/no"
};

FfmpegInfo queryFfmpegInfo();

#endif // PS4IPTV_TEST_FFMPEG_INFO_H
