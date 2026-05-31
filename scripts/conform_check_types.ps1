# Conform: IAsync* out-params use instantiated WF_IAsyncOperation_* (not void**).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$storage = Join-Path $root "include/cwinrt/Windows.Storage.h"
if (-not (Test-Path $storage)) { Write-Error "missing $storage" }
$lines = Select-String -Path $storage -Pattern "void\*\* out" | Where-Object { $_.Line -match "Async" }
if ($lines) {
    Write-Host "FAIL Windows.Storage.h: IAsync methods still use void** out:"
    $lines | Select-Object -First 5 | ForEach-Object { Write-Host $_.Line }
    exit 1
}
$hasInst = Select-String -Path $storage -Pattern "WF_IAsyncOperation_WS_" -Quiet
if (-not $hasInst) {
    Write-Host "FAIL Windows.Storage.h: no instantiated WF_IAsyncOperation_WS_* signatures"
    exit 1
}
Write-Host "conform types: Storage IAsync signatures use instantiated types"
