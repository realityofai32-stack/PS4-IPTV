#!/usr/bin/env python3
"""Local Media playback-path test: representative local files (made with the system ffmpeg) through
tests/media/media_probe (the app's format list + path acceptance + FFmpeg 5.0 open / decode / seek), and a reference
run of the system mpv (a newer mpv than the PS4's 0.34.1: track listing, the same-name external .srt, seeking and
pausing; labelled as reference, not as the console's player).

usage: media_it.py <media_probe> <work dir>
"""

import json
import os
import shutil
import subprocess
import sys

PROBE = sys.argv[1]
WORK = os.path.abspath(sys.argv[2])
MEDIA = os.path.join(WORK, "Local Media Test Çocuk Özel")
os.makedirs(MEDIA, exist_ok=True)


def ffmpeg(args):
    subprocess.run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error"] + args, check=True)


V = ["-f", "lavfi", "-i", "testsrc2=size=640x360:rate=25:duration=12"]
A1 = ["-f", "lavfi", "-i", "sine=frequency=440:duration=12"]
A2 = ["-f", "lavfi", "-i", "sine=frequency=660:duration=12"]
H264 = ["-c:v", "libx264", "-preset", "ultrafast", "-g", "25", "-pix_fmt", "yuv420p"]

SRT = "1\n00:00:01,000 --> 00:00:04,000\nMerhaba dünya\n\n2\n00:00:05,000 --> 00:00:08,000\nİkinci satir\n"


def make():
    if os.path.exists(os.path.join(MEDIA, ".done")):
        return
    sub = os.path.join(WORK, "sub.srt")
    with open(sub, "w", encoding="utf-8") as f:
        f.write(SRT)
    # MKV: H.264, two audio tracks (AAC English, AC-3 Turkish), two embedded subtitles (SubRip, ASS)
    ffmpeg(V + A1 + A2 + ["-i", sub, "-i", sub, "-map", "0:v", "-map", "1:a", "-map", "2:a", "-map", "3:s", "-map", "4:s"]
           + H264 + ["-c:a:0", "aac", "-c:a:1", "ac3", "-c:s:0", "srt", "-c:s:1", "ass",
                     "-metadata:s:a:0", "language=eng", "-metadata:s:a:1", "language=tur",
                     "-metadata:s:s:0", "language=eng", "-metadata:s:s:1", "language=tur",
                     os.path.join(MEDIA, "Film İki Ses.mkv")])
    shutil.copy(sub, os.path.join(MEDIA, "Film İki Ses.srt"))   # same-name external subtitle
    # MP4: H.264 + two AAC tracks + mov_text subtitle
    ffmpeg(V + A1 + A2 + ["-i", sub, "-map", "0:v", "-map", "1:a", "-map", "2:a", "-map", "3:s"] + H264
           + ["-c:a", "aac", "-c:s", "mov_text", "-metadata:s:a:0", "language=eng", "-metadata:s:a:1", "language=tur",
              "-movflags", "+faststart", os.path.join(MEDIA, "movie with spaces.mp4")])
    # MPEG-TS / M2TS: H.264 + AAC + MP2
    for name in ("recording.ts", "BDMV clip.M2TS"):
        ffmpeg(V + A1 + A2 + ["-map", "0:v", "-map", "1:a", "-map", "2:a"] + H264 + ["-c:a:0", "aac", "-c:a:1", "mp2",
                                                                                     "-f", "mpegts",
                                                                                     os.path.join(MEDIA, name)])
    # WebM: VP9 + Opus + WebVTT
    ffmpeg(V + A1 + ["-i", sub, "-map", "0:v", "-map", "1:a", "-map", "2:s", "-c:v", "libvpx-vp9", "-deadline",
                     "realtime", "-cpu-used", "8", "-b:v", "300k", "-c:a", "libopus", "-c:s", "webvtt",
                     os.path.join(MEDIA, "clip.webm")])
    # AVI (MPEG-4 Part 2 + MP3), MOV (H.264 + AAC)
    ffmpeg(V + A1 + ["-c:v", "mpeg4", "-q:v", "5", "-c:a", "libmp3lame", os.path.join(MEDIA, "old.avi")])
    ffmpeg(V + A1 + H264 + ["-c:a", "aac", os.path.join(MEDIA, "camera.MOV")])
    # audio only
    ffmpeg(A1 + ["-c:a", "libmp3lame", os.path.join(MEDIA, "song.mp3")])
    ffmpeg(A1 + ["-c:a", "flac", os.path.join(MEDIA, "song.flac")])
    ffmpeg(A1 + ["-c:a", "libopus", os.path.join(MEDIA, "voice.opus")])
    ffmpeg(A1 + ["-c:a", "aac", os.path.join(MEDIA, "audio.m4a")])
    open(os.path.join(MEDIA, ".done"), "w").close()


EXPECT = {
    # name: (video, audio, subtitles)
    "Film İki Ses.mkv": (1, 2, 2),
    "movie with spaces.mp4": (1, 2, 1),
    "recording.ts": (1, 2, 0),
    "BDMV clip.M2TS": (1, 2, 0),
    "clip.webm": (1, 1, 1),
    "old.avi": (1, 1, 0),
    "camera.MOV": (1, 1, 0),
    "song.mp3": (0, 1, 0),
    "song.flac": (0, 1, 0),
    "voice.opus": (0, 1, 0),
    "audio.m4a": (0, 1, 0),
}

failures = 0


def check(ok, what):
    global failures
    print("%s  %s" % ("ok  " if ok else "FAIL", what))
    if not ok:
        failures += 1


def main():
    make()
    for name, (v, a, s) in EXPECT.items():
        out = subprocess.run([PROBE, os.path.join(MEDIA, name)], check=True, capture_output=True, text=True).stdout
        r = json.loads(out.strip().splitlines()[-1])
        ok = (r["listed"] and r["accepted"] and r["opened"] and r["video"] == v and r["audio"] == a
              and r["subtitles"] == s and (v == 0 or r["video_decoded"]) and r["audio_decoded"] and r["seek"])
        check(ok, "%-24s %-22s v%d a%d s%d decode v=%s a=%s seek=%s  %s" % (
            name, r["container"], r["video"], r["audio"], r["subtitles"], r["video_decoded"], r["audio_decoded"],
            r["seek"], " ".join(r["streams"])))
    # languages of the tracks the player offers (Options > Audio / Subtitles name them by language)
    out = subprocess.run([PROBE, os.path.join(MEDIA, "Film İki Ses.mkv")], check=True, capture_output=True,
                         text=True).stdout
    r = json.loads(out.strip().splitlines()[-1])
    check("audio:aac:eng" in r["streams"] and "audio:ac3:tur" in r["streams"] and "subtitle:subrip:eng" in r["streams"]
          and "subtitle:ass:tur" in r["streams"], "MKV track languages: %s" % r["streams"])
    # not listed by Local Media: subtitles, images, text
    for other in ("Film İki Ses.srt",):
        r = json.loads(subprocess.run([PROBE, os.path.join(MEDIA, other)], check=True, capture_output=True,
                                      text=True).stdout.strip().splitlines()[-1])
        check(not r["listed"], "%s is not offered as media (%d extensions from FFmpeg)" % (other, r["extensions"]))

    # reference: the system mpv (not the console's 0.34.1) - same-name external subtitle, tracks, seek, pause
    mpv = shutil.which("mpv")
    if mpv:
        ver = subprocess.run([mpv, "--version"], capture_output=True, text=True).stdout.splitlines()[0]
        path = os.path.join(MEDIA, "Film İki Ses.mkv")
        p = subprocess.run([mpv, "--no-config", "--vo=null", "--ao=null", "--length=2", "--start=5",
                            "--pause=no", "--msg-level=all=info", "--sub-auto=exact", path],
                           capture_output=True, text=True, timeout=60)
        text = p.stdout + p.stderr
        ext = "[external]" in text and "(subrip)" in text
        print("      reference player: %s" % ver)
        check(ext, "reference mpv: the same-name .srt is loaded as an external subtitle track (sub-auto=exact)")
        check(text.count("Audio --aid") >= 2 or text.count("--aid=") >= 2, "reference mpv: two audio tracks offered")
        check(p.returncode == 0, "reference mpv: plays from 5 s (seek) and ends cleanly")
    print("%d failure(s)" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
