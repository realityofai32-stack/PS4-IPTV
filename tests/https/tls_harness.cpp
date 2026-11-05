// HTTPS playback harness: opens a stream with the FFmpeg 5.0 + Mbed TLS 2.16.6 host build exactly the way the PS4's
// libmpv 0.34.1 does, and reports what happened. Used by tests/https/https_it.py.
//
// mpv code paths reproduced (mpv 0.34.1 sources):
//   stream/stream_lavf.c  mp_setup_av_network_options(): user_agent, tls_verify ("1"/"0"), ca_file
//   stream/stream_lavf.c  open_f(): avio_open2(url, AVIO_FLAG_READ, interrupt cb, &dict) for direct streams; the
//                         demuxer then reads through that AVIOContext (seeks included)
//   demux/demux_lavf.c    HLS has format_hack no_stream: avformat_open_input(url, dopts) with dopts = the network
//                         options, and io_open = nested_io_open(), which copies every av_opts entry that is not
//                         already set into each nested open (playlists, segments, keys) - demuxer-lavf-propagate-opts
//                         defaults to yes
// The app's options (Playback::init): tls-verify=yes, tls-ca-file=<bundle>, user-agent.
//
// Every FFmpeg log line is captured raw and as the app would store it (redact::apply after redact::addUrl(url), what
// Playback::open does), and classified with protocols::classifyTls (the player's TLS error mapping).
//
// usage: tls_harness <mode direct|hls> <url> <ca file|-> <verify 0|1> <user agent> <raw log> <redacted log> [seek]

#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

#include "../../src/platform/redact.h"
#include "../../src/player/protocols.h"

namespace {
    std::mutex g_logMutex;
    std::vector<std::string> g_lines;

    void logCallback(void *avcl, int level, const char *fmt, va_list vl) {
        if (level > AV_LOG_VERBOSE) {
            return;   // mpv requests "v" (Playback::init: mpv_request_log_messages(h, "v"))
        }
        char line[2048];
        int prefix = 1;
        av_log_format_line2(avcl, level, fmt, vl, line, sizeof(line), &prefix);
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_lines.emplace_back(line);
    }

    AVDictionary *g_avOpts = nullptr;   // demux_lavf priv->av_opts
    int (*g_defaultIoOpen)(AVFormatContext *, AVIOContext **, const char *, int, AVDictionary **) = nullptr;
    int g_nestedOpens = 0;

    // demux/demux_lavf.c nested_io_open (propagate_opts = yes)
    int nestedIoOpen(AVFormatContext *s, AVIOContext **pb, const char *url, int flags, AVDictionary **options) {
        AVDictionaryEntry *cur = nullptr;
        while ((cur = av_dict_get(g_avOpts, "", cur, AV_DICT_IGNORE_SUFFIX))) {
            if (!*options || !av_dict_get(*options, cur->key, nullptr, 0)) {
                av_dict_set(options, cur->key, cur->value, 0);
            }
        }
        g_nestedOpens++;
        return g_defaultIoOpen(s, pb, url, flags, options);
    }

    std::string err2str(int e) {
        char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(e, buf, sizeof(buf));
        return buf;
    }
}

int main(int argc, char **argv) {
    if (argc < 8) {
        std::fprintf(stderr, "usage: %s direct|hls url ca|- verify ua rawlog redactedlog [seek]\n", argv[0]);
        return 2;
    }
    std::string mode = argv[1], url = argv[2], ca = argv[3];
    bool verify = std::string(argv[4]) == "1";
    std::string ua = argv[5];
    bool seek = argc > 8 && std::string(argv[8]) == "seek";

    av_log_set_level(AV_LOG_VERBOSE);
    av_log_set_callback(logCallback);
    avformat_network_init();
    redact::addUrl(url);   // Playback::open
    // mpv 0.34.1 itself prints the address it opens (player/loadfile.c "Playing: %s" at info level, stream/stream.c
    // "Opening %s" at verbose): those lines reach the app's log too
    g_lines.push_back("[cplayer] Playing: " + url + "\n");
    g_lines.push_back("[stream] Opening " + url + "\n");

    // stream_lavf.c mp_setup_av_network_options
    AVDictionary *dict = nullptr;
    av_dict_set(&dict, "user_agent", ua.c_str(), 0);
    av_dict_set(&dict, "tls_verify", verify ? "1" : "0", 0);
    if (ca != "-") {
        av_dict_set(&dict, "ca_file", ca.c_str(), 0);
    }

    AVFormatContext *fmt = avformat_alloc_context();
    AVIOContext *direct = nullptr;
    int rc;
    if (mode == "hls") {
        av_dict_copy(&g_avOpts, dict, 0);
        g_defaultIoOpen = fmt->io_open;
        fmt->io_open = nestedIoOpen;
        rc = avformat_open_input(&fmt, url.c_str(), av_find_input_format("hls"), &dict);
    } else {
        rc = avio_open2(&direct, url.c_str(), AVIO_FLAG_READ, nullptr, &dict);
        if (rc >= 0) {
            fmt->pb = direct;
            rc = avformat_open_input(&fmt, nullptr, nullptr, nullptr);
        }
    }
    long packets = 0, videoPackets = 0, audioPackets = 0;
    int streams = 0;
    bool seekOk = false;
    std::string status = "ok";
    if (rc < 0) {
        status = "open-failed:" + err2str(rc);
        if (mode != "hls" && fmt) {
            avformat_free_context(fmt);   // avformat_open_input frees it on failure, a failed avio_open2 does not
        }
        fmt = nullptr;
    } else {
        avformat_find_stream_info(fmt, nullptr);
        streams = (int) fmt->nb_streams;
        AVPacket *pkt = av_packet_alloc();
        while (packets < 5000) {
            int r = av_read_frame(fmt, pkt);
            if (r < 0) {
                break;
            }
            packets++;
            AVMediaType t = fmt->streams[pkt->stream_index]->codecpar->codec_type;
            videoPackets += t == AVMEDIA_TYPE_VIDEO;
            audioPackets += t == AVMEDIA_TYPE_AUDIO;
            av_packet_unref(pkt);
        }
        if (seek && fmt->duration > 0) {
            // keyframe seek to the middle, as Playback::seekTo (mpv: HTTP Range request on the same TLS stream)
            int64_t target = fmt->duration / 2;
            seekOk = avformat_seek_file(fmt, -1, INT64_MIN, target, target, 0) >= 0
                     && av_read_frame(fmt, pkt) >= 0;
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
        if (packets == 0) {
            status = "no-packets";
        }
    }

    // logs: raw (what FFmpeg printed) and stored (what the app's log keeps), plus the player's TLS verdict
    std::string tls = "none";
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        std::ofstream raw(argv[6]);
        std::ofstream red(argv[7]);
        for (const auto &l: g_lines) {
            raw << l;
            red << redact::apply(l);
            protocols::TlsFailure f = protocols::classifyTls(l);
            if (f == protocols::TlsFailure::Certificate && tls == "none") {
                tls = "certificate";
            } else if (f == protocols::TlsFailure::CaBundle && tls == "none") {
                tls = "ca-bundle";
            } else if (f == protocols::TlsFailure::Handshake && tls == "none") {
                tls = "handshake";
            }
        }
    }
    std::printf("{\"status\":\"%s\",\"streams\":%d,\"packets\":%ld,\"video\":%ld,\"audio\":%ld,\"nested\":%d,"
                "\"seek\":%s,\"tls\":\"%s\",\"https_playable\":%s}\n",
                status.c_str(), streams, packets, videoPackets, audioPackets, g_nestedOpens, seekOk ? "true" : "false",
                tls.c_str(), protocols::check(url, true) == protocols::Verdict::Ok ? "true" : "false");
    if (fmt) {
        avformat_close_input(&fmt);
    }
    if (direct) {
        avio_closep(&direct);
    }
    av_dict_free(&dict);
    av_dict_free(&g_avOpts);
    avformat_network_deinit();
    return 0;
}
