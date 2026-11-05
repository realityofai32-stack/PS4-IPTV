# V7 IPTV - next features: HTTPS playback, physical keyboard, Stalker / MAG, Local Media

Branch `feature/v7-next-features` (from v0.3.0 + V7 UI v3 polish). Hardware checklist:
`docs/checkpoint-v7-next-features.txt`.

## 1. HTTPS media playback

- **Cause**: PacBrew's FFmpeg 5.0 package was configured with `--enable-protocol=file,http,ftp,tcp,udp,rtmp`: no
  `tls` / `https` protocol, so mpv could not open any `https://` stream (playback.cpp refused them on purpose).
- **Fix**: `scripts/build-ffmpeg-tls.sh` rebuilds FFmpeg 5.0 from the identical PacBrew recipe with
  `https,tls` added, `--enable-mbedtls` (the sysroot's Mbed TLS 2.16.6 that libcurl already uses, AES-NI enabled) and
  `--enable-version3`. Same version and configuration otherwise: identical object lists apart from `tls.o` /
  `tls_mbedtls.o`, identical exported codec / filter symbols, x86 SIMD kept (NASM). CMakeLists.txt links these
  libraries (`toolchain/ffmpeg-5.0-tls`).
- **libmpv is not rebuilt**: mpv 0.34.1 already turns `tls-verify`, `tls-ca-file` and `user-agent` into FFmpeg
  options for direct streams, and `demuxer-lavf-propagate-opts` (default on) passes them to every nested open of
  the HLS demuxer (variant playlists, segments, keys). FFmpeg's HTTP layer reuses them for redirects
  (http -> https included).
- **Certificates**: `Playback::init` sets `tls-verify=yes` and `tls-ca-file=/app0/assets/cacert.pem` (the CA
  bundle libcurl uses). Verification is never switched off; without a readable bundle HTTPS fails with a
  certificate error.
- **Acceptance**: `player/protocols` (host-tested). `https://` is accepted only when FFmpeg reports both `https` and
  `tls` at start-up (`avio_enum_protocols`); HTTP and the other protocols are accepted exactly as before.
- **Diagnostics**: start-up line `FFmpeg protocols: ...; HTTPS playback available (certificate verification on, CA
  bundle set)`; per open `open https stream at https://host[:port] (HLS)` (origin only); FFmpeg / mbedTLS error lines
  are classified (`TLS: certificate check failed ...` / `TLS: secure connection failed during the handshake`) with
  their own user messages. Every mpv/FFmpeg log line still goes through the redactor.
- The HTTPS warnings of the Xtream sign-in, Home and the playlist editor now appear only when the backend has no TLS.

## 2. Physical keyboard

- `platform/keyboard` loads libSceKeyboard at run time: `sceSysmoduleLoadModule(ORBIS_SYSMODULE_KEYBOARD)` (internal
  id as fallback), finds the module by name, resolves `sceKeyboardInit/Open/Close/ReadState` with `sceKernelDlsym`.
  No new NEEDED module (release check 8 unchanged); without the module the app runs with the controller only. The
  initial user's keyboard is opened, re-tried every 3 s (hot-plug), closed after repeated read errors (unplug). The
  state is read into an oversized zeroed buffer (the OpenOrbis structure is reverse-engineered).
- `platform/keyboard_logic` (host-tested) turns HID snapshots into events: presses, repeat (0.5 s, 50 ms; Enter /
  Escape never repeat), shift, Caps Lock (reported or tracked), AltGr / Ctrl+Alt, shortcuts ignored. Layouts: US and
  Turkish Q (ı İ ş ğ ü ö ç, URL characters, AltGr @ # $ { [ ] } \ |), UTF-8. *Settings > Language > Keyboard
  layout*: Automatic (Turkish Q with a Turkish UI) / US / Turkish Q.
- `Screen::handleKey`: the keyboard modal (every text field: source name, server / portal URL, username,
  password, M3U URL, User-Agent, MAC) types, Backspace, Delete (clear), Enter = OK, Escape = Cancel; Search types
  into the query, Enter shows / opens results; in the source editors typing on a focused field opens its editor
  with the typed character. Keys a screen does not take become controller buttons (`App::onKey`): arrows, Enter =
  Cross, Escape / Backspace = Circle, Page Up / Down = L2 / R2. The DualShock 4 path is unchanged.
- No key, character or typed text is ever logged (only "first key press received").

## 3. Stalker / MAG portals

- Model: `SourceType::Stalker`, `Profile::mac`, `server` = portal address. profiles.json stays schema 2 (additive
  `"sourceType": "stalker"` + `"mac"`): Xtream / M3U entries are written byte-identically, older builds skip the
  new entries. Incomplete entries are dropped on load.
- Protocol (`iptv/stalker`, host-tested): portal address -> API candidates (`portal.php`,
  `stalker_portal/server/load.php`), MAG 250 identity (User-Agent, X-User-Agent, MAC cookie, serial / device ids /
  signature derived from the MAC), handshake token, get_profile (allowed / blocked / login required), account info
  (expiry), genres / categories, channels, paged VOD / series lists, series-module seasons and episodes, old-style
  VOD series (`is_series`, `series: [1..n]`), create_link and command parsing, relative images. Mapped into the
  shared models (Category, LiveChannel + `cmd`, Movie, Series, MovieInfo, SeriesInfo, Episode + `cmd` / series no.).
- Service (`app/stalker_service`): one session per profile, Bearer token, one re-handshake on an unauthorized answer;
  Live TV via `get_all_channels` (paged fallback); Movies / Series read category by category (max 1,500 pages per
  section, cancellable, progress shown in the browser), saved under `cache/<profile>/stalker_*` and refreshed in the
  background after 24 h; details from the list entries; play addresses by create_link per open (fallback to a
  command's own URL), MAG User-Agent for the stream.
- Routing: `app/source_service` sends sign-in, Live TV, catalogs and details to the Xtream or Stalker service; the
  connect flow, VOD library, browsers, search, favorites, Continue Watching and both players are shared. The players
  call create_link through `Playback::prepare` / `failBeforeOpen` so the recovery logic works unchanged.
- No downloads for portal sources (single-use links): the download actions are hidden. Not supported: portals that
  require login / password for the box, EPG, catch-up / archive.
- MAC (every spelling), token, serial, device ids, signature and play tokens (`mac=`, `play_token=`, `sn=`...) are
  registered with the redactor; request URLs are never logged (only origins / API paths).

## 4. Local Media

- `media/local_media` (host-tested): roots `/data/` and `/mnt/usb0..7/`, offered when they open now; normalized
  absolute paths, `..` refused, everything must stay inside its root, symbolic links never followed, hidden files
  skipped; folders first, natural order. Playable extensions come from FFmpeg's demuxer table
  (`av_demuxer_iterate`) minus subtitle / image / text / playlist demuxers, plus the usual names of probed formats
  (TS / M2TS, MPG / VOB, WMV, WAV, RM, Ogg family) when their demuxer is linked.
- `screens/local_media_screen`: storage list -> folders -> files; Cross opens / plays, Circle goes up, Triangle
  re-reads. Entry points: Home navigation (*Local Media*), the first screen (no source needed) and the Sources
  options menu.
- Playback through the VOD player (`VodItem::localMedia`): audio tracks, embedded subtitles (same-name external
  `.srt` files are loaded by mpv's default `sub-auto=exact`: to be confirmed on hardware), subtitle style, aspect / crop / zoom, Options panel. No progress, history, Continue Watching, favorites or search.

## Profile storage decision (reviewed 2026-11-06)

Schema 2 stays, extended additively, because it is the safer choice for downgrades:

- **Schema 3** would make v0.3.0 reject `profiles.json` *and* its `.bak` ("unsupported format version"): the old app
  would start with no source at all and its next save would overwrite every Xtream / M3U source.
- **Additive schema 2**: v0.3.0 keeps every Xtream / M3U source and skips the Stalker entries (v0.3.0's own unit test
  `m3u_profile_schema_migration` reads a `"stalker"` entry and skips it). Xtream / M3U entries are written exactly
  as before.
- The remaining gap (v0.3.0 saves the file at every sign-in and drops the Stalker entries) is closed by
  `profiles.stalker.json` + the `stalkerSidecar` marker: a file without the marker was written by an older build and
  the Stalker sources come back from the sidecar (an id the older build reused gets a new one). A file written by
  this build is authoritative (a deleted portal never returns). Tested: `stalker_profiles_survive_a_downgrade_round_trip`.

## Validation round (2026-11-06)

Bugs found and fixed:

1. Redaction: a URL nested percent-encoded in another URL (redirectors, proxy links, `?to=https%3A...%3Fpassword%3D`)
   kept its credentials in the log (found by the HTTPS integration test). Nested URLs are now registered after
   decoding, secrets also in their percent-encoded spellings, and encoded query parameters are masked structurally.
2. Keyboard: two characters typed in the same frame on a focused source field opened two keyboard modals (the second
   key still went to the form). Keys after one that changes the screen now wait for the new screen.
3. Local Media: the 5,000-entry cap counted every directory entry before filtering, so a folder with thousands of
   pictures / subtitles could hide its videos. Only shown entries count now, and a truncated folder is flagged.
4. Stalker parsing used `atoi` / `atof` on portal text (undefined for out-of-range values): clamped `strtol` / `strtod`.
5. Source editor: caption array and field count came from two members (clang-analyzer); one source now.
6. JSON parser: `cp` initialized (GCC `-O1` maybe-uninitialized, a false positive, now warning-free).
7. JSON parser (found by fuzzing): a number outside int64 (`3.3e32`, `1e400`) was converted with a plain cast in
   `Value::asInt` - undefined behaviour, reachable from any provider or portal answer. Clamped now.
8. Stalker parsing (found while fixing 7): portal numbers feeding arithmetic (page sizes / totals, durations x 60,
   episode numbers) are clamped, and one entry's episode list is capped at 5,000.

Made testable without behaviour change: the keyboard state buffer parser and the connect / read / disconnect state
machine (`kbd::parseState`, `kbd::Session`; the PS4 file only resolves the library and `static_assert`s the
OpenOrbis structure offsets), text-field key handling (`KeyboardModel::applyKey`), keyboard-to-controller routing
(`kbd::toPad`), and storage root detection (`localmedia::detectRoots(base)`).

Test suites (`tests/run-feature-tests.sh` runs them all):

| Suite | What runs |
|---|---|
| `scripts/test-host.sh` / `test-host-sanitize.sh` | all host unit tests, plain and with ASan + UBSan + leak detection |
| `tests/https/run-https-tests.sh [--public]` | FFmpeg 5.0 + Mbed TLS 2.16.6 host build opened like libmpv 0.34.1 against local HTTP / HTTPS servers (valid, wrong host, untrusted, expired, TLS 1.3-only), HLS, redirects, User-Agent, log redaction; optional public HTTPS streams with the bundled CA list |
| `tests/https/inspect_ps4_binary.py` | the PS4 ELF's run-time protocol table (https, tls), tls_open -> Mbed TLS calls, and the same code in the PKG's eboot.bin |
| `tests/stalker/run-stalker-it.sh` | the real Stalker client against mock portals (Ministra / classic layouts, token expiry, login / blocked / malformed / broken / unreachable portals, create_link, saved copies and refresh, next episode, Xtream routing, log secrecy) |
| `tests/media/run-media-tests.sh` | Local Media files (MKV / MP4 / TS / M2TS / WebM / AVI / MOV / audio) through the app's format list and path check and FFmpeg 5.0 open / decode / seek; reference mpv run for the external .srt |
| `tests/fuzz/run-fuzz.sh [s]` | libFuzzer (UBSan) on Stalker / JSON parsing, redaction, the keyboard buffer / decoder, paths / protocols; corpus replayed under ASan |
