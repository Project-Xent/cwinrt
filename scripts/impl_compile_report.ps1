# Regenerate impl shards, compile all, write summary for CI.
param(
    [string]$ShardDir = "tests/conform/header_shards",
    [string]$ReportPath = "build/impl_compile_report.txt"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

& (Join-Path $PSScriptRoot "gen_header_compile_shards.ps1") | Out-Host
& (Join-Path $PSScriptRoot "build_impl_compile_shards.ps1") | Tee-Object -Variable out
$line = ($out | Select-Object -Last 1)
New-Item -ItemType Directory -Force -Path (Split-Path (Join-Path $root $ReportPath)) | Out-Null
Set-Content -Path (Join-Path $root $ReportPath) -Value $line
if ($LASTEXITCODE -ne 0) { exit 1 }
