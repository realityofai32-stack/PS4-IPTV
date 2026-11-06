#!/usr/bin/env python3
# Builds the subtitle font third_party/fonts-subtitles/V7Subtitles.ttf: Inter SemiBold (the subtitle font since the
# VOD player exists) plus the Arabic, Persian / Urdu, Hebrew glyphs and the bidi format characters of DejaVu Sans.
#
# Why one merged font (GitHub issue #1, "arabic subtitles lag"): the PS4 libass (0.14.0, no fontconfig) has no font
# provider, so a glyph missing from the subtitle font can only fall back to the same default font file. Every Arabic
# letter of every rendered frame then repeated the font search and wrote several log lines, and Arabic was drawn as
# missing-glyph boxes. libass 0.14.0 also leaves the face id of that fallback uninitialised (ass_fontselect.c,
# ass_font_select: the default-path branch never sets *uid), so a second fallback font is not reliable. One font
# that has every glyph needs no fallback at all. Latin / Turkish / Greek / Cyrillic glyphs, metrics, kerning and
# hinting stay Inter's own: Latin subtitles are drawn exactly as before.
#
# Licences: Inter is SIL OFL 1.1 (no reserved font name); the copied DejaVu glyphs are DejaVu's own additions (Arabic,
# Hebrew), which DejaVu places in the public domain, so the merged font is distributed under the OFL as a whole. The
# script refuses to copy any glyph built from a component outside those blocks (the Bitstream Vera Latin glyphs).
#
# Deterministic: same inputs -> byte-identical output (no timestamps). Needs fontTools 4.59.0 (pure Python wheel,
# unpacked under toolchain/fonttools: see docs/linux-build.md). Usage: scripts/make-subtitle-font.py [--check]
import copy
import hashlib
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "toolchain", "fonttools"))
from fontTools.ttLib import TTFont, newTable  # noqa: E402
from fontTools.ttLib.tables._g_l_y_f import Glyph  # noqa: E402
from fontTools import subset  # noqa: E402

INTER = os.path.join(REPO, "third_party", "inter", "Inter-SemiBold.ttf")
DEJAVU = os.path.join(REPO, "third_party", "fonts-fallback", "DejaVuSans.ttf")
OUT = os.path.join(REPO, "third_party", "fonts-subtitles", "V7Subtitles.ttf")

# the blocks taken from DejaVu Sans: Hebrew, Arabic, Hebrew / Arabic presentation forms
RANGES = [(0x0590, 0x05FF), (0x0600, 0x06FF), (0xFB1D, 0xFB4F), (0xFB50, 0xFDFF), (0xFE70, 0xFEFE)]
# bidi / joining format characters: invisible, zero width (DejaVu has these; the rest get an empty glyph)
FORMAT_CHARS = [0x200C, 0x200D, 0x200E, 0x200F, 0x202A, 0x202B, 0x202C, 0x202D, 0x202E,
                0x2066, 0x2067, 0x2068, 0x2069, 0x061C]

FAMILY = "V7 Subtitles"


def in_ranges(cp):
    return any(lo <= cp <= hi for lo, hi in RANGES)


def build():
    inter = TTFont(INTER, recalcTimestamp=False)
    dv = TTFont(DEJAVU, recalcTimestamp=False)
    icmap = inter.getBestCmap()
    dcmap = dv.getBestCmap()
    wanted = sorted(cp for cp in dcmap if (in_ranges(cp) or cp in FORMAT_CHARS) and cp not in icmap)

    # DejaVu subset: only those code points, outlines without hinting programs, no layout tables (libass 0.14 without
    # HarfBuzz shapes Arabic with FriBidi into presentation forms and reads no GSUB / GPOS)
    opts = subset.Options()
    opts.hinting = False
    opts.layout_features = []
    opts.glyph_names = True
    opts.notdef_outline = False
    opts.legacy_kern = False
    opts.drop_tables += ["GSUB", "GPOS", "GDEF", "MATH", "FFTM", "kern"]
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=wanted)
    sub.subset(dv)
    scmap = dv.getBestCmap()
    # a glyph that DejaVu encodes at a code point outside the copied blocks (the Bitstream Vera Latin glyphs among
    # them) must not come along as a component; unencoded helpers (arabic_dot ...) are DejaVu's Arabic additions
    outside = {g for cp, g in dcmap.items() if not (in_ranges(cp) or cp in FORMAT_CHARS)}
    glyf = dv["glyf"]
    for name in dv.getGlyphOrder():
        if name in outside:
            sys.exit("refusing: glyph %s is encoded outside the copied blocks" % name)

    order = list(inter.getGlyphOrder())
    names = set(order)
    rename = {}
    for name in dv.getGlyphOrder():
        if name == ".notdef":
            continue
        new = "dv." + name
        assert new not in names
        rename[name] = new
    iglyf = inter["glyf"]
    ihmtx = inter["hmtx"]
    for old, new in rename.items():
        g = copy.deepcopy(glyf[old])
        if g.isComposite():
            for c in g.components:
                c.glyphName = rename[c.glyphName]
        iglyf.glyphs[new] = g
        ihmtx.metrics[new] = dv["hmtx"].metrics[old]
        order.append(new)
    # empty zero-width glyphs for the format characters neither font has
    added = {cp: rename[g] for cp, g in scmap.items()}
    for cp in FORMAT_CHARS:
        if cp not in icmap and cp not in added:
            name = "uni%04X" % cp
            empty = Glyph()
            empty.numberOfContours = 0
            iglyf.glyphs[name] = empty
            ihmtx.metrics[name] = (0, 0)
            order.append(name)
            added[cp] = name
    inter.setGlyphOrder(order)
    iglyf.glyphOrder = order
    for table in inter["cmap"].tables:
        if table.isUnicode():
            for cp, name in added.items():
                if cp <= 0xFFFF or table.format in (12, 13):
                    table.cmap[cp] = name
    inter["post"].formatType = 3.0   # no glyph names in the file
    inter["post"].extraNames = []
    inter["post"].mapping = {}
    inter["OS/2"].recalcUnicodeRanges(inter)
    inter["OS/2"].usLastCharIndex = min(0xFFFF, max(inter.getBestCmap()))

    name = inter["name"]
    copyright_inter = name.getDebugName(0) or ""
    for rec in list(name.names):
        if rec.nameID in (1, 2, 3, 4, 6, 16, 17, 21, 22):
            name.removeNames(nameID=rec.nameID)
    for nid, text in [(0, copyright_inter + " Arabic and Hebrew glyphs: DejaVu fonts (public domain additions)."),
                      (1, FAMILY), (2, "Regular"), (3, FAMILY + " 1.0 (Inter SemiBold + DejaVu Sans Arabic/Hebrew)"),
                      (4, FAMILY), (5, "Version 1.000"), (6, "V7Subtitles-Regular"),
                      (13, "This Font Software is licensed under the SIL Open Font License, Version 1.1."),
                      (14, "https://openfontlicense.org")]:
        name.setName(text, nid, 3, 1, 0x409)
    inter["head"].created = inter["head"].modified = 0x7C259DC0   # fixed: 2016-01-01 (deterministic output)
    return inter, wanted, added


def main():
    check = "--check" in sys.argv
    font, wanted, added = build()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    tmp = OUT + ".tmp"
    font.save(tmp, reorderTables=True)
    data = open(tmp, "rb").read()
    if check:
        os.remove(tmp)
        same = os.path.exists(OUT) and open(OUT, "rb").read() == data
        print("V7Subtitles.ttf %s (%d code points added, sha256 %s)"
              % ("up to date" if same else "DIFFERS", len(added), hashlib.sha256(data).hexdigest()))
        sys.exit(0 if same else 1)
    os.replace(tmp, OUT)
    print("wrote %s: %d bytes, %d code points added from DejaVu Sans, sha256 %s"
          % (OUT, len(data), len(added), hashlib.sha256(data).hexdigest()))


if __name__ == "__main__":
    main()
