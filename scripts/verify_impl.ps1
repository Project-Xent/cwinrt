# Fail if any generated .impl.c still contains E_NOTIMPL stubs.
param(
    [string]$ImplDir = "include/cwinrt/impl"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$dir = Join-Path $root $ImplDir
if (-not (Test-Path $dir)) {
    Write-Error "impl dir not found: $dir (run xmake gen-all-impl first)"
}

$hits = Select-String -Path (Join-Path $dir "*.impl.c") -Pattern "\bE_NOTIMPL\b" -ErrorAction SilentlyContinue
if ($hits) {
    $hits | ForEach-Object { Write-Host "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }
    Write-Error "found $($hits.Count) E_NOTIMPL in $ImplDir"
}

$count = (Get-ChildItem $dir -Filter "*.impl.c").Count
Write-Host "verify_impl: $count impl files, zero E_NOTIMPL"
