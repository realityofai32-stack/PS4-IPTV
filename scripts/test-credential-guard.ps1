<#
.SYNOPSIS
    Self-test of cmake/scan_credentials.cmake: it must pass on clean input and fail on planted secrets.
    Planted secrets are read from config/test_streams.txt and written only into a temporary folder that is
    deleted afterwards; nothing secret is printed.
#>
$ErrorActionPreference = 'Continue'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$cmake = Join-Path $RepoRoot 'toolchain\cmake\bin\cmake.exe'
$script = Join-Path $RepoRoot 'cmake\scan_credentials.cmake'
$cfg = Join-Path $RepoRoot 'config\test_streams.txt'
$sources = "$cfg|$(Join-Path $RepoRoot 'config\forbidden_strings.txt')"
$tmp = Join-Path $RepoRoot ('build\guard-test-' + [IO.Path]::GetRandomFileName())
$fail = 0

function Invoke-Guard([string] $dir, [string] $files) {
    & $cmake "-DSCAN_DIR=$dir" "-DSCAN_FILES=$files" "-DSECRET_SOURCES=$sources" -P $script *>&1 | Out-Null
    return $LASTEXITCODE
}
function Expect([string] $name, [bool] $shouldPass, [int] $code) {
    $ok = ($code -eq 0) -eq $shouldPass
    if (-not $ok) { $script:fail++ }
    '{0}  {1} (exit {2}, expected {3})' -f ($(if ($ok) { 'PASS' } else { 'FAIL' })), $name, $code,
        ($(if ($shouldPass) { 'success' } else { 'failure' }))
}

try {
    if (-not (Test-Path $cfg)) { throw 'config/test_streams.txt is required for this self-test' }
    $line = Get-Content $cfg | Where-Object { $_ -match '/live/[^/]+/[^/]+/' } | Select-Object -First 1
    if (-not ($line -match '/live/([^/]+)/([^/]+)/')) { throw 'no Xtream URL in config/test_streams.txt' }
    $password = $Matches[2]

    New-Item -ItemType Directory -Force "$tmp\clean", "$tmp\name", "$tmp\content" | Out-Null
    Set-Content "$tmp\clean\readme.txt" 'nothing to see' -NoNewline
    Expect 'clean folder passes' $true (Invoke-Guard "$tmp\clean" '')

    Set-Content "$tmp\name\test_streams.txt" 'TS=<placeholder>' -NoNewline
    Expect 'file named test_streams.txt is rejected' $false (Invoke-Guard "$tmp\name" '')

    [IO.File]::WriteAllBytes("$tmp\content\blob.bin", [byte[]](0, 1, 2) + [Text.Encoding]::UTF8.GetBytes("x$password") + [byte[]](0))
    Expect 'binary containing the password is rejected' $false (Invoke-Guard "$tmp\clean" "$tmp\content\blob.bin")

    $empty = Join-Path $tmp 'unparsable.txt'
    Set-Content $empty 'this file has no key=value secrets in a known form?' -NoNewline
    & $cmake "-DSCAN_DIR=$tmp\clean" "-DSCAN_FILES=" "-DSECRET_SOURCES=$empty" -P $script *>&1 | Out-Null
    Expect 'secret source without extractable secrets fails closed' $false $LASTEXITCODE
} finally {
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }
}
"credential guard self-test: $fail failure(s)"
exit $fail
