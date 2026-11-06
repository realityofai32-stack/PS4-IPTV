#!/usr/bin/env python3
# Deterministic subtitle fixtures for the subtitle tests (GitHub issue #1, "arabic subtitles lag"): Arabic / RTL sets
# and Latin control sets with the same structure (event count, timing, line count, line length), plus Hebrew,
# Turkish, accented Latin, mixed RTL/LTR, malformed and stress files. No randomness: the same files every run.
# Usage: make_fixtures.py OUT_DIR [--stress]   (--stress also writes the large generated files, not tracked in git)
import os
import sys

AR = [
    "مرحباً بكم في الحلقة الأولى",
    "لا أعرف ماذا حدث هنا، لكننا سنكتشف ذلك.",
    "هل أنت متأكد؟ لم يتبقَّ لدينا وقت!",
    "الطريق إلى المدينة طويل جداً؛ سنصل في الصباح.",
    "قال إنه سيعود قبل منتصف الليل",
    "أين كنت طوال هذا الوقت؟",
    "لا تقلق، كل شيء سيكون على ما يرام",
    "هذه ليست المرة الأولى التي يحدث فيها هذا",
]
EN = [
    "Welcome to the very first episode",
    "I don't know what happened here, but we'll find out.",
    "Are you sure? We have no time left at all!",
    "The road to the city is very long; we arrive in the morning.",
    "He said he would be back before midnight",
    "Where have you been all this time?",
    "Don't worry, everything is going to be fine",
    "This is not the first time this has happened",
]
# Arabic with English words, digits, Latin punctuation and brackets (bidi runs, mirrored brackets)
AR_MIXED = [
    "الحلقة 12: Breaking Point (2008)",
    "سعر التذكرة 45.50 دولار - أي 20% أقل",
    "اتصل بي على الرقم 0555-123-456 غداً",
    "شاهدنا فيلم \"The Long Night\" مع أحمد",
    "الساعة الآن 10:45 مساءً [موسيقى]",
    "قال: \"OK, let's go\" ثم غادر",
]
EN_MIXED = [
    "Episode 12: Breaking Point (2008)",
    "The ticket costs 45.50 dollars - that is 20% less",
    "Call me at 0555-123-456 tomorrow",
    "We watched the film \"The Long Night\" with Ahmed",
    "It is now 10:45 in the evening [music]",
    "He said: \"OK, let's go\" and then left",
]
# fully vocalised Arabic (harakat, shadda, tanwin, superscript alef) - combining marks over every letter
AR_DIACRITICS = [
    "بِسْمِ اللَّهِ الرَّحْمَٰنِ الرَّحِيمِ",
    "إِنَّ مَعَ الْعُسْرِ يُسْرًا",
    "وَقُلْ رَبِّ زِدْنِي عِلْمًا",
    "اَلْعِلْمُ نُورٌ وَالْجَهْلُ ظَلَامٌ",
]
EN_DIACRITICS = [
    "In the name of the most gracious one",
    "Indeed with hardship comes ease",
    "And say: my lord, increase me in knowledge",
    "Knowledge is light and ignorance is dark",
]
# Persian / Urdu letters (presentation forms A) and Arabic-Indic digits
AR_PERSIAN = [
    "این یک آزمایش است: پ چ ژ گ",
    "کتاب را به من بده، لطفاً",
    "۱۲۳۴۵۶۷۸۹۰ و ١٢٣٤٥٦٧٨٩٠",
    "یہ اردو کا ایک جملہ ہے",
]
HE = [
    "שלום, מה שלומך היום?",
    "אני לא יודע מה קרה כאן",
    "הפרק ה-12 מתחיל עכשיו (2008)",
    "בוא נלך הביתה, כבר מאוחר.",
]
TR = [
    "Işığı kapatır mısın? Çok yorgunum.",
    "Ğ, Ü, Ş, İ, Ö, Ç ve ı ğ ü ş i ö ç harfleri",
    "Yarın sabah İstanbul'a gidiyoruz.",
    "Şimdi değil, belki öğleden sonra.",
]
ACCENTED = [
    "Où êtes-vous allés? C'était déjà très tard.",
    "¿Qué pasó aquí? ¡No lo sé, señor!",
    "Größe, Übung und Ärger - schön.",
    "Ação, coração, São Paulo, ñandú, Ærøskøbing.",
]
# a missing script (CJK, emoji) - no subtitle font has these
MISSING = [
    "日本語の字幕テスト",
    "中文字幕测试 😀",
    "한국어 자막",
]

RLM = "‏"   # right-to-left mark: Arabic SRT files from common tools put one at the start of every line


def ts(ms):
    h, ms = divmod(ms, 3600000)
    m, ms = divmod(ms, 60000)
    s, ms = divmod(ms, 1000)
    return "%02d:%02d:%02d,%03d" % (h, m, s, ms)


def srt(events):
    out = []
    for i, (start, end, text) in enumerate(events, 1):
        out.append("%d\n%s --> %s\n%s\n" % (i, ts(start), ts(end), text))
    return "\n".join(out)


def steady(lines, count, two_lines=False, period=2000, show=1800, prefix=""):
    ev = []
    for i in range(count):
        a = lines[i % len(lines)]
        text = prefix + a
        if two_lines:
            text += "\n" + prefix + lines[(i + 3) % len(lines)]
        ev.append((i * period, i * period + show, text))
    return ev


def fast(lines, count):
    # 120 ms per event, back to back, plus an overlapping event every 10th (two on screen)
    ev = []
    for i in range(count):
        ev.append((i * 120, i * 120 + 120, lines[i % len(lines)]))
        if i % 10 == 0:
            ev.append((i * 120 + 40, i * 120 + 400, lines[(i + 1) % len(lines)]))
    ev.sort(key=lambda e: e[0])
    return ev


def long_lines(lines, count):
    ev = []
    for i in range(count):
        text = " ".join(lines[(i + k) % len(lines)] for k in range(12))   # ~ 450 characters, wraps to many lines
        ev.append((i * 3000, i * 3000 + 2800, text))
    return ev


ASS_HEADER = """[Script Info]
ScriptType: v4.00+
PlayResX: 1920
PlayResY: 1080
WrapStyle: 0
ScaledBorderAndShadow: yes

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,64,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,3,1,2,60,60,50,1
Style: Top,Tahoma,56,&H0000FFFF,&H000000FF,&H00000000,&H80000000,-1,0,0,0,100,100,0,0,1,2,0,8,60,60,40,178
Style: Sign,Times New Roman,48,&H00FFFFFF,&H000000FF,&H00202020,&H00000000,0,-1,0,0,100,100,0,0,1,2,2,7,40,40,40,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""


def ass_time(ms):
    h, ms = divmod(ms, 3600000)
    m, ms = divmod(ms, 60000)
    s, ms = divmod(ms, 1000)
    return "%d:%02d:%02d.%02d" % (h, m, s, ms // 10)


def ass(lines, count):
    out = [ASS_HEADER]
    for i in range(count):
        a, b = i * 1500, i * 1500 + 1400
        out.append("Dialogue: 0,%s,%s,Default,,0,0,0,,%s\n" % (ass_time(a), ass_time(b), lines[i % len(lines)]))
        if i % 3 == 0:   # overlapping top line in another font / weight
            out.append("Dialogue: 1,%s,%s,Top,,0,0,0,,{\\fad(150,150)}%s\n"
                       % (ass_time(a), ass_time(b + 900), lines[(i + 2) % len(lines)]))
        if i % 5 == 0:   # positioned, blurred sign
            out.append("Dialogue: 2,%s,%s,Sign,,0,0,0,,{\\pos(300,200)\\blur2\\bord3}%s\n"
                       % (ass_time(a), ass_time(b), lines[(i + 4) % len(lines)]))
    return "".join(out)


def write(path, text, encoding="utf-8"):
    with open(path, "w", encoding=encoding, newline="\n") as f:
        f.write(text)


def main():
    out = sys.argv[1]
    stress = "--stress" in sys.argv
    os.makedirs(out, exist_ok=True)
    n = 150   # 5 minutes of steady events
    sets = {
        "ar_single": steady(AR, n), "en_single": steady(EN, n),
        "ar_multi": steady(AR, n, two_lines=True), "en_multi": steady(EN, n, two_lines=True),
        "ar_rlm": steady(AR, n, prefix=RLM), "en_rlm_control": steady(EN, n),
        "ar_mixed": steady(AR_MIXED, n), "en_mixed": steady(EN_MIXED, n),
        "ar_diacritics": steady(AR_DIACRITICS, n), "en_diacritics": steady(EN_DIACRITICS, n),
        "ar_fast": fast(AR, 1500), "en_fast": fast(EN, 1500),
        "ar_long": long_lines(AR, 60), "en_long": long_lines(EN, 60),
        "fa_ur": steady(AR_PERSIAN, n), "he": steady(HE, n), "tr": steady(TR, n), "accented": steady(ACCENTED, n),
        "missing_glyphs": steady(MISSING, n),
        "mixed_rtl_ltr": steady([x for pair in zip(AR_MIXED, HE, EN, TR) for x in pair], n),
    }
    for name, ev in sets.items():
        write(os.path.join(out, name + ".srt"), srt(ev))
    write(os.path.join(out, "ar_styled.ass"), ass(AR + AR_MIXED, 200))
    write(os.path.join(out, "en_styled.ass"), ass(EN + EN_MIXED, 200))
    # malformed inputs: a parser or renderer must survive them (no crash, no hang)
    bad = srt(steady(AR, 20))
    bad += "\n21\n00:00:99,999 --> 00:00:01,000\nزمن غير صالح\n"            # end before start / out of range
    bad += "\n22\nnot a time line\nنص\n"
    bad += "\n23\n00:01:00,000 --> 00:01:02,000\n"                            # no text
    bad += "\n24\n00:01:03,000 --> 00:01:05,000\n" + "ب" * 20000 + "\n"     # one 20,000-letter word
    write(os.path.join(out, "malformed.srt"), bad)
    with open(os.path.join(out, "invalid_utf8.srt"), "wb") as f:
        body = srt(steady(AR, 20)).encode("utf-8")
        # truncated sequences, lone continuation bytes, overlong NUL, surrogates, > U+10FFFF, CP1256 Arabic bytes
        junk = b"\xd8\n\x80\x80\xc0\x80 \xed\xa0\x80 \xf4\x90\x80\x80 \xc7\xe1\xda\xd1\xc8\xed\xc9 \xff\xfe"
        f.write(body + b"\n21\n00:00:41,000 --> 00:00:43,000\n" + junk + b"\n")
    with open(os.path.join(out, "bom_crlf.srt"), "wb") as f:
        f.write(b"\xef\xbb\xbf" + srt(steady(AR, 20)).replace("\n", "\r\n").encode("utf-8"))
    if stress:
        write(os.path.join(out, "stress_100k_events.srt"), srt(fast(AR, 100000)))
        write(os.path.join(out, "stress_long_line.srt"),
              srt([(0, 60000, " ".join(AR * 400))]))                             # one ~ 12,000-word event


if __name__ == "__main__":
    main()
