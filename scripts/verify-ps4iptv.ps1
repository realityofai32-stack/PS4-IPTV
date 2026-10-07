<#
.SYNOPSIS
    Release validation of the PS4 IPTV production PKG. Prints PASS/FAIL per check, exit code = failures.

.DESCRIPTION
    1  private config git-ignored and never committed
    2  clean rebuild: zero errors, zero warnings from our sources (src/)
    3  host unit tests (core, Xtream, storage, keyboard, redaction)
    4  credential guard self-test (must catch planted secrets)
    5  PkgTool pkg_validate
    6  PKG contents: exact allow-list of packaged files (no config, no credentials)
    7  extracted PKG re-scanned for private secrets
    8  linked system modules match the hardware-proven pPlay set (+ nothing unexpected)
#>
param([ValidateSet('Release', 'Debug')] [string] $BuildType = 'Release')

$ErrorActionPreference = 'Continue'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Tool = Join-Path $RepoRoot 'toolchain'
$Build = Join-Path $RepoRoot ("build\ps4iptv-" + $BuildType.ToLower())
$PkgTool = Join-Path $Tool 'host-bin\PkgTool.Core.exe'
$Llvm = Join-Path $Tool 'llvm-12.0.1\bin'
$cmake = Join-Path $Tool 'cmake\bin\cmake.exe'
$env:DOTNET_ROLL_FORWARD = 'Major'
$env:DOTNET_SYSTEM_GLOBALIZATION_INVARIANT = '1'
$results = [System.Collections.Generic.List[string]]::new()
function Report([string] $name, [bool] $ok, [string] $detail) {
    $line = '{0}  {1}  {2}' -f ($(if ($ok) { 'PASS' } else { 'FAIL' })), $name, $detail
    $results.Add($line)
    Write-Host $line
}

Push-Location $RepoRoot
$x = $null
try {
    # 1
    $ignored = $true
    foreach ($f in 'config/test_streams.txt', 'config/forbidden_strings.txt') {
        git check-ignore -q $f
        $ignored = $ignored -and ($LASTEXITCODE -eq 0)
    }
    $tracked = [string](git ls-files config/test_streams.txt config/forbidden_strings.txt)
    $history = [string](git log --all --oneline -- config/test_streams.txt config/forbidden_strings.txt)
    Report '1 private config ignored, never committed' ($ignored -and -not $tracked -and -not $history) `
        "ignored=$ignored tracked=$([bool]$tracked) in-history=$([bool]$history)"

    # 2
    & (Join-Path $PSScriptRoot 'build-ps4iptv.ps1') -BuildType $BuildType -Clean *>&1 | Out-Null
    $logFile = Join-Path $RepoRoot "build\ps4iptv-verify-$($BuildType.ToLower()).log"
    $env:OPENORBIS = "$Tool\pacbrew\opt\pacbrew\ps4\openorbis" -replace '\\', '/'
    & $cmake --build $Build --target clean *>&1 | Out-Null
    & $cmake --build $Build --verbose *>&1 | Out-File -Encoding utf8 $logFile
    $buildOk = $LASTEXITCODE -eq 0
    $log = Get-Content $logFile
    $errors = @($log | Select-String -CaseSensitive -Pattern ': error:|FAILED:|undefined symbol|CREDENTIAL GUARD')
    $warn = @($log | Select-String -Pattern '^(.+?):\d+:\d+: warning:')
    $ours = @($warn | Where-Object { $_.Line -match '/src/|\\src\\' -and $_.Line -notmatch 'pplay-reference' })
    Report '2 clean build' ($buildOk -and $errors.Count -eq 0 -and $ours.Count -eq 0) `
        "exit=$buildOk errors=$($errors.Count) warnings(ours)=$($ours.Count) warnings(upstream)=$($warn.Count - $ours.Count)"
    $ours | Select-Object -First 10 | ForEach-Object { Write-Host "      $($_.Line)" }
    $guardLine = $log | Select-String 'credential guard:' | Select-Object -Last 1
    if ($guardLine) { Write-Host "      build $($guardLine.Line.Trim())" }

    # 3
    $hostOut = & (Join-Path $PSScriptRoot 'run-host-tests.ps1') 2>&1
    $summary = @($hostOut | Select-String 'checks, ')
    $hostOk = $LASTEXITCODE -eq 0 -and $summary.Count -ge 2 -and -not ($summary | Where-Object { $_ -notmatch ' 0 failures' })
    Report '3 host unit tests' $hostOk (($summary | ForEach-Object { $_.Line }) -join ' | ')

    # 4
    $guard = & (Join-Path $PSScriptRoot 'test-credential-guard.ps1') 2>&1
    Report '4 credential guard self-test' ($LASTEXITCODE -eq 0) ([string]($guard | Select-Object -Last 1))

    # 5
    $pkg = Get-ChildItem $Build -Filter 'IV0001-IPTV00002_*.pkg' | Select-Object -First 1
    $v = & $PkgTool pkg_validate $pkg.FullName 2>&1
    $okCount = @($v | Select-String '^\[OK\]').Count
    $bad = @($v | Select-String '^\[(?!OK)')
    Report '5 PkgTool pkg_validate' ($okCount -gt 0 -and $bad.Count -eq 0) "$($pkg.Name): $okCount OK, $($bad.Count) not OK"

    # 6
    $x = Join-Path $RepoRoot ('build\verify-extract-' + [IO.Path]::GetRandomFileName())
    & $PkgTool pkg_extract --passcode 00000000000000000000000000000000 $pkg.FullName $x *>&1 | Out-Null
    $uroot = Join-Path $x 'uroot'
    $files = @(Get-ChildItem -Recurse -File $uroot | ForEach-Object { $_.FullName.Substring($uroot.Length + 1) -replace '\\', '/' } | Sort-Object)
    $allowed = @('assets/cacert.pem', 'assets/fonts/Inter-Regular.ttf', 'assets/fonts/Inter-SemiBold.ttf', 'eboot.bin',
                 'sce_module/libc.prx', 'sce_module/libSceFios2.prx', 'sce_sys/about/right.sprx', 'sce_sys/keystone')
    $unexpected = @($files | Where-Object { $_ -notin $allowed })
    $missing = @($allowed | Where-Object { $_ -notin $files })
    $entries = & $PkgTool pkg_listentries $pkg.FullName
    $sfoOk = [bool]($entries | Select-String 'PARAM_SFO') -and [bool]($entries | Select-String 'ICON0_PNG')
    Report '6 PKG contents allow-list' ($unexpected.Count -eq 0 -and $missing.Count -eq 0 -and $sfoOk) `
        ("files: " + ($files -join ', ') + $(if ($unexpected) { " | UNEXPECTED: " + ($unexpected -join ', ') } else { '' }) +
         $(if ($missing) { " | MISSING: " + ($missing -join ', ') } else { '' }))

    # 7
    $sources = "$RepoRoot\config\test_streams.txt|$RepoRoot\config\forbidden_strings.txt"
    $scan = & $cmake "-DSCAN_DIR=$uroot" "-DSCAN_FILES=" "-DSECRET_SOURCES=$sources" -P "$RepoRoot\cmake\scan_credentials.cmake" 2>&1
    Report '7 extracted PKG secret scan' ($LASTEXITCODE -eq 0) ([string]($scan | Select-Object -Last 1)).Trim()

    # 8
    $need = (& "$Llvm\llvm-objdump.exe" -p (Join-Path $Build 'ps4iptv') | Select-String 'NEEDED' | ForEach-Object { ($_ -split '\s+')[-1] } | Sort-Object)
    $proven = (& "$Llvm\llvm-objdump.exe" -p (Join-Path $RepoRoot 'build\pplay-ps4\pplay') | Select-String 'NEEDED' | ForEach-Object { ($_ -split '\s+')[-1] } | Sort-Object)
    $extra = @($need | Where-Object { $_ -notin $proven })
    Report '8 system modules within the pPlay-proven set' ($extra.Count -eq 0) ("NEEDED: " + ($need -join ' ') + $(if ($extra) { " | NOT IN pPlay: " + ($extra -join ' ') } else { '' }))
} finally {
    if ($x -and (Test-Path -LiteralPath $x)) { Remove-Item -LiteralPath $x -Recurse -Force }
    Pop-Location
}
Write-Host ''
$fail = @($results | Where-Object { $_.StartsWith('FAIL') }).Count
Write-Host ("SUMMARY: {0} checks, {1} failed" -f $results.Count, $fail)
exit $fail
