# Fallback fonts

The UI font (Inter, `third_party/inter`) covers Latin (including Turkish), Greek and Cyrillic. Text in
other scripts (provider channel names, titles, audio / subtitle track names, M3U entries) is drawn with
these fallback fonts, glyph by glyph, only for code points Inter does not have.

| File | Covers (used for) | Size | License | Source |
|---|---|---|---|---|
| `DejaVuSans.ttf` (2.37) | Arabic incl. Presentation Forms-B, Hebrew, Armenian, Georgian, extra Greek / symbols, U+FFFD | 757,076 bytes | Bitstream Vera license + public domain DejaVu changes + Arev license (`LICENSE-DejaVu.txt`) | https://github.com/dejavu-fonts/dejavu-fonts/releases/tag/version_2_37 (`dejavu-fonts-ttf-2.37.zip`, `ttf/DejaVuSans.ttf`) |
| `DroidSansFallback.ttf` | Japanese kana, all 11,172 Hangul syllables, 20,902 CJK unified ideographs | 3,451,744 bytes | Apache License 2.0 (`LICENSE-DroidSansFallback.txt`) | AOSP `platform/frameworks/base`, tag `android-8.1.0_r1`, `data/fonts/DroidSansFallback.ttf` |

SHA-256:

```
7da195a74c55bef988d0d48f9508bd5d849425c1770dba5d7bfc6ce9ed848954  DejaVuSans.ttf
56d3edfb377a8bc3c4b77f4476405c770173c0e31221ad7c77d0a8e1f3039a3e  DroidSansFallback.ttf
```

Both licenses allow redistribution in source and binary form (the fonts are unmodified). Not covered:
Thai, Devanagari and other Indic scripts, Ethiopic, emoji, and the CJK ideographs outside the BMP.
