# One translation unit per Windows.* header; plus header+impl shards for signature checking.
param(
    [string]$HeaderDir = "include/cwinrt",
    [string]$ImplDir = "include/cwinrt/impl",
    [string]$OutDir = "tests/conform/header_shards",
    [string[]]$Only = @()
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$hdrRoot = Join-Path $root $HeaderDir
$implRoot = Join-Path $root $ImplDir
$outRoot = Join-Path $root $OutDir
if (-not (Test-Path $hdrRoot)) {
    Write-Error "header dir not found: $hdrRoot"
}

if (Test-Path $outRoot) {
    Remove-Item (Join-Path $outRoot "compile_Windows_*.c") -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $outRoot "compile_impl_*.c") -Force -ErrorAction SilentlyContinue
} else {
    New-Item -ItemType Directory -Path $outRoot | Out-Null
}

$headers = Get-ChildItem $hdrRoot -Filter "Windows.*.h" | Sort-Object Name
if ($Only.Count -gt 0) {
    $headers = $headers | Where-Object { $Only -contains $_.Name }
}

$nHdr = 0
$nImpl = 0
foreach ($h in $headers) {
    $safe = ($h.BaseName -replace '\.', '_')
    $rel = "cwinrt/" + $h.Name
    $hdrPath = Join-Path $outRoot ("compile_{0}.c" -f $safe)
    @"
/* Auto-generated: header compile-test $rel */
#include <$rel>
int main(void) { return 0; }
"@ | Set-Content -Path $hdrPath -Encoding UTF8
    $nHdr++

    $implFile = Join-Path $implRoot ($h.BaseName + ".impl.c")
    if (-not (Test-Path $implFile)) {
        continue
    }
    $implRel = "../../../include/cwinrt/impl/" + $h.BaseName + ".impl.c"
    $implPath = Join-Path $outRoot ("compile_impl_{0}.c" -f $safe)
    @"
/* Auto-generated: header+impl compile-test $rel */
#include <$rel>
#include "$implRel"
int main(void) { return 0; }
"@ | Set-Content -Path $implPath -Encoding UTF8
    $nImpl++
}

Write-Host "wrote $nHdr header shards and $nImpl impl shards under $OutDir"
