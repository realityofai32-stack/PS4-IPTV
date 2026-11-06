// Embedded subtitles through the console's FFmpeg version: demuxes the first subtitle stream of a file with FFmpeg
// 5.0 (the host build of the PS4 configuration, tests/https/build-host-tls-stack.sh) and decodes it the way mpv
// 0.34.1's lavc_conv does (avcodec_decode_subtitle2, ASS output), printing one line per event:
//   <start ms> <end ms> <ASS dialogue text>
// The test compares those lines with the source SRT (same text, same timing): an embedded Arabic track must reach
// libass exactly like an external one. Usage: embedded_dump FILE
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: embedded_dump FILE\n");
        return 2;
    }
    AVFormatContext *fmt = NULL;
    if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    int si = av_find_best_stream(fmt, AVMEDIA_TYPE_SUBTITLE, -1, -1, NULL, 0);
    if (si < 0) {
        fprintf(stderr, "no subtitle stream\n");
        return 1;
    }
    AVStream *st = fmt->streams[si];
    const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext *ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(ctx, st->codecpar);
    ctx->pkt_timebase = st->time_base;
    AVDictionary *opts = NULL;
    av_dict_set(&opts, "flags2", "+ass_ro_flush_noop", 0);   // mpv lavc_conv.c (FFmpeg 5.0 always decodes to ASS)
    if (!dec || avcodec_open2(ctx, dec, &opts) < 0) {
        fprintf(stderr, "no decoder\n");
        return 1;
    }
    av_dict_free(&opts);
    fprintf(stderr, "codec %s\n", dec->name);
    AVPacket *pkt = av_packet_alloc();
    long events = 0;
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == si) {
            AVSubtitle sub;
            int got = 0;
            if (avcodec_decode_subtitle2(ctx, &sub, &got, pkt) >= 0 && got) {
                int64_t start = av_rescale_q(pkt->pts, st->time_base, (AVRational) {1, 1000});
                int64_t dur = av_rescale_q(pkt->duration, st->time_base, (AVRational) {1, 1000});
                for (unsigned i = 0; i < sub.num_rects; i++) {
                    const char *ass = sub.rects[i]->ass;
                    if (!ass) {
                        continue;
                    }
                    // "ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text": the text after 8 commas
                    const char *p = ass;
                    for (int c = 0; c < 8 && p; c++) {
                        p = strchr(p, ',');
                        if (p) {
                            p++;
                        }
                    }
                    printf("%" PRId64 " %" PRId64 " %s\n", start, start + dur, p ? p : ass);
                    events++;
                }
                avsubtitle_free(&sub);
            }
        }
        av_packet_unref(pkt);
    }
    fprintf(stderr, "events %ld\n", events);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    return 0;
}
