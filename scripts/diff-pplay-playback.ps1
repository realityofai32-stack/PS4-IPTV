<#
.SYNOPSIS
    Shows how the playback test's pPlay-derived files differ from upstream pPlay.

.DESCRIPTION
    tests\playback-test\src\pplay\mpv.{h,cpp} must differ from pPlay only by diagnostics (logging,
    error reporting); video_texture.{h,cpp} only by being decoupled from pPlay's Main/fade skin.
    Run this whenever those files change and review the output.
#>
$RepoRoot = Split-Path -Parent $PSScriptRoot
$up = Join-Path $RepoRoot 'external\pplay-reference\src\player'
$ours = Join-Path $RepoRoot 'tests\playback-test\src\pplay'
foreach ($f in 'mpv.h', 'mpv.cpp', 'video_texture.h', 'video_texture.cpp') {
    git --no-pager diff --no-index --ignore-cr-at-eol --stat -- "$up\$f" "$ours\$f"
    git --no-pager diff --no-index --ignore-cr-at-eol -- "$up\$f" "$ours\$f"
}
