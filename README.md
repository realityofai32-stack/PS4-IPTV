# V7 IPTV

Native, open-source IPTV and media client for jailbroken PlayStation 4 consoles, designed for TV and DualShock 4.

<p align="center">
  <a href="../../releases/latest"><img src="https://img.shields.io/github/v/release/realityofai32-stack/PS4-IPTV?label=release" alt="Latest release"></a>
  <img src="https://img.shields.io/badge/platform-PlayStation%204-003791" alt="Platform: PlayStation 4">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue" alt="License: GPL-3.0-or-later"></a>
</p>

<p align="center">
  <strong>Xtream Codes · M3U / M3U8 · Live TV · Movies · Series · Offline Downloads</strong>
</p>

<p align="center">
  <a href="../../releases/latest"><strong>Download Latest Release</strong></a>
  · <a href="../../issues">Report a Bug</a>
  · <a href="#building-from-source">Build from Source</a>
</p>

> **V7 IPTV is a client only.** It ships with no channels, movies, series, accounts, subscriptions, playlists or
> provider credentials, and it is not affiliated with any IPTV provider. Bring your own legitimate source and make
> sure you have permission to access, play and download its content.

<p align="center">
  <img src="docs/screenshots/home.jpg" alt="V7 IPTV Home: navigation, Continue Watching hero with Resume and Details, and Continue Watching cards" width="100%">
</p>

V7 IPTV is the redesigned evolution of the PS4 IPTV project: same app, same title ID and same data on the console,
with a new interface and name.

## What's new in v0.3.0

- **Complete V7 IPTV redesign**: new V7 branding and logo, a neutral black / charcoal TV interface and nine
  selectable accent themes
- **Redesigned screens**: Home, Movies, Series, Movie and Series details, Live TV, Search and Settings, with
  updated Downloads and Favorites
- **Artwork**: posters are fitted to their own shape instead of being cropped, sharper poster images, provider
  backdrops where the source offers them, with a safe poster fallback
- **Accelerated hold-to-scroll** on every list
- **Next Episode auto-play**: a 45-second countdown at the end of an episode with *Play now* and *Cancel*, from
  Series Detail and Continue Watching, across season boundaries, and offline-aware
- **Native Kubuntu build workflow**: this release was built and validated on Kubuntu Linux

### Accent themes

*Settings › Appearance* offers **V7 Blue** (default), **Cyan**, **Teal**, **Green**, **Purple**, **Magenta**,
**Red**, **Orange** and **White**. The base interface always stays neutral black / charcoal; the accent colours only
the focus, buttons, active tabs, progress bars, toggles and selection indicators.

## Screenshots

<table>
  <tr>
    <td width="50%"><img src="docs/screenshots/movies.jpg" alt="Movies: categories and poster grid"></td>
    <td width="50%"><img src="docs/screenshots/series.jpg" alt="Series: categories and poster grid"></td>
  </tr>
  <tr>
    <td align="center"><sub>Movies</sub></td>
    <td align="center"><sub>Series</sub></td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/series-detail.jpg" alt="Series details with seasons, episodes and resume position"></td>
    <td><img src="docs/screenshots/search.jpg" alt="Search across movies, series and live channels with the on-screen keyboard"></td>
  </tr>
  <tr>
    <td align="center"><sub>Series details: seasons, episodes, resume</sub></td>
    <td align="center"><sub>Search across Movies, Series and Live TV</sub></td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/settings.jpg" alt="Settings: Appearance with the nine accent themes"></td>
    <td><img src="docs/screenshots/downloads.jpg" alt="Downloads with progress and resume"></td>
  </tr>
  <tr>
    <td align="center"><sub>Settings: accent themes</sub></td>
    <td align="center"><sub>Downloads</sub></td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/live-tv.jpg" alt="Live TV: categories, channels and the channel card"></td>
    <td><img src="docs/screenshots/favorites.jpg" alt="Favorites: channels, movies and series"></td>
  </tr>
  <tr>
    <td align="center"><sub>Live TV</sub></td>
    <td align="center"><sub>Favorites</sub></td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/video.jpg" alt="Episode playback with progress, audio and subtitle status"></td>
    <td><img src="docs/screenshots/video-options.jpg" alt="Playback options: audio, subtitles, video, subtitle style"></td>
  </tr>
  <tr>
    <td align="center"><sub>Playback</sub></td>
    <td align="center"><sub>Playback options</sub></td>
  </tr>
</table>

<sub>Screenshots were captured on a real PS4. Channel names, logos, posters, artwork, video frames and media
metadata shown in them come from the user's own configured source and belong to their respective owners; they are
not distributed with V7 IPTV.</sub>

## Features

- **Sources**: Xtream Codes accounts and M3U / M3U8 playlists, with multiple sources
- **Live TV** with categories, channel logos, favorites, fast zapping and MPEG-TS / HLS fallback
- **Movies & Series** with poster grids, sorting, metadata, seasons and episodes
- **Search** across Live TV, Movies and Series, including tolerant matching for small typos
- **Favorites, Continue Watching and Recently Watched** with exact resume positions
- **Offline downloads** for movies and episodes: resumable, interrupted downloads continue, offline playback
- **Multiple audio tracks and embedded subtitles**, switchable during playback, with subtitle size, position and
  shadow
- **Video geometry**: Aspect Ratio, Crop / Fill, Zoom and video position, applied live during playback
- **Next Episode** countdown and auto-play for series episodes
- **English and Turkish UI**, plus Unicode fallback fonts for international titles and channel names
- **DualShock 4 navigation** designed for the TV, with accelerated hold-to-scroll and an on-screen keyboard

### Next Episode

Series episodes support a **45-second Next Episode countdown**. During the final 45 seconds of an episode that has
a next episode, a small panel appears in the bottom-right corner:

- **✕ Cross**: play the next episode now
- **○ Circle**: cancel auto-play for this playback

If no button is pressed, the next episode starts automatically when the current episode genuinely ends; nothing
of the ending is cut. It works for episodes started from Series Detail and from Continue Watching, follows the
provider's episode order across season boundaries, and respects *Settings › Playback › Auto-play next episode*
(on by default; when off, the usual *Up next* screen appears at the end). In offline mode the next episode must
already be downloaded.

### What each source type supports

| | Xtream Codes | M3U / M3U8 playlist |
|---|---|---|
| Live TV, categories, logos | ✓ | ✓ (`group-title`, `tvg-logo`) |
| Movies, Series | ✓ | – (entries are always treated as live channels) |
| Search, Favorites, Recently Watched | ✓ | ✓ |
| Continue Watching, offline downloads | ✓ (Movies, episodes) | – |
| Account status and expiry | ✓ | – |

Playlist entries are never guessed to be movies or series. Use an Xtream Codes source for Movies, Series,
Continue Watching and downloads.

## Requirements

- A **jailbroken PS4** or another compatible homebrew environment that can install fake-signed PKG files.
  V7 IPTV does not include or describe any exploit.
- **Your own source**: an Xtream Codes compatible service (server address, username, password) and/or the address
  of an M3U / M3U8 playlist you are entitled to use. The project provides no content, accounts or playlists.
- Network access for streaming; downloaded content plays offline.

## Quick Start

1. Download the latest PKG from [Releases](../../releases/latest).
2. Install it with the PS4 package installer of your homebrew environment.
3. Launch **V7 IPTV**.
4. Choose **Add Source**.
5. Select **Xtream Codes** or **M3U / M3U8 Playlist**.
6. Enter your own valid source information and start watching.

## Installing

1. Download `IV0001-IPTV00002_00-IPTV000020002000.pkg` from the [Releases](../../releases) page (or build it
   yourself, below). Each release lists the PKG's SHA-256.
2. Install it with the package installer of your PS4 homebrew environment. It installs over PS4 IPTV v0.2.0 as an
   update (same title ID `IPTV00002`).
3. Start **V7 IPTV**.

The app keeps its data in `/data/PS4IPTV/` on the console (sources, settings, history, caches, downloads); the
path is unchanged from earlier versions, so an update keeps everything. Sources, including passwords and playlist
addresses, are stored only on the console.

## Using the app

| Button | Action |
|---|---|
| ✕ | Select / open / play; in the player: pause / resume; during the Next Episode countdown: play now |
| ○ | Back; during the Next Episode countdown: cancel auto-play |
| □ | Favorite; on an episode: download it; on a Continue Watching card: remove it from the row |
| △ | Search (Live TV, Movies, Series); in the player: technical info |
| OPTIONS | Settings (Home); sort & refresh (Movies, Series); Refresh Playlist / Playlist Info (playlist sources); source actions (Sources); playback options (player) |
| L1 / R1 | Previous / next category, season, tab or episode |
| L2 / R2 | Page up / down; in the player: seek 1 minute |
| D-pad ← → | Seek 10 seconds in the player |

**Accelerated hold-to-scroll**: hold a direction to scroll quickly. After 0.3 s the list moves every 90 ms, then
60 ms, 40 ms and, after 3.5 s, every 30 ms. A short press always moves exactly one item. *Settings › Appearance ›
Smooth scrolling* turns the gliding animation off.

### Playback options

Press OPTIONS while a movie or episode plays:

- **Audio** and **Subtitles**: every track the file contains, named by language; commentary and forced tracks
  are labelled.
- **Video**: four independent controls, applied instantly without restarting playback:
  - **Aspect Ratio**: Auto / Source, 16:9, 16:10, 4:3, 5:4, 1:1, 1.85, 2.21, 2.35, 2.39, 2.40. Never cuts anything.
  - **Crop / Fill**: *Fill Screen* fills the screen without distortion and cuts what does not fit; a ratio keeps
    only the centre of the picture in that shape (e.g. to remove black bars that are part of the video).
  - **Zoom** (100–150 %) and a horizontal / vertical **position** for a zoomed or cropped picture.
  - **Reset Video Geometry** returns to the defaults.
  Defaults are in Settings; a change in the panel applies to the current playback only (or *Set as default*).
- Subtitle size, position and shadow; technical information.

## M3U / M3U8 playlists

A playlist source is a **list of Live TV channels**. Add one with *Add Source › M3U / M3U8 Playlist*: a name and
the playlist address, for example `https://example.com/list.m3u`.

- **Addresses**: `http://` and `https://` playlist URLs (downloaded with libcurl: redirects, gzip, timeouts), or
  a file copied to `/data/PS4IPTV/playlists/` on the console. An optional **User-Agent** is sent with the
  playlist request and the streams.
- **Format**: Extended M3U - `#EXTM3U`, `#EXTINF` with `tvg-id`, `tvg-name`, `tvg-logo`, `group-title` and
  `tvg-chno`, `#EXTGRP`, `#EXTVLCOPT:http-user-agent`. UTF-8 with or without BOM, any line endings. A broken
  entry is skipped; the rest of the playlist still loads.
- **Categories**: Favorites, All Channels, then the playlist's groups in playlist order; channels without a group
  are under *Uncategorized*.
- **Saved copy and refresh**: the playlist is kept on the console and refreshed in the background when it is
  older than 12 hours (*Options › Refresh Playlist* refreshes now). A failed download never empties the list.
  *Options › Playlist Info* shows channel, group and logo counts and skipped entries, never the address.
- **Playback**: each channel's own URL (MPEG-TS, HLS and other HTTP streams) on the same player as Xtream
  channels. Channels with **HTTPS stream URLs cannot be played** in this version (see *Known limitations*); they
  show a clear message and Playlist Info counts them.

## Offline downloads

Movies and episodes from Xtream sources can be downloaded to the console's internal storage and watched later
without a connection.

- The provider's **original file** is stored byte for byte: every audio and subtitle track, nothing transcoded.
- Stored in `/data/PS4IPTV/downloads/`, named by content ID, never by title.
- One download at a time, in queue order, over HTTP(S). **Interrupted transfers resume** where they stopped
  (HTTP Range); a server that cannot resume is never appended to. After a network loss a download waits and
  continues by itself.
- Downloads **pause while a video plays**. Free space is checked before and during a download (at least 512 MB
  stay free); files larger than 4 GB are supported.
- **Offline mode**: when the provider cannot be reached at start-up, choose *Continue offline*; downloaded titles
  play without any request to the provider. Progress is shared between the stream and the download.
- *Settings › Diagnostics › Download diagnostics* shows the measured network and disk speed (no addresses or
  passwords).

**Use downloads only for services and content where you have permission to store content locally.** The feature
saves the files your service delivers to you; it does not remove DRM, decrypt content or circumvent any access
control.

## Known limitations

- **HTTPS media streams are not supported.** The bundled FFmpeg playback build has no TLS protocol, so streams are
  played over HTTP (also RTMP / UDP / RTP). This affects HTTPS entries in M3U playlists and HTTPS stream URLs in
  general. The Xtream API, playlist downloads, images and offline downloads use libcurl and do work over HTTPS.
- **M3U / M3U8 sources are Live TV only**: no movie / series detection, Continue Watching or downloads.
- No EPG / programme guide, recording or timeshift.
- Video is decoded in software; very high bitrates or 4K may not play smoothly.
- **Scripts**: Thai, Devanagari and other Indic scripts, emoji and rare CJK characters are not covered and show a
  box. Arabic and Hebrew are reordered right-to-left with basic Arabic joining (FriBidi), not full OpenType
  shaping; the interface itself stays left-to-right.
- **Subtitles** are drawn with the interface font: subtitles in scripts other than Latin, Greek and Cyrillic
  cannot be drawn.
- Provider metadata (titles, channel names, categories, plots) is shown exactly as the provider sends it,
  regardless of the interface language.

## Building from source

**v0.3.0 was built and validated natively on Kubuntu Linux** (x86-64) with the Bash scripts below. The Windows
11 / PowerShell workflow is kept for contributors but was not re-tested for this release.

The toolchain reproduces [pPlay](https://github.com/Cpasjuste/pplay)'s PS4 build: **LLVM/Clang 12.0.1**,
**CMake 3.31**, **Ninja**, **pkgconf**, the **PacBrew PS4 sysroot** (musl, libc++, SDL2, libmpv 0.34.1,
FFmpeg 5.0, libcurl 7.80 + Mbed TLS, ...) and the **OpenOrbis** tools (`create-fself`, `create-gp4`) plus
**LibOrbisPkg** `PkgTool.Core` for the PKG. Everything lives in `toolchain\` / `toolchain/` inside the repository
(git-ignored); nothing is installed system-wide.

Both workflows need the pPlay checkout in `external/pplay-reference` at the pinned commit
`039954636c1f2ef20b5ec188226e7ed245cbb763`: V7 IPTV compiles libcross2d and two pPlay sources from it and copies
pPlay's PS4 system modules into the PKG.

### Kubuntu / Linux (release build)

The project-local toolchain is described component by component in [docs/linux-build.md](docs/linux-build.md).
`PkgTool.Core` needs **OpenSSL 1.1**, which current Kubuntu / Ubuntu releases no longer ship: a project-local
OpenSSL 1.1.1 in `toolchain/openssl-1.1.1/` is used for that tool only (`cmake/ps4-linux.cmake`). System packages:
`g++` (host tests), `pkgconf`, `python3`, `git`.

```bash
git clone https://github.com/realityofai32-stack/PS4-IPTV.git
cd PS4-IPTV
git clone --recursive https://github.com/Cpasjuste/pplay external/pplay-reference
git -C external/pplay-reference checkout --recurse-submodules 039954636c1f2ef20b5ec188226e7ed245cbb763
# toolchain/ as described in docs/linux-build.md, then:
scripts/build-ps4iptv.sh Release                 # -> build/ps4iptv-release/IV0001-IPTV00002_00-IPTV000020002000.pkg
scripts/test-host.sh                             # host unit tests (g++ -Wall -Wextra -Werror)
scripts/test-download-integration.sh             # libcurl transport + downloads + M3U against a local test server
scripts/test-credential-guard.sh                 # self-test of the PKG credential guard
scripts/verify-ps4iptv.sh Release                # full release validation
```

### Windows 11

Builds run natively with PowerShell (no WSL, VM or Docker). `scripts\setup-toolchain.ps1` downloads every
component, checks each against a pinned SHA-256 and unpacks it into `toolchain\`; only LLVM's official installer
needs one administrator (UAC) prompt. Prerequisites: Windows 11, PowerShell 5.1, Git, Python 3, the .NET runtime
(for PkgTool.Core) and, for the host tests only, Visual Studio 2022 Build Tools with the Windows SDK. The first
setup downloads about 0.5 GB.

```powershell
git clone https://github.com/realityofai32-stack/PS4-IPTV.git
cd PS4-IPTV
powershell -ExecutionPolicy Bypass -File scripts\setup-toolchain.ps1        # once: toolchain\ (pinned, verified)
powershell -ExecutionPolicy Bypass -File scripts\build-pplay-reference.ps1  # once: pPlay checkout + reference build
powershell -ExecutionPolicy Bypass -File scripts\build-ps4iptv.ps1          # -> build\ps4iptv-release\*.pkg
powershell -ExecutionPolicy Bypass -File scripts\run-host-tests.ps1                # unit tests (MSVC)
powershell -ExecutionPolicy Bypass -File scripts\run-download-integration-test.ps1 # host libcurl + local HTTP server
powershell -ExecutionPolicy Bypass -File scripts\verify-ps4iptv.ps1                # full release validation
```

### Release validation

`verify-ps4iptv.sh` / `verify-ps4iptv.ps1` rebuild from clean and check:

- zero errors and zero warnings from the project's sources;
- all host tests, the localization checks and the download integration test;
- `pkg_validate`, an exact allow-list of packaged files, and a secret scan of the extracted PKG;
- a secret scan of the Git history;
- the linked system modules, against a pPlay reference build;
- that no diagnostic screen can open by itself.

The secret scans compare against values only you know. Put any private value (your server host, username,
password, playlist address) one per line in `config/forbidden_strings.txt`, or a test stream URL in
`config/test_streams.txt` (see `config/test_streams.example.txt`). Both files are git-ignored. Without them the
scans fall back to patterns (authenticated-looking URLs, token parameters, playlists) and report that.

### Keeping credentials out of the repository

`config/test_streams.txt`, `config/forbidden_strings.txt` and all `*.m3u` / `*.m3u8` files are git-ignored (only
the sanitized parser fixtures in `tests/fixtures/m3u/`, which use example hosts, are tracked). The build fails if
any of those values or any playlist file would end up in the PKG, and `scripts/scan-git-history.py` checks every
commit for them, for authenticated-looking stream URLs and for playlists outside the fixtures, without printing
any of them.

## Project layout

```
src/app/          application shell, services (Xtream, catalogs, progress, next episode, offline glue)
src/core/         shared helpers: JSON, URLs, UTF-8, formatting
src/downloads/    download engine: manifest, resumable transfers (libcurl), queue, storage checks
src/i18n/         localization tables (English, Turkish)
src/images/       logo / poster / backdrop loading, decoding, disk and memory cache
src/iptv/         Xtream API and Extended M3U parsing, catalogs, search index, sorting
src/network/      HTTP client (libcurl) and background jobs
src/player/       playback adapter over pPlay's mpv wrapper, tracks, stability, video geometry
src/platform/     PS4 / host platform glue, controller input, clock
src/screens/      TV screens
src/storage/      JSON stores on the console (sources, settings, favorites, history)
src/ui/           V7 design system: theme, palette, layout, artwork fitting, text with fallback fonts, widgets
tests/host/       host unit tests, download and playlist integration tests
tests/fixtures/   sanitized M3U parser fixtures
scripts/          toolchain setup, build, tests, release validation (Bash and PowerShell)
cmake/            Linux and Windows PS4 toolchain files, PKG credential scan
third_party/      bundled fonts and their licenses
docs/             screenshots, design notes and the hardware test checklists used for each checkpoint
```

**Adding a language**: copy `src/i18n/strings_en.cpp`, translate every entry (keep the keys and the `{0}`
placeholders) and register it in `src/i18n/i18n.cpp`. The host tests reject missing keys, blank text, mismatched
placeholders and screen text that bypasses the tables.

See [CONTRIBUTING.md](CONTRIBUTING.md) for bug reports and pull requests, and [SECURITY.md](SECURITY.md) for
reporting credential leaks or security issues.

## License

V7 IPTV is free software under the **GNU General Public License v3.0 or later**; see [LICENSE](LICENSE).
It builds on pPlay and libcross2d (GPL-3.0, Cpasjuste), mpv, FFmpeg, SDL2, libass, FreeType, FriBidi, libcurl,
Mbed TLS, stb_image, the OpenOrbis PS4 Toolchain and PacBrew's PS4 ports, and the Inter, DejaVu Sans and Droid
Sans Fallback typefaces. [NOTICE](NOTICE) lists every component with its authors and license.

Each GitHub release is built from the tagged source. The tag is the complete corresponding source of the
released PKG, together with the pinned upstream components that the build scripts download.

PlayStation and DualShock are trademarks of Sony Interactive Entertainment Inc. This project is not affiliated
with or endorsed by Sony.
