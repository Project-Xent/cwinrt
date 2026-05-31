# PIID gate: diff cwinrt-gen's computed parameterized IIDs against the
# authoritative cppwinrt guid_v values. Builds the cppwinrt reference program and
# cwinrt-gen, runs both over a curated instantiation set, and fails on mismatch.
param(
    [string]$SdkVersion = "10.0.26100.0"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "msvc_devshell.ps1") 2>$null

$kits  = "${env:ProgramFiles(x86)}\Windows Kits\10"
$winmd = Join-Path $kits "UnionMetadata\$SdkVersion\Windows.winmd"
$inc   = Join-Path $kits "Include\$SdkVersion\cppwinrt"
$gen   = Join-Path $root "build\windows\x64\release\cwinrt-gen.exe"
$piidDir = Join-Path $root "tests\piid"
$refSrc = Join-Path $piidDir "cppwinrt_piids.cpp"
$refExe = Join-Path $piidDir "cppwinrt_piids.exe"

if (-not (Test-Path $winmd)) { Write-Error "Union metadata not found: $winmd" }
if (-not (Test-Path $gen))   { Write-Error "cwinrt-gen not built: run 'xmake build cwinrt-gen'" }

# (Re)build the cppwinrt reference if stale.
if (-not (Test-Path $refExe) -or (Get-Item $refSrc).LastWriteTime -gt (Get-Item $refExe).LastWriteTime) {
    Push-Location $piidDir
    & cl /nologo /std:c++20 /EHsc /I "$inc" $refSrc /Fe:$refExe | Out-Null
    Pop-Location
    if ($LASTEXITCODE -ne 0) { Write-Error "cppwinrt reference build failed" }
}

$mine = & $gen --selftest-piid2 "$winmd" 2>$null
$ref  = & $refExe

$mineMap = @{}; foreach ($l in $mine) { $p = $l -split ' '; if ($p.Count -ge 2) { $mineMap[$p[0]] = $p[1] } }
$refMap  = @{}; foreach ($l in $ref)  { $p = $l -split ' '; if ($p.Count -ge 2) { $refMap[$p[0]]  = $p[1] } }

$bad = 0
foreach ($k in $refMap.Keys) {
    if ($mineMap[$k] -ne $refMap[$k]) {
        Write-Host ("FAIL {0}`n  mine={1}`n  ref ={2}" -f $k, $mineMap[$k], $refMap[$k])
        $bad++
    }
}
$ok = $refMap.Count - $bad
Write-Host "piid check: $ok/$($refMap.Count) match cppwinrt ($bad mismatches)"
if ($bad -ne 0) { exit 1 }
