<#
.SYNOPSIS
    Pre-hardware validation of the PS4 IPTV playback test PKG. Prints PASS/FAIL per check.

.DESCRIPTION
    1  config/test_streams.txt is git-ignored and was never committed
    2  clean rebuild: zero errors, zero warnings in our sources (upstream warnings listed separately)
    3  PkgTool pkg_validate
    4  PKG contents: eboot, system modules, icon/sfo entries, packaged test_streams.txt == local file
    5  packaged config parsed by the app's own C++ parser: TS and HLS READY (sanitized output only)
    6  pPlay playback libraries linked (libmpv/FFmpeg/SDL2/libass symbols identical to the pPlay ELF)
    7  credential redaction unit tests
    The extracted PKG copy (contains credentials) is deleted at the end.
#>
$ErrorActionPreference = 'Continue'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Tool = Join-Path $RepoRoot 'toolchain'
$Build = Join-Path $RepoRoot 'build\playback-test'
$Pkg = Join-Path $Build 'IV0001-IPTT00001_00-IPTT000010100000.pkg'
$PkgTool = Join-Path $Tool 'host-bin\PkgTool.Core.exe'
$Llvm = Join-Path $Tool 'llvm-12.0.1\bin'
$env:DOTNET_ROLL_FORWARD = 'Major'
$env:DOTNET_SYSTEM_GLOBALIZATION_INVARIANT = '1'
$results = [System.Collections.Generic.List[string]]::new()
function Report([string] $name, [bool] $ok, [string] $detail) {
    $line = '{0}  {1}  {2}' -f ($(if ($ok) { 'PASS' } else { 'FAIL' })), $name, $detail
    $results.Add($line)
    Write-Host $line
}

Push-Location $RepoRoot
try {
    # 1 ------------------------------------------------------------------------------------------
    git check-ignore -q config/test_streams.txt
    $ignored = $LASTEXITCODE -eq 0
    $tracked = [string](git ls-files config/test_streams.txt)
    $history = [string](git log --all --oneline -- config/test_streams.txt)
    Report '1 config git-ignored, never committed' ($ignored -and -not $tracked -and -not $history) `
        ("ignored=$ignored tracked=" + [bool]$tracked + " in-history=" + [bool]$history)

    # 2 ------------------------------------------------------------------------------------------
    $logFile = Join-Path $RepoRoot 'build\playback-test-verify-build.log'
    & (Join-Path $PSScriptRoot 'build-playback-test.ps1') -Clean *>&1 | Out-Null
    $env:OPENORBIS = "$Tool\pacbrew\opt\pacbrew\ps4\openorbis" -replace '\\', '/'
    & "$Tool\cmake\bin\cmake.exe" --build $Build --target clean *>&1 | Out-Null
    & "$Tool\cmake\bin\cmake.exe" --build $Build --verbose *>&1 | Out-File -Encoding utf8 $logFile
    $buildOk = $LASTEXITCODE -eq 0
    $log = Get-Content $logFile
    $errors = @($log | Select-String -Pattern ': error:|FAILED:|undefined symbol')
    $warn = @($log | Select-String -Pattern '^(.+?):\d+:\d+: warning:')
    $ours = @($warn | Where-Object { $_.Line -match 'tests/playback-test/' })
    $upstream = @($warn | Where-Object { $_.Line -notmatch 'tests/playback-test/' })
    Report '2 clean build' ($buildOk -and $errors.Count -eq 0 -and $ours.Count -eq 0) `
        "exit=$buildOk errors=$($errors.Count) warnings(ours)=$($ours.Count) warnings(upstream)=$($upstream.Count)"
    $ours | Select-Object -First 10 | ForEach-Object { Write-Host "      $($_.Line)" }

    # 3 ------------------------------------------------------------------------------------------
    $v = & $PkgTool pkg_validate $Pkg 2>&1
    $okCount = @($v | Select-String '^\[OK\]').Count
    $bad = @($v | Select-String '^\[(?!OK)')
    Report '3 PkgTool pkg_validate' ($okCount -gt 0 -and $bad.Count -eq 0) "$okCount OK, $($bad.Count) not OK"

    # 4 ------------------------------------------------------------------------------------------
    $x = Join-Path $RepoRoot ('build\verify-extract-' + [IO.Path]::GetRandomFileName())
    & $PkgTool pkg_extract --passcode 00000000000000000000000000000000 $Pkg $x *>&1 | Out-Null
    $uroot = Join-Path $x 'uroot'
    $files = Get-ChildItem -Recurse -File $uroot | ForEach-Object { $_.FullName.Substring($uroot.Length + 1) -replace '\\', '/' }
    $expected = 'eboot.bin', 'sce_module/libc.prx', 'sce_module/libSceFios2.prx', 'sce_sys/about/right.sprx', 'test_streams.txt'
    $missing = @($expected | Where-Object { $_ -notin $files })
    $entries = & $PkgTool pkg_listentries $Pkg
    $hasIcon = [bool]($entries | Select-String 'ICON0_PNG')
    $hasSfo = [bool]($entries | Select-String 'PARAM_SFO')
    $packaged = Join-Path $uroot 'test_streams.txt'
    $same = (Test-Path $packaged) -and ((Get-FileHash $packaged).Hash -eq (Get-FileHash 'config\test_streams.txt').Hash)
    Report '4 PKG contents' ($missing.Count -eq 0 -and $hasIcon -and $hasSfo -and $same) `
        ("files: " + ($files -join ', ') + " | icon=$hasIcon sfo=$hasSfo | packaged config == config/test_streams.txt: $same")

    # 5 + 7 --------------------------------------------------------------------------------------
    $hostOut = & (Join-Path $PSScriptRoot 'run-host-tests.ps1') 2>&1 | Select-Object -Last 1
    Report '7 redaction/parser unit tests' ([string]$hostOut -match ' 0 failures') ([string]$hostOut)
    $check = & (Join-Path $RepoRoot 'build\host-tests\config_check.exe') $packaged
    $checkOk = $LASTEXITCODE -eq 0
    Report '5 packaged config READY (app parser)' $checkOk ''
    $check | ForEach-Object { Write-Host "      $_" }

    # 6 ------------------------------------------------------------------------------------------
    $elf = Join-Path $Build 'playback_test'
    $pplayElf = Join-Path $RepoRoot 'build\pplay-ps4\pplay'
    $st = & "$Llvm\llvm-nm.exe" --defined-only $elf | ForEach-Object { ($_ -split '\s+')[-1] }
    $sp = & "$Llvm\llvm-nm.exe" --defined-only $pplayElf | ForEach-Object { ($_ -split '\s+')[-1] }
    $hp = [Collections.Generic.HashSet[string]]::new([string[]]$sp)
    $fam = '^(mpv_|ps4_|ao_|vo_|ra_|gl_|mp_|ass_)'
    $mpvT = @($st | Where-Object { $_ -match $fam }); $mpvP = @($sp | Where-Object { $_ -match $fam })
    $mpvCommon = @($mpvT | Where-Object { $hp.Contains($_) })
    $avT = @($st | Where-Object { $_ -match '^(av|ff_|sws_|swr_)' }); $avCommon = @($avT | Where-Object { $hp.Contains($_) })
    $sdlT = @($st | Where-Object { $_ -match '^SDL_' }); $sdlCommon = @($sdlT | Where-Object { $hp.Contains($_) })
    $need = (& "$Llvm\llvm-objdump.exe" -p $elf | Select-String 'NEEDED' | ForEach-Object { ($_ -split '\s+')[-1] } | Sort-Object) -join ' '
    $needP = (& "$Llvm\llvm-objdump.exe" -p $pplayElf | Select-String 'NEEDED' | ForEach-Object { ($_ -split '\s+')[-1] } | Sort-Object) -join ' '
    $libOk = ($mpvT.Count -eq $mpvP.Count) -and ($mpvCommon.Count -eq $mpvT.Count) -and ($avCommon.Count -eq $avT.Count) `
        -and ($sdlCommon.Count -eq $sdlT.Count) -and ($need -eq $needP)
    Report '6 pPlay playback libraries linked' $libOk `
        ("mpv+libass $($mpvCommon.Count)/$($mpvP.Count) identical, FFmpeg $($avCommon.Count)/$($avT.Count) in pPlay, " +
         "SDL2 $($sdlCommon.Count)/$($sdlT.Count) in pPlay, system modules identical: " + ($need -eq $needP))
} finally {
    if ($x -and (Test-Path -LiteralPath $x)) {
        Remove-Item -LiteralPath $x -Recurse -Force
        Write-Host "      (deleted extracted PKG copy: it contains credentials)"
    }
    Pop-Location
}

Write-Host ''
$fail = @($results | Where-Object { $_.StartsWith('FAIL') }).Count
Write-Host ("SUMMARY: {0} checks, {1} failed" -f $results.Count, $fail)
exit $fail
