# Validate vtable slot assignment across every namespace's
# interfaces against the WinRT ABI rule (IInspectable 0-5; interface methods
# from slot 6 in metadata token order; runtimeclass methods get a real slot +
# dispatch target). Exits nonzero on any violation. See gen/slot.c:cwinrt_slot_check.
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
$winmd = Join-Path "${env:ProgramFiles(x86)}\Windows Kits\10\UnionMetadata\$SdkVersion" "Windows.winmd"
if (-not (Test-Path $winmd)) { Write-Error "Union metadata not found: $winmd" }

& $gen --check-slots $winmd
exit $LASTEXITCODE
