# Security policy

## Reporting a problem privately

Please **do not open a public issue** for:

- a security vulnerability in PS4 IPTV,
- a way the app could reveal credentials, playlist addresses or authenticated stream URLs (in logs, on screen,
  in files it writes, in crash output),
- credentials or private data you found in this repository, its history or a release.

Use **GitHub private vulnerability reporting** instead: on the repository page open the **Security** tab and
choose **Report a vulnerability**. Only the maintainers can see the report.

When you describe the problem, **never include a real server address, username, password, playlist URL or
token**, not even your own. Replace them with placeholders such as `http://example.com:8080`, `USERNAME`,
`PASSWORD`. A screenshot or log excerpt is welcome once the private values are removed.

## Scope

PS4 IPTV is a client. It does not run servers and does not provide content, accounts or playlists. Problems with
an IPTV service itself (its availability, its content, its accounts) are outside this project.

## Supported versions

Only the latest release is supported. Fixes are published as a new release.
