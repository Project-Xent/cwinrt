# Differential check: diff the IIDs cwinrt extracts from the [Guid] attribute
# against the Windows SDK ABI headers. Non-generic interfaces only.
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
$abi = Join-Path $kit "Include\$SdkVersion\winrt"
if (-not (Test-Path $winmd)) { Write-Error "winmd not found: $winmd" }
if (-not (Test-Path $abi)) { Write-Error "Windows ABI headers not found: $abi" }

Write-Host "indexing Windows ABI interface IDs..."
$ref = @{}
$rx = [regex]'MIDL_INTERFACE\("([0-9A-Fa-f-]{36})"\)\s*\r?\n\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*public'
Get-ChildItem $abi -Filter "windows.*.h" | ForEach-Object {
    $namespace = [System.IO.Path]::GetFileNameWithoutExtension($_.Name)
    foreach ($m in $rx.Matches([System.IO.File]::ReadAllText($_.FullName))) {
        $name = "$namespace.$($m.Groups[2].Value)".ToLowerInvariant()
        $ref[$name] = $m.Groups[1].Value.ToUpperInvariant()
    }
}
Write-Host "Windows ABI interfaces indexed: $($ref.Count)"

$ours = & $gen --dump-iids $winmd
$checked = 0; $mismatch = 0; $missing = 0
foreach ($line in $ours) {
    if ($line -notmatch '^(\S+)\s+([0-9A-F-]{36})$') { continue }
    $name = $matches[1].ToLowerInvariant(); $guid = $matches[2].ToUpperInvariant()
    if (-not $ref.ContainsKey($name)) { $missing++; continue }
    $checked++
    if ($ref[$name] -ne $guid) {
        $mismatch++
        Write-Host "MISMATCH $name : ours=$guid sdk=$($ref[$name])"
    }
}
Write-Host "iid check: $checked verified against Windows ABI headers, $mismatch mismatch, $missing unavailable"
if ($mismatch -gt 0) { exit 1 }
