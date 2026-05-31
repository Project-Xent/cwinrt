# Differential check: diff the IIDs cwinrt extracts from the [Guid]
# attribute against cppwinrt's guid_v<> constants (an independent second source
# that reads the same winmd). Non-generic interfaces only (generic PIIDs = 2b).
# Exits nonzero on any mismatch.
param(
    [string]$SdkVersion = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not $SdkVersion) {
    $verFile = Join-Path $root "sdk.version"
    $SdkVersion = (Test-Path $verFile) ? (Get-Content $verFile -Raw).Trim() : "10.0.26100.0"
}
$gen = Join-Path $root "build\windows\x64\release\cwinrt-gen.exe"
if (-not (Test-Path $gen)) { xmake build cwinrt-gen }
$kit = "${env:ProgramFiles(x86)}\Windows Kits\10"
$winmd = Join-Path $kit "UnionMetadata\$SdkVersion\Windows.winmd"
$cppwinrt = Join-Path $kit "Include\$SdkVersion\cppwinrt\winrt\impl"
if (-not (Test-Path $winmd)) { Write-Error "winmd not found: $winmd" }
if (-not (Test-Path $cppwinrt)) { Write-Error "cppwinrt headers not found: $cppwinrt" }

# Build cppwinrt reference map: full.dotted.Name -> GUID (uppercase, no braces).
Write-Host "indexing cppwinrt guid_v constants..."
$ref = @{}
$rx = [regex]'guid_v<winrt::([A-Za-z0-9_:]+)>.*//\s*([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12})'
Get-ChildItem $cppwinrt -Filter "*.0.h" | ForEach-Object {
    foreach ($m in $rx.Matches([System.IO.File]::ReadAllText($_.FullName))) {
        $name = $m.Groups[1].Value -replace '::', '.'
        $ref[$name] = $m.Groups[2].Value.ToUpper()
    }
}
Write-Host "cppwinrt interfaces indexed: $($ref.Count)"

$ours = & $gen --dump-iids $winmd
$checked = 0; $mismatch = 0; $missing = 0
foreach ($line in $ours) {
    if ($line -notmatch '^(\S+)\s+([0-9A-F-]{36})$') { continue }
    $name = $matches[1]; $guid = $matches[2].ToUpper()
    if (-not $ref.ContainsKey($name)) { $missing++; continue }   # not in cppwinrt (e.g. newer/private)
    $checked++
    if ($ref[$name] -ne $guid) {
        $mismatch++
        Write-Host "MISMATCH $name : ours=$guid cppwinrt=$($ref[$name])"
    }
}
Write-Host "iid check: $checked verified against cppwinrt, $mismatch mismatch, $missing not-in-cppwinrt"
if ($mismatch -gt 0) { exit 1 }
