# V7 roadmap, November 2026: what was built, what was only investigated

Development checkpoint on the uncommitted `feature/v7-live-nav` working tree (base: v0.4.1 compatibility commit
`9b04792` plus the hardware-approved in-player Live TV navigation). Nothing is committed, tagged or published. The
approved Live TV build is kept, read-only, in `build/approved-v7-live-nav-hw/` (PKG, `working-tree.diff`, untracked
sources).

| # | Task | Result |
|---|---|---|
| 1 | M3U / XMLTV programme guide | implemented |
| 2 | User-created local playlists, shown as Live TV groups | implemented |
| 3 | PS4 system keyboard | removed after two failed hardware tests; V7's keyboard only |
| 4 | Jellyfin / Emby | implemented (connect, sign in, browse, play, resume / progress) |
| 5 | Stremio add-ons | basic version implemented (one add-on per source, direct http(s) streams only) |
| 6 | YouTube addresses in M3U | feasibility only: not suitable; a clear message instead of a failure |
| 7 | Blu-ray / optical disc | feasibility + limited probe-only support for readable data discs |

## 1. M3U / XMLTV programme guide

- **Where it comes from:** the playlist header, `#EXTM3U url-tvg="…"` / `x-tvg-url` / `tvg-url`; several
  addresses separated by commas, at most 3; http / https only. Playlist Info shows whether the playlist names one.
- **When it is fetched:** once, when the live player's guide panel first needs it (`app/xmltv_service.*`). The feed
  is streamed (plain XML, a `.gz` file, or `Content-Encoding`) through a streaming XML reader (`iptv/xmltv.*`):
  - never held in memory, never written to disk;
  - capped at 1 MB/s so the stream keeps its bandwidth;
  - at most 160 MB as sent and 1 GB of XML.
- **What is kept:** a compact saved guide (`/data/PS4IPTV/cache/<profile>/xmltv_guide.bin`) with only the channels
  the playlist matches and the next 36 hours, at most 48 programmes per channel. Only its channel table stays in
  memory; the panel reads one channel from the file.
- **Refresh and failure:** refreshed in the background after 12 h (the old guide answers meanwhile). After a failed
  download nothing is tried again for an hour, across restarts too. Offline: the saved guide only.
- **Matching:** `tvg-id` as written, then ignoring case; otherwise the channel name against exactly one guide display
  name.
- **Never invented:** programmes need a readable start and a title. A missing stop ends at the channel's next
  programme (at most 24 h later); an unknown time zone drops the programme.
- **Details:** `docs/v7-live-navigation.md`, section "Playlist (M3U) guides: XMLTV".

## 2. My Playlists, shown as Live TV groups

- **What a playlist is:** a normal M3U file in `/data/PS4IPTV/playlists/` (`media/user_playlists.*`,
  `screens/my_playlists_screen.cpp`). Playlists persist, import / export as plain M3U (USB), and can still be
  opened as a source of their own.
- **In Live TV (hardware round 2):** every playlist shown in Live TV is a group in the Live TV group list, after
  Favorites / All Channels and before the provider's categories, in the user's order (`app/user_groups.*`,
  `iptv::LiveCatalog::setUserGroups`). The in-player group list (Left, Left) has them too.
  - An entry added from Live TV keeps the source and channel it came from (`v7-source` / `v7-channel` M3U
    attributes, ignored by other players). In that source's Live TV it is that very channel: an index into the
    source's list, nothing copied. In a playlist source an entry with the same address is the source's channel too.
  - Any other entry (an address added by hand, a channel of another source) is one extra channel after the source's
    channels; one address is one extra channel, whichever playlists list it.
  - All Channels and search stay the source's own channels.
  - Opening a group shows its channels in the normal channel list; playing, zapping, Left / Right in the player,
    favorites and history work as for any channel.
- **Managing (My Playlists, from Live TV → OPTIONS or the Profiles menu):** the list is in the user's order (the
  groups' order). Square picks a playlist up, Up / Down carry it, Cross / Square put it down, Circle puts it back.
  OPTIONS: Show in Live TV / Hide from Live TV, Rename, Open as a source, Export to USB, Delete, New playlist, Import
  from USB. Inside a playlist: add an address, rename / remove / move entries. The order and the hidden playlists are
  kept in the settings (`playlistOrder`, `hiddenPlaylists`). Every change rebuilds the groups (after the live player
  closes, if one is open).
- **Limits:** at most 5,000 entries / 2 MB per playlist edited here; portal (Stalker) channels cannot be added (their
  addresses expire); Xtream channels are written with their live address, which contains the account credentials (an
  export warns about it).

## 3. PS4 system keyboard (removed)

- Two hardware tests failed the same way: the console's keyboard opened, the first key press closed it and no text
  came back. A fix of the dialog's parameter lifetime after the first test did not change that.
- The feature was removed completely: the Settings option, the OPTIONS entry and its hint (forms and Search), the
  runtime loading of the console's dialog library and its crash guard, and their tests and notes.
- Text entry is exactly as in v0.4.1: V7's own keyboard in every form and in Search, and a USB / Bluetooth keyboard
  types directly. A setting saved by an earlier development build is ignored and dropped at the next save.

## 4. Jellyfin / Emby

- **Implementation:** one implementation for both servers (same REST API): `iptv/mediabrowser.*`,
  `app/mediabrowser_service.*`. It is a new source type, `jellyfin`, added from **Add Source → Jellyfin / Emby**.
- **Sign-in:**
  - `GET /System/Info/Public` checks the server and follows a move to https.
  - Then `POST /Users/AuthenticateByName`; the password may be empty.
  - The access token lives in memory only and is registered as a log secret.
- **Browsing:** movie / TV libraries → Movies / Series categories, 500 items per page (at most 50,000 per kind),
  saved copies like the other sources. Details include overview, genres, people, runtime and media streams; episodes
  come from `/Shows/{id}/Episodes` (missing "virtual" episodes skipped).
- **Playback:** the file as it is (`/Videos/{id}/stream?static=true`), never transcoded, in the existing movie
  player. A codec the console cannot decode fails as for any file.
- **Resume / progress:**
  - Playing / Progress / Stopped reports, including pause.
  - A position the server has becomes the console's Resume when the console has none (a local position is never
    replaced).
- **Not included:** Live TV of the server, music / photos, downloads, transcoding, Quick Connect.
- **Data safety:** v0.4.1 and older skip unknown source types and would drop them on save. These sources are
  therefore also kept in `profiles.mediaservers.json` and restored after such a downgrade
  (`storage/profile_store.cpp`).

## 5. Stremio add-ons

- **Feasibility:** add-ons are plain HTTP(S) JSON services (manifest / catalog / meta / stream); no Stremio code,
  browser or runtime is needed. A basic version therefore fits: `iptv/stremio.*`, `app/stremio_service.*`, source
  type `stremio`, **Add Source → Stremio**.
- **Mapping:**
  - movie / series catalogs → Movies / Series categories (catalogs that need a search word or another required
    value are left out); "skip" paging, at most 5 pages per catalog;
  - meta → details and episodes;
  - stream → asked when a title plays; the first stream the player can open is used.
- **What can play:** only direct `http(s)` addresses. Torrents (`infoHash`), YouTube ids and "open in a browser"
  links cannot play here and are skipped, as are streams that need headers other than a User-Agent.
- **Configuration:** an add-on that requires it must be configured on its own web page; its address (which can
  carry an API key) is then entered here. It is never logged; long path parts are registered secrets.
- **Not included:** several add-ons combined in one source (Stremio's usual catalog add-on + stream add-on setup),
  choosing between streams, subtitles add-ons, progress sync.
- **Add-on choice:** V7 bundles and suggests no add-on. Most stream add-ons in circulation are torrent / debrid
  based and stay unplayable here by design.

## 6. YouTube addresses in M3U: not suitable

- **Why not:**
  - A YouTube page (`youtube.com/watch`, `youtu.be`, `/live`, `/shorts`) is not a stream. The streams are only
    reachable through YouTube's undocumented player API: per-client request formats, signature / "n" parameters that
    need YouTube's own JavaScript to be executed, and proof-of-origin tokens.
  - The YouTube Data API gives metadata only, by design.
  - mpv's own YouTube support needs `yt-dlp` (Python) at run time: not available on the console. Its maintainers
    now also need a JavaScript runtime for YouTube.
  - All of this is exactly the fragile scraping the task ruled out.
- **What changed:** the player's protocol gate (`player/protocols.*`) recognises YouTube page addresses and shows
  "This is a YouTube page, not a stream address…" at once, instead of failing inside FFmpeg.
- **Unchanged:** every other address, including direct `googlevideo.com` HLS streams.

## 7. Blu-ray / optical disc

- **Commercial Blu-ray movies (AACS / BD+):** out of scope. They play only in the console's own Blu-ray player, and
  nothing here reads or circumvents copy protection.
- **Data discs and unprotected authored discs:**
  - Whether an app can see a disc at all is not known yet. Inside the app sandbox `/mnt` does not exist (hardware
    finding, v0.4.1); USB drives are `/usbN`.
  - Local Media now probes the places a disc could appear: `/disc` (sandbox view), then `/mnt/disc`. The probe is
    read-only (stat + opendir) and separate from the approved USB detection, which is unchanged.
  - A readable disc is listed after the USB drives as "Disc" and browsed like a USB drive: media files only, no
    symbolic links, never outside it. The M2TS streams of an unprotected authored disc (`BDMV/STREAM`) play like any
    MPEG-TS file. There is no playlist / menu navigation (no libbluray).
  - A disc with an `AACS` folder at its top is recognised before anything else is read: "This is a protected Blu-ray
    movie…".
  - The probe result for both paths is in the log line "localmedia: … disc …"; the startup log lists the sandbox's
    "/" entries.
- **Expected outcome:** the console probably does not mount data discs for applications. In that case nothing
  appears and nothing else changes.

## Tests (focused)

| Area | Tests |
|---|---|
| XMLTV | `test_xmltv.cpp` (unit), `xmltv_it.cpp` (local server: gzip / plain / Content-Encoding, 404, HTML, cancel, saved guide, no retry, log without addresses) |
| My Playlists | `test_user_playlists.cpp` |
| Text entry (as v0.4.1) | `test_keyboard.cpp`, `test_hints.cpp` (both unchanged from v0.4.1) |
| Jellyfin / Emby | `test_mediabrowser.cpp` (unit, sidecar / downgrade), `mediaserver_it.cpp` (fake server: redirect, wrong password, paging, details, resume, reports, log without token) |
| Stremio | `test_stremio.cpp` (unit), `stremio_it.cpp` (fake add-on: configuration, paging, a catalog ignoring skip, streams) |
| YouTube | `test_https.cpp` (protocol gate) |
| Disc | `test_localmedia.cpp` (probe, empty mount points, AACS, USB candidates unchanged) |
| Shared | `test_i18n.cpp`, `test_epg.cpp`, `test_live_nav.cpp`, `test_m3u.cpp`, `test_storage.cpp`, `test_stalker.cpp`, `test_playback.cpp`, `test_input.cpp`, `test_core.cpp`, `m3u_it.cpp` |

`scripts/test-host.sh` and `scripts/run-host-tests.ps1` list the new unit tests. `scripts/test-download-integration.sh`
builds and runs the integration programs against `scripts/download-test-server.py`; that script now also serves an
XMLTV guide, a Jellyfin / Emby API and a Stremio add-on. No fuzzing and no full-suite run were done for this
checkpoint.
