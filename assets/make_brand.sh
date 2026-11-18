#!/usr/bin/env bash
# Regenerates the V7 Player package images from the app's brand geometry (src/ui/brand_art.*):
#   assets/pic1.png   1920 x 1080 RGB: the PS4's start image, the in-app loading screen's first frame in V7 Blue
#   assets/icon0.png  512 x 512 RGB:   the home-screen icon (V7 Blue)
# Needs g++ and Python 3 with Pillow (PNG compression only). --previews also writes every accent's loading screen
# to build/brand/ for review. The PNGs are committed: the PS4 build only copies them.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$repo/build/brand"
mkdir -p "$out"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I "$repo/src" -o "$out/brand_render" \
    "$repo/assets/brand/brand_render.cpp" "$repo/src/ui/brand_art.cpp"
"$out/brand_render" "$out" ${1:-}
python3 -I - "$out" "$repo/assets" <<'PY'
import glob, os, sys
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
for name, size in (('pic1', (1920, 1080)), ('icon0', (512, 512))):
    im = Image.open(os.path.join(src, name + '.ppm')).convert('RGB')
    assert im.size == size, (name, im.size)
    # plain 8-bit RGB, non-interlaced, no ancillary chunks (what the PS4's package images are)
    im.save(os.path.join(dst, name + '.png'), format='PNG', optimize=True)
    print('%s.png %dx%d %d bytes' % (name, im.size[0], im.size[1], os.path.getsize(os.path.join(dst, name + '.png'))))
for p in sorted(glob.glob(os.path.join(src, 'preview-*.ppm'))):
    Image.open(p).save(p[:-4] + '.png', format='PNG')
PY
