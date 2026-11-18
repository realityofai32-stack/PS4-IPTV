# In-player Live TV navigation

While a Live TV channel plays, the channel list, the group list and the guide open over the picture. The video
goes on behind them. `src/app/live_nav.*` decides what is shown and what a button does. It is host-tested.
`src/screens/player_screen.cpp` draws the panels and plays what the navigation decides.

## Controls

| Where | Button | What happens |
|---|---|---|
| Video | Left | Opens the channel list of the current group. The playing channel is selected. |
| Video | Right | Opens the guide of the playing channel. |
| Video | Up / Down, L1 / R1 | Zap through the current group, as before. |
| Channel list | Up / Down, L2 / R2 | Move one row, or one page. |
| Channel list | L1 / R1 | Show the previous / next group. |
| Channel list | Cross | Plays the channel without leaving the player and closes the list. Its group becomes the list that Up / Down zap through. On the playing channel, Cross just closes the list. |
| Channel list | Left | Opens the group list to the left. The channels move right. |
| Channel list | Circle / Right | Back to the video. |
| Group list | Up / Down | The channel column follows the highlighted group. |
| Group list | Cross / Right | Opens the highlighted group's channels. |
| Group list | Circle | Back to the channels of the group shown before. |
| Guide | Up / Down | Zap; the guide follows the channel. |
| Guide | Left | Switches to the channel list. |
| Guide | Circle / Right / Cross | Back to the video. |

- Held buttons never open, close or play anything.
- Square, Triangle and Options do nothing while a list is open.
- A list closes by itself after 12 s without input; the guide after 20 s.
- The channel line (HUD) stays hidden while a panel is open. It shows briefly when the picture is back.

## Groups

The groups are the same as on the Live TV screen: Favorites, All channels, then the source's categories. The list
opens on the group the player was started from:

- **Live TV screen:** the category, Favorites or All channels that was open.
- **Favorites screen and Home channel cards:** Favorites.
- **Search:** "Search results", listed first.
- **One channel from the history:** that channel's own category, or All channels if the category is gone.

## Guide data: what is fetched and what is never shown

The app had no programme data. Channels carry only an EPG id (`epg_channel_id`, `tvg-id`, `xmltv_id`). The guide
asks the source only when it is shown. The request is for one channel, the one playing, after zapping has settled.
It asks for the short guide: the programme on air and the next three.

| Source | Request | When none |
|---|---|---|
| Xtream | `player_api.php … action=get_short_epg&stream_id=<id>&limit=4` | The channel has no `epg_channel_id`: no request. |
| Stalker / MAG | `portal.php?type=itv&action=get_short_epg&ch_id=<id>&size=4` (the session's token) | — |
| M3U playlist | the XMLTV guide the playlist header names (`url-tvg` / `x-tvg-url` / `tvg-url`), downloaded once into a compact saved guide, then read from it per channel (below) | the playlist names no guide, or the guide has nothing for the channel |

- **Session cache** (`epg::Cache`, 48 channels at most): an answer is asked again when its programme on air ends,
  and at most after 30 min. "No program information" is asked again after 15 min. A failed request is retried
  after 1 min. A request without an answer counts as lost after 45 s. Nothing is fetched while zapping, nothing in
  the background, and nothing for a channel nobody looks at.
- **Nothing made up:** entries without valid `start_timestamp` / `stop_timestamp` values, without a title, longer
  than a day or with a placeholder title ("N/A", "-") are dropped. The panel's local date strings are never used,
  because their time zone is unknown.
  - Same-slot duplicates are kept once.
  - A programme that runs into the next one ends where the provider's next one starts.
  - Titles and descriptions are one clean line, capped at 160 / 700 bytes.
  - Xtream's base64 texts are decoded only when they decode to valid UTF-8 text, so plain-text panels stay intact.
- **What is shown:** an answer with nothing left, or only programmes that have ended, shows "No program
  information". A failed request shows "Program information could not be loaded". Times are local (the console's
  time-zone setting, like the HUD clock).

### Playlist (M3U) guides: XMLTV

- **Found in the playlist header:** `#EXTM3U url-tvg="…"` (also `x-tvg-url`, `tvg-url`; several addresses separated
  by commas, at most 3; http / https only). Playlist Info shows whether the playlist names one.
- **Downloaded once, when a guide panel first needs it** (`app/xmltv_service.*`). The feed is streamed (plain or a
  `.gz` file; `Content-Encoding` too) through a streaming XML reader; it is never held in memory and never written
  to disk. The download is capped at 1 MB/s, so the stream keeps its bandwidth; at most 160 MB as sent and 1 GB of
  XML.
- **Kept as a compact saved guide**, `/data/PS4IPTV/cache/<profile>/xmltv_guide.bin`:
  - only the guide channels the playlist matches;
  - only programmes still running or starting within the next 36 hours, at most 48 per channel;
  - while building, at most 200,000 programmes and 16 MB of text (descriptions are dropped first, titles are kept);
  - only its channel table stays in memory; the guide panel reads one channel's programmes from the file.
- **Refreshed** in the background after 12 hours (the old guide keeps answering meanwhile). It is not used once it
  was built from other addresses. After a failed download (HTTP error, not an XMLTV file, too large…) nothing is
  tried again for an hour, also across restarts. Offline, only the saved guide is used.
- **Matching:**
  1. The channel's `tvg-id` against the guide's channel ids: as written, then ignoring ASCII case.
  2. Otherwise the channel name against the guide's display names (case and Latin accents ignored), and only when
     exactly one guide channel has that name.
  No partial names, no guessing.
- **Nothing made up:**
  - A programme needs a readable start and a title.
  - A missing stop ends at the channel's next programme in the guide (at most 24 h later); otherwise the programme
    is dropped.
  - Times without a zone are UTC (the XMLTV rule); an unknown zone name drops the programme.
  - Same-slot duplicates and overlaps are handled as above.
  - Titles / descriptions in the UI language are preferred when the guide has several.

## Cost

- The lists are indices into the session's catalog; no provider data is copied.
- The Favorites list is built once per favourites change.
- Rows exist only for the visible lines.
- Logos are shown only when already in memory, so no image download competes with the stream.
- Picking a channel uses the same open path as a zap, with no debounce and no extra work.

## Tests

- `tests/host/test_epg.cpp`:
  - Xtream and Stalker answers (formats, empty answers, error pages);
  - base64 detection;
  - dropped, never-repaired entries;
  - duplicates, overlaps and caps;
  - text cleaning;
  - now / next and progress;
  - cache timing and its bound;
  - which sources are asked.
- `tests/host/test_live_nav.cpp`:
  - every transition above;
  - picking channels and the new zap list;
  - the groups for every opener;
  - L1 / R1, paging, favourites changes;
  - idle close;
  - held buttons.
- `tests/host/test_xmltv.cpp`: XMLTV times and zones, the streaming reader (pieces down to one byte, entities,
  CDATA, encodings, languages, HTML / JSON answers, bounded tokens), what the builder keeps, matching, the saved
  guide (round trip, damaged / replaced files), the playlist header's guide addresses.
- `tests/host/xmltv_it.cpp` (`scripts/test-download-integration.sh`, local test server): gzip files, plain XML,
  `Content-Encoding`, 404, HTML, cancellation, one download then the saved guide (also after a restart), no retry
  after a failure, changed addresses, the log free of addresses' paths and credentials; the gzip inflater.
- `tests/host/test_hints.cpp`: the player's hint sets (two new actions, `ChannelList` / `Guide`, with D-pad
  left / right glyphs).
