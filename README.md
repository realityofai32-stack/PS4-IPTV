# PS4 IPTV

A native IPTV client for the PlayStation 4 (homebrew), built for the TV and the DualShock 4. It connects to
**your own Xtream Codes compatible service** and plays live channels, movies and series on the console, with
offline downloads for services that allow storing content.

> **PS4 IPTV does not provide any content.** It ships with no channels, movies, series, accounts or
> playlists, and it is not affiliated with any IPTV provider. You need a subscription to a service you are
> legitimately entitled to use. You are responsible for the services and content you access with it.

## Features

- **Xtream Codes profiles**: several providers / accounts, connection test, account status and expiry
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
- **DualShock 4 interface** made for the TV: hold-to-scroll, controller hints everywhere, on-screen keyboard
- **English and Türkçe** user interface (English by default; Settings › Language)

Playback uses the proven [pPlay](https://github.com/Cpasjuste/pplay) stack: libmpv 0.34.1 with the PacBrew PS4
patches, FFmpeg 5.0 (software decoding), SDL2 and libcross2d.

## Requirements

- A **jailbroken PS4** (or a compatible homebrew environment) that can install fake-signed PKGs
- Your own, legitimate **Xtream Codes compatible** service: server address, username and password
- Network access for streaming; downloaded content plays offline

## Installing

1. Download the `IV0001-IPTV00002_00-IPTV00002*.pkg` file from the [Releases](../../releases) page, or build it
   yourself (below).
2. Install it with your homebrew environment's package installer (for example from a USB stick).
3. Start **PS4 IPTV**, choose **Add Xtream Profile** and enter the details from your provider.

The app keeps its data in `/data/PS4IPTV/` on the console (profiles, settings, history, caches, downloads).
Profiles, including their passwords, are stored only on the console.

## Using the app

| Button | Action |
|---|---|
| ✕ | Select / open / play |
| ○ | Back |
| □ | Favorite; on an episode: download it; on a Continue Watching card: remove it from the row |
| △ | Search (Live TV, Movies, Series); in the player: technical info |
| OPTIONS | Settings (Home); sort & refresh (Movies, Series); playback options (player) |
| L1 / R1 | Previous / next category, season, tab or episode |
| L2 / R2 | Page up / down; in the player: seek 1 minute |
| D-pad ← → | Seek 10 seconds in the player |

Hold a direction to scroll quickly; the speed increases the longer it is held.

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
Both are git-ignored. The build fails if any of those values would end up in the PKG, and
`scripts/scan-git-history.py` checks every commit for them without printing them.

## Project layout

```
src/app/          application shell, services (Xtream, catalogs, progress, offline glue)
src/downloads/    download engine: manifest, resumable transfers (libcurl), queue, storage checks
src/i18n/         localization tables (English, Türkçe)
src/iptv/         Xtream API parsing, catalogs, search index, sorting
src/player/       playback adapter over pPlay's mpv wrapper, tracks, stability, video geometry
src/screens/      TV screens
src/ui/           UTF-8 text rendering, widgets, on-screen keyboard
tests/host/       host unit tests and the download integration test
scripts/          toolchain setup, build, tests, verification, staging
```

## Known limitations

- Streams are played over **HTTP**; the PS4 FFmpeg build has no TLS for playback. (The API, images and downloads
  use libcurl and work over HTTPS.)
- Video is decoded in software; very high bitrates or 4K may not play smoothly.
- The interface font covers Latin, Greek and Cyrillic scripts. Language names of other scripts are shown in the
  interface language, and subtitles in those scripts cannot be drawn.

## License

PS4 IPTV is free software under the **GNU General Public License v3.0 or later** - see [LICENSE](LICENSE).
It builds on pPlay and libcross2d (GPL-3.0, Cpasjuste), mpv, FFmpeg, SDL2, libass, FreeType, FriBidi, libcurl,
Mbed TLS, stb_image, the OpenOrbis PS4 Toolchain and PacBrew's PS4 ports, and the Inter typeface. See
[NOTICE](NOTICE) for every component, its authors and license.
