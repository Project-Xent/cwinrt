# Compile each tests/conform/header_shards/compile_impl_*.c (header + impl in one TU).
param(
    [string]$ShardDir = "tests/conform/header_shards",
    [string]$IncludeDir = "include",
    [int]$Max = 0,
    [int]$MinPass = 342,
    [int]$Parallel = 0
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

. (Join-Path $PSScriptRoot "msvc_devshell.ps1")

$shardRoot = Join-Path $root $ShardDir
$include = Join-Path $root $IncludeDir
$files = Get-ChildItem $shardRoot -Filter "compile_impl_*.c" | Sort-Object Name
if ($files.Count -eq 0) {
    Write-Error "no compile_impl_*.c in $shardRoot (run gen_header_compile_shards.ps1)"
}

$objDir = Join-Path $root "build\.header_shards\impl_obj"
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

$fail = 0
$ok = 0
$i = 0

function Invoke-ImplShardCompile {
    param($File, $ObjPath, $IncludePath, [switch]$Quiet)
    $args = @("/nologo", "/TC", "/std:c17", "/W4", "/c", "/I$IncludePath", $File.FullName, "/Fo$ObjPath")
    if ($Quiet) {
        & cl.exe @args 2>&1 | Out-Null
    } else {
        & cl.exe @args
    }
    return $LASTEXITCODE
}

if ($Parallel -gt 0) {
    if ($PSVersionTable.PSVersion.Major -lt 7) {
        Write-Error "-Parallel requires PowerShell 7+"
    }
    if ($Parallel -gt 32) { $Parallel = 32 }
    $toRun = @()
    foreach ($f in $files) {
        if ($Max -gt 0 -and $i -ge $Max) { break }
        $i++
        $toRun += $f
    }
    $failed = [System.Collections.Concurrent.ConcurrentBag[string]]::new()
    $toRun | ForEach-Object -Parallel {
        $f = $_
        $obj = Join-Path $using:objDir ($f.BaseName + ".obj")
        $args = @("/nologo", "/TC", "/std:c17", "/W4", "/c", "/I$($using:include)", $f.FullName, "/Fo$obj")
        & cl.exe @args 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            $bag = $using:failed
            $bag.Add($f.Name) | Out-Null
        }
    } -ThrottleLimit $Parallel
    $fail = $failed.Count
    $ok = $toRun.Count - $fail
    foreach ($n in $failed) {
        Write-Host "FAIL $n"
        $f = $toRun | Where-Object { $_.Name -eq $n } | Select-Object -First 1
        if ($f) {
            $obj = Join-Path $objDir ($f.BaseName + ".obj")
            Invoke-ImplShardCompile -File $f -ObjPath $obj -IncludePath $include | Out-Null
        }
    }
} else {
    foreach ($f in $files) {
        if ($Max -gt 0 -and $i -ge $Max) { break }
        $i++
        $obj = Join-Path $objDir ($f.BaseName + ".obj")
        $rc = Invoke-ImplShardCompile -File $f -ObjPath $obj -IncludePath $include -Quiet
        if ($rc -ne 0) {
            Write-Host "FAIL $($f.Name)"
            Invoke-ImplShardCompile -File $f -ObjPath $obj -IncludePath $include | Out-Null
            $fail++
        } else {
            $ok++
        }
    }
}

Write-Host "impl compile: $ok ok, $fail failed (of $($files.Count) namespaces, min pass $MinPass)"
if ($ok -lt $MinPass) { exit 1 }
