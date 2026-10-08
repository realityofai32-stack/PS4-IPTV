# PS4 IPTV

A native IPTV client for the PlayStation 4 (homebrew), built for the TV and the DualShock 4. It connects to
**your own Xtream Codes compatible service** or **your own M3U / M3U8 playlist** and plays live channels - and,
from Xtream services, movies and series - on the console, with offline downloads for services that allow
storing content.

> **PS4 IPTV does not provide any content.** It ships with no channels, movies, series, accounts or
> playlists, and it is not affiliated with any IPTV provider. You need a subscription to a service you are
> legitimately entitled to use. You are responsible for the services and content you access with it.

## Features

- **Two kinds of sources**, as many as you like:
  - **Xtream Codes**: Live TV, Movies and Series; connection test, account status and expiry
  - **M3U / M3U8 playlists**: Live TV channels from a playlist URL (see below)
- **Live TV**: categories, channel logos, favorites, fast zapping, MPEG-TS or HLS with automatic fallback
- **Movies and Series**: poster grids by category, details (plot, cast, rating, duration), seasons and episodes
- **Offline downloads** of movies and episodes to the console's internal storage, played back without a connection
- **Continue Watching** with exact resume (to the millisecond), Recently Watched, watched state; remove a card
  without losing its resume position
- **Favorites** for channels, movies and series
- **Search** across Live TV, Movies and Series (Turkish letters, apostrophes and small typos are fine)
- **Sorting**: A–Z, newest added, year, rating, recently watched, provider order
- **Every audio and subtitle track** of a file, switchable during playback; preferred audio / subtitle language
- **VLC-like video geometry**: Aspect Ratio (Auto / Source, 16:9, 16:10, 4:3, 5:4, 1:1, 1.85, 2.21, 2.35, 2.39,
  2.40), Crop / Fill (None, Fill Screen or the same ratios), Zoom (100–150 %) and position, changed live
- **Playback stability presets** (Fast, Balanced, Maximum stability) with stall detection and bounded reconnects
- **DualShock 4 interface** made for the TV: hold-to-scroll that accelerates the longer you hold (up to ~33 steps
  per second; the left stick pushed all the way is faster still), smoothly gliding lists and poster grids,
  controller hints everywhere, on-screen keyboard
- **Near-black TV design** with clear focus and readable contrast from the couch
- **English and Türkçe** user interface (English by default; Settings › Language)
- **International channel and title names**: Latin (incl. Turkish), Greek, Cyrillic, Arabic, Hebrew, Armenian,
  Georgian, Japanese, Korean and Chinese characters are drawn (see *Known limitations*)

Playback uses the proven [pPlay](https://github.com/Cpasjuste/pplay) stack: libmpv 0.34.1 with the PacBrew PS4
patches, FFmpeg 5.0 (software decoding), SDL2 and libcross2d.

## Requirements

- A **jailbroken PS4** (or a compatible homebrew environment) that can install fake-signed PKGs
- Your own, legitimate **Xtream Codes compatible** service (server address, username and password) and/or the
  address of an **M3U / M3U8 playlist** you are entitled to use
- Network access for streaming; downloaded content plays offline

## Installing

1. Download the `IV0001-IPTV00002_00-IPTV00002*.pkg` file from the [Releases](../../releases) page, or build it
   yourself (below).
2. Install it with your homebrew environment's package installer (for example from a USB stick).
3. Start **PS4 IPTV**, choose **Add Source**, then **Xtream Codes** or **M3U / M3U8 Playlist**, and enter the
   details from your provider.

The app keeps its data in `/data/PS4IPTV/` on the console (sources, settings, history, caches, downloads).
Sources, including their passwords and playlist addresses, are stored only on the console. Sources created by
earlier versions are kept as they are (Xtream Codes).

## Using the app

| Button | Action |
|---|---|
| ✕ | Select / open / play |
| ○ | Back |
| □ | Favorite; on an episode: download it; on a Continue Watching card: remove it from the row |
| △ | Search (Live TV, Movies, Series); in the player: technical info |
| OPTIONS | Settings (Home); sort & refresh (Movies, Series); Refresh Playlist / Playlist Info (Live TV of a playlist source); source actions (Sources); playback options (player) |
| L1 / R1 | Previous / next category, season, tab or episode |
| L2 / R2 | Page up / down; in the player: seek 1 minute |
| D-pad ← → | Seek 10 seconds in the player |

Hold a direction to scroll quickly: after 0.3 s the list moves every 90 ms, then 60 ms, 40 ms and, after 3.5 s,
every 30 ms. Releasing stops at once; a short press always moves exactly one item. L2 / R2 still jump a page.
*Settings › Smooth scrolling* turns the gliding animation off (lists then move row by row).

## M3U / M3U8 playlists

A playlist source is a **list of Live TV channels**. Add one with **Add Source › M3U / M3U8 Playlist**: a name
and the playlist address are all it needs, for example

```
Name:          My Playlist
Playlist URL:  https://example.com/list.m3u
```

- **Addresses**: `http://` and `https://` playlist URLs (downloaded with libcurl: redirects, gzip, timeouts), or a
  file you copied to `/data/PS4IPTV/playlists/` (e.g. `/data/PS4IPTV/playlists/sports.m3u`). An optional
  **User-Agent** is sent with the playlist request and the streams, for services that require one.
- **Format**: Extended M3U - `#EXTM3U`, `#EXTINF` with `tvg-id`, `tvg-name`, `tvg-logo`, `group-title` and
  `tvg-chno`, `#EXTGRP` (category when `group-title` is missing) and `#EXTVLCOPT:http-user-agent`. UTF-8 with or
  without BOM, any line endings. A broken entry is skipped on its own; the rest of the playlist still loads.
- **Categories**: Favorites, All Channels, then the playlist's groups in playlist order; channels without a group
  are under *Uncategorized*. Channel logos come from `tvg-logo` (initials when there is none).
- **Search, Favorites and Recently Watched** work for playlist channels. Each source keeps its own favorites and
  history: *TRT 1* in two playlists are two different channels. Channel identities survive app restarts and
  playlist refreshes, including refreshes where the stream tokens change.
- **Saved copy and refresh**: the playlist is checked before the source is saved and kept on the console. Opening
  the source shows the saved list at once and refreshes it in the background when it is older than 12 hours;
  *Options › Refresh Playlist* refreshes now. Only a valid new playlist replaces the list - a failed download
  never empties it. *Options › Playlist Info* shows channel, group and logo counts, skipped entries and the last
  refresh (never the address or any password).
- **Playback**: each channel's own URL (MPEG-TS, HLS `.m3u8` and other HTTP streams) on the same player as Xtream
  channels. **HTTPS stream URLs cannot be played** by this version (see *Known limitations*); such channels show
  a clear message, and Playlist Info counts them.
- **Live only**: playlist entries are never guessed to be movies or series. Use an Xtream Codes source for Movies,
  Series, Continue Watching and downloads.

### Playback options

Press OPTIONS while a movie or episode plays:

- **Audio** and **Subtitles**: every track the file contains, named by language (native name where the console
  font can show it, e.g. Deutsch, Español, Русский; the name in your UI language for scripts it cannot, e.g.
  Japanese), otherwise *Track 1*, *Track 2*. Commentary and forced tracks are labelled.
- **Video**: four independent controls, applied instantly without restarting playback, seeking or changing
  tracks:
  - **Aspect Ratio**: the shape the picture is shown with (Auto / Source = the file's own). Never cuts anything.
  - **Crop / Fill**: *Fill Screen* fills the screen without distortion and cuts what does not fit; a ratio
    (e.g. 2.39:1) keeps only the centre of the picture in that shape, e.g. to remove black bars that are part of
    the video. *None* by default.
  - **Zoom** (100–150 %) on top, and a horizontal / vertical **position** for a zoomed or cropped picture.
  - **Reset Video Geometry** returns to Auto / Source, None, 100 %, centred.
  The defaults are in Settings; a change in the panel applies to that playback only (or *Set as default*).
- Subtitle size, position and shadow; technical information.

## Offline downloads

Movies and episodes can be downloaded to the console's internal storage and watched later without a connection.

- **Original files**: the provider's file is stored byte for byte - original resolution and bitrate, every audio
  and subtitle track, the container metadata. Nothing is transcoded.
- **Where**: `/data/PS4IPTV/downloads/` (`movies/`, `episodes/`, `temp/` for transfers in progress, `metadata/`
  for the download list). File names are generated from content IDs (`movie_<id>.mkv`), never from titles.
- **How**: one download at a time, in queue order, over HTTP(S) with resume. Interrupted transfers continue from
  where they stopped (HTTP Range); a server that cannot resume is never appended to. Network loss puts a download
  in *Waiting for network* and it continues by itself (Settings › *Retry downloads automatically*).
- **Downloads pause while a video plays**, so playback always has the console's full decoding power and network.
- **Free space** is checked before and during a download; at least 512 MB always stay free. Files larger than 4 GB
  are supported.
- **Offline**: when the provider cannot be reached at start-up, choose **Continue offline**. Downloads, the saved
  channel / movie / series lists and your progress remain available; downloaded titles play without any request
  to the provider. Downloads stay playable even if their profile is deleted.
- **Progress is shared**: a movie started as a stream resumes at the same position when played from its download,
  and the other way round. Deleting a download keeps favorites, history and the position.
- Manage everything in **Downloads** (Home) and in **Settings › Storage & downloads**.
- **Speed**: no bandwidth cap; one connection at the speed the network and the provider allow. The app enlarges
  the socket receive buffer so a single connection is not limited by a small TCP window.
  **Settings › Diagnostics › Download diagnostics** shows the measured network and disk speed of the current or
  last download (no addresses or passwords).

**Use downloads only for services and content where you have permission to store content locally.** The feature
saves the files your service delivers to you; it does not remove DRM, decrypt content or circumvent any access
control, and it is not intended to bypass the terms of any service.

## Languages

The interface is available in **English** (default) and **Türkçe**; switch in *Settings › Language* (applies
immediately). Provider metadata - titles, channel names, categories, plots, cast - is always shown exactly as
the provider sends it. The audio/subtitle language of a video is independent of the interface language.

Adding a language: copy `src/i18n/strings_en.cpp`, translate every entry (keep the keys and the `{0}`
placeholders), register it in `src/i18n/i18n.cpp`. The host tests reject missing keys, blank text, mismatched
placeholders and screen text that bypasses the tables.

## Building from source

Builds run **natively on Windows 11** with PowerShell; no WSL, VM or Docker. VS Code is optional.

The toolchain reproduces pPlay's PS4 build: **LLVM/Clang 12.0.1**, **CMake 3.31**, **Ninja**, the **PacBrew PS4
sysroot** (musl, libc++, SDL2, libmpv 0.34.1, FFmpeg 5.0, libcurl 7.80 + Mbed TLS ...) and the **OpenOrbis** tools
(`create-fself`, `create-gp4`) plus **LibOrbisPkg** `PkgTool.Core` for the PKG. Everything is downloaded and
checked against pinned SHA-256 hashes into `toolchain\` inside the repository; nothing is installed system-wide
except LLVM (its installer needs one administrator prompt).

Prerequisites: Git, Python 3, .NET runtime (for PkgTool), and Visual Studio 2022 Build Tools with the Windows
SDK (only for the host tests).

```powershell
git clone https://github.com/<you>/PS4_IPTV.git
cd PS4_IPTV
powershell -ExecutionPolicy Bypass -File scripts\setup-toolchain.ps1        # once: toolchain\ (pinned, verified)
powershell -ExecutionPolicy Bypass -File scripts\build-pplay-reference.ps1  # once: pPlay checkout + reference build
powershell -ExecutionPolicy Bypass -File scripts\build-ps4iptv.ps1          # build\ps4iptv-release\*.pkg
```

Tests and release checks:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run-host-tests.ps1                # unit tests (MSVC)
powershell -ExecutionPolicy Bypass -File scripts\run-download-integration-test.ps1 # libcurl + local HTTP server
powershell -ExecutionPolicy Bypass -File scripts\verify-ps4iptv.ps1                # full release validation
```

Download speed measurements on the PC (they use your provider account from `config\test_streams.txt` and print
no address, user name or password):

```powershell
python -I scripts\measure-provider-download.py --seconds 60   # raw single connection, Range, receive buffer
powershell -ExecutionPolicy Bypass -File scripts\run-download-bench.ps1  # the app's download pipeline
```

`verify-ps4iptv.ps1` rebuilds from clean and checks: zero errors and warnings, all host tests, the localization
checks, the download integration test, `pkg_validate`, an exact allow-list of packaged files, a secret scan of the
extracted PKG and of the complete Git history, the linked system modules, and that no diagnostic screen can open
by itself.

### Keeping your credentials out of the repository

For hardware testing you may keep an authenticated test URL in `config/test_streams.txt` (see
`config/test_streams.example.txt`) and any other private value, one per line, in `config/forbidden_strings.txt`.
Both are git-ignored, and so are `*.m3u` / `*.m3u8` playlists (only the sanitized parser fixtures in
`tests/fixtures/m3u/`, which use example hosts, are tracked). The build fails if any of those values, or any
playlist file, would end up in the PKG, and `scripts/scan-git-history.py` checks every commit for them, for
authenticated-looking stream URLs and for playlists outside the fixtures - without printing any of them.

## Project layout

```
src/app/          application shell, services (Xtream, catalogs, progress, offline glue)
src/downloads/    download engine: manifest, resumable transfers (libcurl), queue, storage checks
src/i18n/         localization tables (English, Türkçe)
src/iptv/         Xtream API and Extended M3U parsing, catalogs, search index, sorting
src/player/       playback adapter over pPlay's mpv wrapper, tracks, stability, video geometry
src/screens/      TV screens
src/ui/           theme, UTF-8 text rendering with fallback fonts, widgets, on-screen keyboard
tests/host/       host unit tests, download and playlist integration tests
tests/fixtures/   sanitized M3U parser fixtures
scripts/          toolchain setup, build, tests, verification, staging
```

## Known limitations

- Streams are played over **HTTP** (also RTMP / UDP / RTP); the PS4 FFmpeg build has no TLS for playback, so
  **HTTPS stream URLs - including HTTPS entries in M3U playlists - cannot be played**. The Xtream API, playlist
  downloads, images and downloads use libcurl and work over HTTPS.
- Video is decoded in software; very high bitrates or 4K may not play smoothly.
- **Scripts**: the interface font (Inter) covers Latin, Greek and Cyrillic; DejaVu Sans adds Arabic, Hebrew,
  Armenian and Georgian, and Droid Sans Fallback adds Japanese (kana), Korean (Hangul) and Chinese / Japanese
  ideographs. Thai, Devanagari and other Indic scripts, emoji and rare CJK characters are not covered and show a
  box. **Arabic and Hebrew** are reordered right-to-left and Arabic letters are joined (FriBidi), but this is
  basic shaping, not a full OpenType shaping engine, and the interface itself stays left-to-right.
- **Subtitles** are drawn by libass with the interface font only: subtitles in scripts other than Latin, Greek and
  Cyrillic cannot be drawn. Language names of such tracks are shown in the interface language.

## License

PS4 IPTV is free software under the **GNU General Public License v3.0 or later** - see [LICENSE](LICENSE).
It builds on pPlay and libcross2d (GPL-3.0, Cpasjuste), mpv, FFmpeg, SDL2, libass, FreeType, FriBidi, libcurl,
Mbed TLS, stb_image, the OpenOrbis PS4 Toolchain and PacBrew's PS4 ports, and the Inter, DejaVu Sans and Droid Sans Fallback typefaces. See
[NOTICE](NOTICE) for every component, its authors and license.
