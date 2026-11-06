#!/usr/bin/env bash
# Host (x86-64 Linux) build of the PS4 subtitle rendering stack for the subtitle tests (tests/subtitles):
#   - FreeType 2.10.1, FriBidi 1.0.4, libass 0.14.0: the versions of PacBrew's ps4-openorbis-freetype 2.10.1-3,
#     ps4-openorbis-libfribidi 1.0.4-2 and ps4-openorbis-libass 0.14.0-2 that libmpv 0.34.1 renders text subtitles
#     with on the console
#   - libass is configured like the PS4 package: no fontconfig, no HarfBuzz, no assembly (the PS4 libass.a has no
#     Fc* / hb_* references and only the C rasterizer: ass_rasterizer_c.o; checked below against the sysroot)
# The PS4 libraries cannot run on Linux; this build runs the same font selection, shaping, bidi and rasterizer code.
# Output: build/host-ass/{freetype,fribidi,libass} (git-ignored).
set -euo pipefail
export LC_ALL=C

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TC="$REPO/toolchain"
OUT="$REPO/build/host-ass"
STAMP="$OUT/.stack-ok"
RECIPE="freetype-2.10.1+fribidi-1.0.4+libass-0.14.0-nofc-nohb-noasm-1"
if [[ -f "$STAMP" && "$(cat "$STAMP")" == "$RECIPE" && "${1:-}" != "--force" ]]; then
    echo "host subtitle stack up to date: $OUT"
    exit 0
fi

DL="$TC/downloads"
FT="$DL/freetype-2.10.1.tar.xz"
FB="$DL/fribidi-1.0.4.tar.bz2"
LA="$DL/libass-0.14.0.tar.xz"
[[ -f "$FT" ]] || curl -fsSL --retry 3 --max-time 300 -o "$FT" \
    https://downloads.sourceforge.net/project/freetype/freetype2/2.10.1/freetype-2.10.1.tar.xz
[[ -f "$FB" ]] || curl -fsSL --retry 3 --max-time 300 -o "$FB" \
    https://github.com/fribidi/fribidi/releases/download/v1.0.4/fribidi-1.0.4.tar.bz2
[[ -f "$LA" ]] || curl -fsSL --retry 3 --max-time 300 -o "$LA" \
    https://github.com/libass/libass/releases/download/0.14.0/libass-0.14.0.tar.xz
sha256sum -c --quiet - <<EOF
16dbfa488a21fe827dc27eaf708f42f7aa3bb997d745d31a19781628c36ba26f  $FT
94bdfe553e004d8bd095b109e182682311dd511740d5083326d1582f1df237be  $FB
881f2382af48aead75b7a0e02e65d88c5ebd369fe46bc77d9270a94aa8fd38a2  $LA
EOF

# the PS4 libass: same object list as a no-fontconfig / no-HarfBuzz / no-asm build
PS4ASS="$TC/pacbrew/opt/pacbrew/ps4/openorbis/usr/lib/libass.a"
if [[ -f "$PS4ASS" ]]; then
    objs="$(ar t "$PS4ASS" | tr '\n' ' ')"
    for o in ass_fontconfig.o ass_shaper_hb ass_rasterizer_x86 x86; do
        [[ "$objs" != *"$o"* ]] || { echo "PS4 libass.a unexpectedly contains $o" >&2; exit 1; }
    done
    [[ "$objs" == *ass_rasterizer_c.o* ]] || { echo "PS4 libass.a: no C rasterizer?" >&2; exit 1; }
fi

rm -rf "$OUT"
mkdir -p "$OUT/src"
tar xJf "$FT" -C "$OUT/src"
tar xjf "$FB" -C "$OUT/src"
tar xJf "$LA" -C "$OUT/src"
CF="-O2 -g -fPIC -Wno-incompatible-pointer-types -Wno-int-conversion -Wno-implicit-function-declaration"

(cd "$OUT/src/freetype-2.10.1" && CFLAGS="$CF" ./configure --prefix="$OUT/freetype" --disable-shared --enable-static \
    --without-zlib --without-bzip2 --without-png --without-harfbuzz --without-brotli > "$OUT/freetype.log" 2>&1 \
    && make -j"$(nproc)" >> "$OUT/freetype.log" 2>&1 && make install >> "$OUT/freetype.log" 2>&1) \
    || { tail -30 "$OUT/freetype.log"; exit 1; }
(cd "$OUT/src/fribidi-1.0.4" && CFLAGS="$CF" ./configure --prefix="$OUT/fribidi" --disable-shared --enable-static \
    --disable-docs > "$OUT/fribidi.log" 2>&1 \
    && make -j"$(nproc)" -C lib >> "$OUT/fribidi.log" 2>&1 && make -C lib install >> "$OUT/fribidi.log" 2>&1 \
    && make install-pkgconfigDATA >> "$OUT/fribidi.log" 2>&1) \
    || { tail -30 "$OUT/fribidi.log"; exit 1; }
export PKG_CONFIG_PATH="$OUT/freetype/lib/pkgconfig:$OUT/fribidi/lib/pkgconfig"
(cd "$OUT/src/libass-0.14.0" && CFLAGS="$CF" ./configure --prefix="$OUT/libass" --disable-shared --enable-static \
    --disable-fontconfig --disable-harfbuzz --disable-asm --disable-require-system-font-provider \
    > "$OUT/libass.log" 2>&1 \
    && make -j"$(nproc)" >> "$OUT/libass.log" 2>&1 && make install >> "$OUT/libass.log" 2>&1) \
    || { tail -30 "$OUT/libass.log"; exit 1; }
grep -q '^#define CONFIG_FONTCONFIG' "$OUT/src/libass-0.14.0/config.h" && { echo "fontconfig enabled?" >&2; exit 1; }
grep -q '^#define CONFIG_HARFBUZZ' "$OUT/src/libass-0.14.0/config.h" && { echo "harfbuzz enabled?" >&2; exit 1; }
echo "$RECIPE" > "$STAMP"
echo "host subtitle stack built: $OUT"
