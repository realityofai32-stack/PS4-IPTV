#!/usr/bin/env python3
# Compares the events FFmpeg 5.0 decoded from an embedded track (embedded_dump output: "<start> <end> <text>") with
# the source subtitle file: same number of events up to LIMIT ms, same text (lines joined with \N), same start / end
# to the millisecond (ASS sources: centiseconds). Writes the decoded events as SRT for the renderer bench.
# Usage: compare_embedded.py SOURCE.srt|SOURCE.ass DUMP.txt LIMIT_MS OUT.srt
import re
import sys


def srt_events(path):
    text = open(path, encoding="utf-8-sig").read().replace("\r\n", "\n")
    out = []
    for block in text.strip().split("\n\n"):
        lines = block.split("\n")
        m = [i for i, l in enumerate(lines) if "-->" in l]
        if not m:
            continue
        t = lines[m[0]]
        a, b = [x.strip() for x in t.split("-->")]

        def ms(s):
            h, mi, rest = s.split(":")
            se, mil = re.split("[,.]", rest)
            return ((int(h) * 60 + int(mi)) * 60 + int(se)) * 1000 + int(mil)
        out.append((ms(a), ms(b), "\\N".join(lines[m[0] + 1:])))
    return out


def ass_events(path):
    out = []
    for line in open(path, encoding="utf-8-sig"):
        if not line.startswith("Dialogue:"):
            continue
        f = line[len("Dialogue:"):].strip().split(",", 9)

        def ms(s):
            h, mi, se = s.split(":")
            return int(round(((int(h) * 60 + int(mi)) * 60 + float(se)) * 1000))
        out.append((ms(f[1]), ms(f[2]), f[9].rstrip("\n")))
    return out


def main():
    src, dump, limit, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
    want = (ass_events if src.endswith(".ass") else srt_events)(src)
    want = sorted(e for e in want if e[0] < limit)
    got = []
    for line in open(dump, encoding="utf-8"):
        a, b, t = line.rstrip("\n").split(" ", 2)
        got.append((int(a), int(b), t))
    got.sort()
    bad = 0
    if len(got) != len(want):
        print("event count differs: %d decoded, %d in the source" % (len(got), len(want)))
        bad += 1
    for g, w in zip(got, want):
        if g != w:
            bad += 1
            if bad <= 3:
                print("differs: decoded %r source %r" % (g[:2], w[:2]))
    with open(out, "w", encoding="utf-8") as f:
        for i, (a, b, t) in enumerate(got, 1):
            def ts(x):
                return "%02d:%02d:%02d,%03d" % (x // 3600000, x // 60000 % 60, x // 1000 % 60, x % 1000)
            f.write("%d\n%s --> %s\n%s\n\n" % (i, ts(a), ts(b), t.replace("\\N", "\n")))
    print("embedded %s: %d event(s), %d difference(s)" % (src.rsplit("/", 1)[-1], len(got), bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
