# Compile each tests/conform/header_shards/compile_*.c (run gen_header_compile_shards.ps1 first).
param(
    [string]$ShardDir = "tests/conform/header_shards",
    [string]$IncludeDir = "include",
    [int]$Max = 0,
    [int]$MinPass = 342,
    # 0 = sequential cl.exe; N>0 = compile N shards in parallel (PowerShell 7+)
    [int]$Parallel = 0
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$shardRoot = Join-Path $root $ShardDir
$include = Join-Path $root $IncludeDir
$files = Get-ChildItem $shardRoot -Filter "compile_Windows_*.c" | Sort-Object Name
if ($files.Count -eq 0) {
    Write-Error "no compile_Windows_*.c in $shardRoot (run gen_header_compile_shards.ps1)"
}

. (Join-Path $PSScriptRoot "msvc_devshell.ps1")

$objDir = Join-Path $root "build\.header_shards\obj"
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

$fail = 0
$ok = 0
$i = 0
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
            & cl.exe @("/nologo", "/TC", "/std:c17", "/W4", "/c", "/I$include", $f.FullName, "/Fo$obj")
        }
    }
} else {
    foreach ($f in $files) {
        if ($Max -gt 0 -and $i -ge $Max) { break }
        $i++
        $obj = Join-Path $objDir ($f.BaseName + ".obj")
        $args = @("/nologo", "/TC", "/std:c17", "/W4", "/c", "/I$include", $f.FullName, "/Fo$obj")
        & cl.exe @args 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Write-Host "FAIL $($f.Name)"
            & cl.exe @args
            $fail++
        } else {
            $ok++
        }
    }
}
Write-Host "header compile: $ok ok, $fail failed (of $($files.Count) headers, min pass $MinPass)"
if ($ok -lt $MinPass) { exit 1 }
