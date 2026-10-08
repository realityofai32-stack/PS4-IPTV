# Contributing

Bug reports and pull requests are welcome. Please keep them free of private data.

## Never share credentials

Issues, pull requests, logs and screenshots are public. Before posting, remove:

- server addresses / hostnames of your IPTV service, usernames, passwords,
- playlist URLs, stream URLs (`/live/…/…/`, `/movie/…`, `/series/…`), tokens and signed URLs,
- account identifiers and anything else that identifies your provider account.

Use placeholders (`http://example.com:8080`, `USERNAME`, `PASSWORD`). Do not attach real M3U / M3U8 playlists;
if a playlist entry breaks the parser, reproduce it with example values. Credential leaks go to
[SECURITY.md](SECURITY.md), not to a public issue.

## Reporting a bug

A useful report contains:

- the PS4 IPTV version (*Settings › About* shows the version and build ID) and your firmware / homebrew
  environment,
- the source type (Xtream Codes or M3U / M3U8) and the stream format if known (MPEG-TS, HLS, MKV, ...),
- exact steps to reproduce, what you expected and what happened,
- a sanitized log or screenshot if available.

## Building and testing

Development happens **natively on Windows 11 with PowerShell**; no WSL, VM or Docker. The README's
[Building from source](README.md#building-from-source) section has the setup. Before opening a pull request:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run-host-tests.ps1
powershell -ExecutionPolicy Bypass -File scripts\verify-ps4iptv.ps1
```

`verify-ps4iptv.ps1` needs `config/forbidden_strings.txt` (git-ignored) with your own private values so that its
secret scans have something to compare against.

## Guidelines

- Match the style of the surrounding code; keep changes focused.
- User-visible text goes through the localization tables (`src/i18n/`), in English and Turkish. The host tests
  reject text that bypasses them.
- PS4 system interfaces must come from verified sources (pPlay, libcross2d, the OpenOrbis / PacBrew headers),
  never guessed prototypes or struct layouts.
- Changes that affect playback, input or rendering need testing on a real PS4; say in the pull request what you
  tested and on which firmware.
- By contributing you agree that your contribution is licensed under the GPL-3.0-or-later, like the project.
