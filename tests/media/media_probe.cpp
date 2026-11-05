// Local Media playback-path probe: one local file through what the console does with it, using the FFmpeg 5.0 host
// build (same version and demuxer / decoder configuration as the PS4 libraries; tests/https/build-host-tls-stack.sh).
//
//   1. Local Media lists it: localmedia::ffmpegFormats() (the app's function, over FFmpeg's real demuxer table)
//   2. the player accepts the path: protocols::check (Playback::protocolSupported)
//   3. FFmpeg opens it from the path as mpv does (file protocol), finds every stream (video / audio / subtitle,
//      with languages), decodes the first video and audio frame with the build's decoders, and seeks to the middle
//      (a keyframe seek, as Playback::seekTo)
// Prints one JSON line. usage: media_probe <file>

#include <cstdio>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

#include "../../src/media/local_media.h"
#include "../../src/player/protocols.h"

namespace {
    std::string esc(const std::string &s) {
        std::string o;
        for (char c: s) {
            if (c == '"' || c == '\\') {
                o += '\\';
            }
            o += c;
        }
        return o;
    }

    // decodes packets of `stream` until one frame comes out (or the file ends)
    bool decodeOne(AVFormatContext *fmt, int stream) {
        const AVCodecParameters *par = fmt->streams[stream]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(par->codec_id);
        if (!codec) {
            return false;
        }
        AVCodecContext *ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(ctx, par);
        bool got = false;
        if (avcodec_open2(ctx, codec, nullptr) == 0) {
            AVPacket *pkt = av_packet_alloc();
            AVFrame *frame = av_frame_alloc();
            av_seek_frame(fmt, -1, 0, AVSEEK_FLAG_BACKWARD);
            for (int i = 0; i < 2000 && !got && av_read_frame(fmt, pkt) >= 0; i++) {
                if (pkt->stream_index == stream && avcodec_send_packet(ctx, pkt) >= 0) {
                    got = avcodec_receive_frame(ctx, frame) >= 0;
                }
                av_packet_unref(pkt);
            }
            if (!got) {
                avcodec_send_packet(ctx, nullptr);   // flush: decoders with delay
                got = avcodec_receive_frame(ctx, frame) >= 0;
            }
            av_frame_free(&frame);
            av_packet_free(&pkt);
        }
        avcodec_free_context(&ctx);
        return got;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return 2;
    }
    std::string path = argv[1];
    av_log_set_level(AV_LOG_QUIET);
    std::string name = path.substr(path.rfind('/') + 1);
    localmedia::Formats formats = localmedia::ffmpegFormats();
    bool listed = formats.supported(localmedia::extensionOf(name));
    bool accepted = protocols::check(path, true) == protocols::Verdict::Ok;
    AVFormatContext *fmt = nullptr;
    int rc = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
    std::string streams;
    int video = -1, audio = -1, nVideo = 0, nAudio = 0, nSubs = 0;
    bool videoDecoded = false, audioDecoded = false, seekOk = false;
    double duration = 0;
    std::string container;
    if (rc == 0 && avformat_find_stream_info(fmt, nullptr) >= 0) {
        container = fmt->iformat->name;
        duration = fmt->duration > 0 ? fmt->duration / (double) AV_TIME_BASE : 0;
        for (unsigned i = 0; i < fmt->nb_streams; i++) {
            const AVStream *st = fmt->streams[i];
            const char *type = av_get_media_type_string(st->codecpar->codec_type);
            AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", nullptr, 0);
            streams += std::string(streams.empty() ? "" : ",") + "\"" + (type ? type : "?") + ":"
                       + avcodec_get_name(st->codecpar->codec_id) + ":" + (lang ? esc(lang->value) : "") + "\"";
            switch (st->codecpar->codec_type) {
                case AVMEDIA_TYPE_VIDEO:
                    if (!(st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
                        nVideo++;
                        video = video < 0 ? (int) i : video;
                    }
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    nAudio++;
                    audio = audio < 0 ? (int) i : audio;
                    break;
                case AVMEDIA_TYPE_SUBTITLE:
                    nSubs++;
                    break;
                default:
                    break;
            }
        }
        videoDecoded = video >= 0 && decodeOne(fmt, video);
        audioDecoded = audio >= 0 && decodeOne(fmt, audio);
        if (fmt->duration > 0) {
            int64_t target = fmt->duration / 2;
            AVPacket *pkt = av_packet_alloc();
            seekOk = avformat_seek_file(fmt, -1, INT64_MIN, target, target, 0) >= 0 && av_read_frame(fmt, pkt) >= 0;
            av_packet_free(&pkt);
        }
    }
    std::printf("{\"file\":\"%s\",\"listed\":%s,\"accepted\":%s,\"opened\":%s,\"container\":\"%s\",\"duration\":%.2f,"
                "\"video\":%d,\"audio\":%d,\"subtitles\":%d,\"video_decoded\":%s,\"audio_decoded\":%s,\"seek\":%s,"
                "\"streams\":[%s],\"extensions\":%zu}\n",
                esc(name).c_str(), listed ? "true" : "false", accepted ? "true" : "false", rc == 0 ? "true" : "false",
                container.c_str(), duration, nVideo, nAudio, nSubs, videoDecoded ? "true" : "false",
                audioDecoded ? "true" : "false", seekOk ? "true" : "false", streams.c_str(), formats.size());
    if (fmt) {
        avformat_close_input(&fmt);
    }
    return 0;
}
