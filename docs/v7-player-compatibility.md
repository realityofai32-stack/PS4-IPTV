# V7 Player v0.5.0: compatibility matrix

V7 Player v0.5.0 (`APP_VER` 00.50). Title ID `IPTV00002`, the content ID and `/data/PS4IPTV/` are unchanged. The
release is the hardware-approved development checkpoint `checkpoint-v7-player-r4` with only version metadata and
documentation changed.

Host tests are not hardware support. The matrix keeps the two apart.

| Status | Meaning |
|---|---|
| **HW** | tested on real PS4 hardware: the developer's console, system software 13.02, GoldHEN (v0.4.0 release, the development checkpoints and the final round `checkpoint-v7-player-r4`) |
| **HOST** | host-tested (unit / integration / failure-injection tests) and code-reviewed; not verified on hardware |
| **UNVERIFIED** | neither; expected behaviour only |
| **KNOWN ISSUE** | reported, not resolved |

## Platform and startup

| Area | Status | Notes |
|---|---|---|
| System software 13.02 (GoldHEN) | HW | the only firmware tested |
| System software 13.52 (GoldHEN 2.4b18.13 via GamerHack) | KNOWN ISSUE | v0.4.0 reported to close before its first screen; cause unknown. Needs `startup.txt`, `log.txt`, `log.prev.txt` from an affected console. No firmware-specific patch was made. |
| Other firmwares | UNVERIFIED | no claim of universal compatibility |
| Renderer (SDL2 / Piglet OpenGL ES) cannot start | HOST (code review) | system notification + exit; the loading screen needs the renderer, so it never hides this |
| Optional module missing (keyboard library) | HOST | controller only; module list read into a fixed buffer, any reported count is clamped; crash guard skips a stage that crashed |
| Network module load failure | HOST (code review) | logged; requests then fail with messages; "Continue offline" |
| Audio output cannot start | HOST (code review) | mpv's "audio device" error becomes the player's message |
| Unexpected firmware version values | HOST | formatted or reported as "unknown" |
| `startup.txt` missing / damaged / read-only | HOST | ignored or crash guard off; never blocks startup |
| Log folder not writable | HOST | startup continues, lines stay in memory (Diagnostics), the folder in the way is never replaced |
| Loading screen, start image `pic1.png`, icon `icon0.png`, title "V7 Player" | HW + HOST | saved accent from the first in-app frame; V7 Blue without one; no added startup delay |

## User data

| Area | Status | Notes |
|---|---|---|
| Data written by v0.4.0 | HOST | real v0.4.0 files (written by v0.4.0's own store code): profiles of all types, Stalker side file, settings, favorites, history, download list - all read and saved again |
| Data written by this build | HOST | every source type, playlist order and visibility, accent |
| Missing / empty / truncated / malformed / binary / newer-version / oversized files | HOST | defaults or the previous copy; the unreadable file is **kept** as `<file>.damaged` (`-2`, `-3`) and survives later saves |
| A folder where a data file belongs | HOST | defaults; saving fails cleanly; the folder is never moved or removed |
| Profiles unreadable | HOST | Stalker, Jellyfin / Emby and Stremio sources come back from their side files; nothing signs in by itself |
| Download list unreadable | HOST | partial downloads are kept (no orphan clean-up unless the list was read completely) |

## Storage

| Area | Status | Notes |
|---|---|---|
| `/data`, USB drives at `/usb0` … `/usb7` | HW | |
| `/mnt/usbN` fallback, empty mount points | HOST (empty mount point = no drive: HW) | |
| Missing / empty / denied drives, symlinks, path traversal, special files | HOST | never leaves its root |
| Read-only media | HOST (code review) | Local Media only reads; a playlist export to a read-only drive reports a failure |
| Data / optical disc | HOST (probe only) / UNVERIFIED | the console probably does not mount data discs for applications; AACS discs are refused |

## Network (every source type)

| Failure | Status |
|---|---|
| DNS failure, connection refused, reset, truncated answer | HOST |
| Stalled answer (bounded by the stall and total time limits) | HOST |
| Cancellation (the transfer stops at once, no callback) | HOST |
| Redirect loop, HTTP 401 / 403 / 404 / 500 / 503 (temporary errors tried twice) | HOST |
| Malformed, empty, HTML, binary and oversized (80 MiB) answers | HOST |
| Offline start with saved lists (Xtream live list, M3U playlist, media server and add-on catalogs) | HOST |

Covered for Xtream, M3U, XMLTV, Stalker / MAG, Jellyfin / Emby and Stremio by `tests/host/failure_it.cpp`: 84 failing
calls, each ends within 6 s with a message that carries no address or credential.

## Sources and features

| Area | Status | Notes |
|---|---|---|
| Xtream Codes | HW + HOST | |
| M3U / M3U8 | HW + HOST | chunked answers HOST |
| Stalker / MAG | HW + HOST (mock portals) | experimental; real portals differ |
| Live TV in-player navigation (Left → channels, Left → groups, Right → guide, zapping) | HW | approved 2026-11-17 |
| My Playlists as Live TV groups, order, visibility | HW | r2 / r3 |
| Xtream / Stalker short EPG | HW + HOST | |
| XMLTV guides | HW + HOST | host: gzip, chunked, time zones, expired data (nothing on air, nothing invented), missing / duplicate ids, a 35 MB guide with bounded memory (+48 KiB resident, 8 KB saved for 3 channels) |
| Jellyfin / Emby | HOST (mock server) | sign-in, wrong password, expired token, empty server, malformed items, missing artwork, resume, progress reports, server gone while playback is prepared. Real servers: UNVERIFIED |
| Stremio add-ons | HOST (mock add-on) | invalid manifests, empty catalogs, failing metas / streams, torrent-only and YouTube-only streams refused with a message, the first direct stream of several, add-on gone mid-session. No add-on is bundled or recommended. Real add-ons: UNVERIFIED |
| YouTube addresses in M3U | HOST | refused with a clear message |
| Unsupported codec | HOST (code review) | mpv / FFmpeg's error becomes the player's message; real files: hardware |
| V7 on-screen keyboard, physical keyboard, Search, Continue Watching, downloads, subtitles / audio, Arabic / RTL subtitles, Home / Details | HW (earlier rounds) + HOST regression | the PS4 system keyboard stays removed |

## Logging and privacy

HOST: a source scan fails the build's tests when a log statement passes a password, username, MAC address, token,
playlist or add-on address, local path or raw item id; the failure test checks the whole log of 84 failing calls for
credentials. Local file paths and names never reach the log (player ids, mpv lines and error details are masked);
file-layer errors show only the root of a USB drive, a disc or the playlist folder (`/usb0/[...]`); addresses show as
`host/…/name`, local playlists as `[local playlist]`.

## Bugs found and fixed during the final compatibility pass

1. **Downloads:** an unreadable `downloads.json` made every partial download look like an orphan and deleted it; the
   empty list then replaced the file. Now the file is kept, partial files are only cleaned up after a complete read
   of the main list, and a list without items or from a newer version counts as unreadable.
2. **Profiles:** with `profiles.json` and its `.bak` unreadable, the next save deleted the Stalker and media server
   side files (those sources were lost) and the save after it lost the damaged file. Both kept now; side-file sources
   are restored.
3. **Settings, favorites, history:** an unreadable file was gone after two saves. Kept now.
4. **A folder named like a data file** was renamed to `.bak` by the next save. Now never touched.
5. **XMLTV:** an answer that was not XML and contained no `<` (JSON, an error text, 80 MiB of spaces) was downloaded
   at the guide's 1 MB/s speed cap until the size limit (minutes) instead of failing at once.
6. **Log privacy:** a local file's path and name could reach the log through the movie player's item id, mpv error
   details and file-layer error texts (USB folder and file names, playlist names).
7. **Tests:** the Stalker integration test no longer compiled after the roadmap's source service changes.
8. **Continue Watching (fixed in the current v0.5.0 package, hardware-tested):** a series episode exited near its end
   (Circle / Stop, pause, app exit, L1 / R1, a player error) at a position in its last 45 s that the player's 0.25 s
   position sample had not shown yet was stored as watched without its next episode, and the series left Continue
   Watching. Every exit now records through one path (`src/app/playback_progress.h`) that moves the series on to its
   next episode whenever a record completes an episode.

## Validation of the release

| Suite | Result |
|---|---|
| Host unit tests (`scripts/test-host.sh`, all) | 383 tests, 343 982 checks, 0 failures |
| ASan + UBSan: 22 test files (brand, data recovery, privacy, storage, startup, downloads, XMLTV, EPG, media servers, add-ons, playlists, Live TV groups and navigation, local media, theme, subtitles, tracks, HTTPS, playback, M3U, Xtream, Stalker) | 169 tests, 290 632 checks, 0 failures |
| Integration (`scripts/test-download-integration.sh`: downloads, M3U, XMLTV, media server and add-ons, network failures of every source type) | 340 checks, 0 failures |
| Stalker / MAG mock portals (`tests/stalker/run-stalker-it.sh`) | 168 checks, 0 failures |
| Fuzzing (libFuzzer + UBSan, corpus replayed under ASan + UBSan): 45 s per target during the pass, 20 s per target before the release | 6 targets, ~870 000 runs, no finding |
| APP_VER derivation, credential guard self-test | 0 failures |
| PS4 Release build (clean) | 0 warnings, 0 errors; 10 system modules as before; `pkg_validate` all OK |

## Remaining hardware-only unknowns

- Startup on 13.52 and every firmware other than 13.02.
- The package images on the console (home-screen icon, start image) and the hand-over from the start image to the
  in-app loading screen; the loading screen's motion on a TV. Startup work happens between loading-screen frames: a
  long single step (the playback backend) holds the current frame.
- Real Jellyfin / Emby servers, real Stremio add-ons, real XMLTV feeds at full size on the console.
- Data / optical discs.
- Audio output failures and unsupported codecs with real files.
