# Fast impl-compile gate: compile ALL compile_impl_*.c in a single
# `cl /MP` invocation (one process, all cores) instead of 342 sequential cl
# launches. ~5-10x faster. Reports ok/failed per namespace by mapping each
# error's source path back to its namespace. Exit 1 if any namespace fails.
param(
    [string]$ShardDir = "tests/conform/header_shards",
    [string]$IncludeDir = "include",
    [int]$MinPass = 342
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

. (Join-Path $PSScriptRoot "msvc_devshell.ps1")

$shardRoot = Join-Path $root $ShardDir
$include = Join-Path $root $IncludeDir
$files = Get-ChildItem $shardRoot -Filter "compile_impl_*.c" | Sort-Object Name
if ($files.Count -eq 0) { Write-Error "no compile_impl_*.c in $shardRoot" }

$objDir = Join-Path $root "build\.header_shards\fast_obj"
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

# Response file avoids the command-line length limit with 342 paths.
# /Fo names an OUTPUT DIRECTORY (trailing separator). Use a forward slash so the
# closing quote isn't escaped by a backslash (\" -> literal quote => cl D8036,
# which aborts before compiling and silently looks like "0 errors").
$rsp = Join-Path $objDir "fast.rsp"
$foDir = ($objDir -replace '\\', '/') + "/"
$lines = @("/nologo", "/TC", "/std:c17", "/W4", "/c", "/MP", "/I`"$include`"", "/Fo`"$foDir`"")
$lines += $files | ForEach-Object { "`"$($_.FullName)`"" }
Set-Content -Path $rsp -Value $lines -Encoding ascii

# Capture cl output to a FILE: with /MP, cl spawns child processes that inherit
# the file handle, so their errors are captured (a piped `2>&1` into a variable
# misses child output and silently reported false-green).
$cllog = Join-Path $objDir "cl_out.txt"
$proc = Start-Process -FilePath "cl.exe" -ArgumentList "@`"$rsp`"" -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput $cllog -RedirectStandardError "$cllog.err"
$rc = $proc.ExitCode
$out = @()
if (Test-Path $cllog) { $out += Get-Content $cllog }
if (Test-Path "$cllog.err") { $out += Get-Content "$cllog.err" }

# Map each error line's source path to a namespace (Windows.*.h / *.impl.c).
$failed = @{}
foreach ($line in $out) {
    if ($line -match ': (?:fatal )?error [A-Z]+\d+') {
        if ($line -match '([A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)*)\.(?:impl\.c|h)\(') {
            $failed[$matches[1]] = $true
        }
    }
}
$fail = $failed.Count
$ok = $files.Count - $fail
foreach ($ns in ($failed.Keys | Sort-Object)) { Write-Host "FAIL $ns" }
# cl's exit code is the source of truth: a nonzero exit with no mapped namespace
# means our parsing missed something -- treat it as failure, never false-green.
$unattributed = ($rc -ne 0 -and $fail -eq 0)
if ($unattributed) {
    Write-Host "FAIL (cl exit=$rc, errors not attributable to a namespace):"
    $out | Where-Object { $_ -match ': (?:fatal )?error ' } | Select-Object -First 15 | ForEach-Object { Write-Host "  $_" }
}
Write-Host "impl compile (fast): $ok ok, $fail failed (of $($files.Count) namespaces, min pass $MinPass)"
if ($ok -lt $MinPass -or $unattributed) { exit 1 }
