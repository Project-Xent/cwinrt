# Generate all Windows.* binding headers from Union metadata.
param(
    [string]$SdkVersion = "",
    [string]$OutDir = "include/cwinrt",
    [string]$ImplDir = "include/cwinrt/impl",
    [switch]$Impl,
    [switch]$Refs,
    [switch]$Continue,
    # Worker threads inside one cwinrt-gen process (shared winmd parse). 0 = CPU count.
    [int]$Jobs = 0,
    # Deprecated: multi-process mode re-parses winmd per worker. Prefer -Jobs.
    [int]$Parallel = 0
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# Accept absolute or repo-relative output dirs (callers like check_determinism.ps1
# pass absolute temp dirs).
if (-not [System.IO.Path]::IsPathRooted($OutDir))  { $OutDir = Join-Path $root $OutDir }
if (-not [System.IO.Path]::IsPathRooted($ImplDir)) { $ImplDir = Join-Path $root $ImplDir }

if (-not $SdkVersion) {
    $verFile = Join-Path $root "sdk.version"
    if (Test-Path $verFile) {
        $SdkVersion = (Get-Content $verFile -Raw).Trim()
    } else {
        $SdkVersion = "10.0.26100.0"
    }
}
Write-Host "SDK version: $SdkVersion"

$gen = Join-Path $root "build\windows\x64\release\cwinrt-gen.exe"
if (-not (Test-Path $gen)) {
    Write-Host "Building cwinrt-gen..."
    xmake build cwinrt-gen
    if (-not (Test-Path $gen)) {
        Write-Error "cwinrt-gen not found at $gen"
    }
}

$kits = "${env:ProgramFiles(x86)}\Windows Kits\10"
$winmd = Join-Path $kits "UnionMetadata\$SdkVersion\Windows.winmd"
if (-not (Test-Path $winmd)) {
    Write-Error "Union metadata not found: $winmd"
}

if ($Refs) {
    Write-Warning "Contract References/*.winmd files use different names than API namespaces."
    Write-Warning "Use default Union batch (omit -Refs) for consumer headers like Windows.UI.Composition.h"
}

if ($Parallel -gt 0) {
    Write-Warning "-Parallel uses many processes and re-parses winmd each time; prefer -Jobs (shared winmd in one process)."
    if ($Jobs -le 0) { $Jobs = $Parallel }
}

if ($Jobs -le 0) {
    $Jobs = [Environment]::ProcessorCount
}
if ($Jobs -gt 64) { $Jobs = 64 }

$mode = if ($Impl) { "headers + impl" } else { "headers only" }
$genArgs = @("--batch-union", "--winmd", $winmd, "-o", $OutDir, "--jobs", $Jobs)
if ($Impl) {
    $genArgs += "--impl"
    $genArgs += @("--impl-dir", $ImplDir)
}
if ($Continue) { $genArgs += "--continue" }
Write-Host "cwinrt-gen --batch-union ($mode, winmd once, --jobs $Jobs)"
& $gen @genArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Parameterized IIDs for generic instantiations (cwinrt_piids.h + .impl.c).
& $gen --emit-piids $winmd $OutDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$count = (Get-ChildItem $OutDir -Filter "Windows.*.h").Count
Write-Host "wrote $count headers under $OutDir"
if ($Impl) {
    $implCount = (Get-ChildItem $ImplDir -Filter "*.impl.c" -ErrorAction SilentlyContinue).Count
    Write-Host "wrote $implCount impl files under $ImplDir"
}
