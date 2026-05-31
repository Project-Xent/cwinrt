# Prove codegen is deterministic — same winmd produces byte-identical
# output regardless of run or worker count (--jobs scheduling must not leak into
# output order). Regenerates into temp dirs and diffs.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "msvc_devshell.ps1") 2>$null

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) "cwinrt_determinism"
$a = Join-Path $tmp "a"; $b = Join-Path $tmp "b"; $c = Join-Path $tmp "c"
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue

function Regen($out, $jobs) {
    New-Item -ItemType Directory -Force (Join-Path $out "impl") | Out-Null
    & (Join-Path $PSScriptRoot "gen_all_windows.ps1") -Impl -Jobs $jobs -OutDir $out -ImplDir (Join-Path $out "impl") *> $null
    if ($LASTEXITCODE -ne 0) { Write-Error "regen failed (jobs=$jobs)" }
}

Write-Host "regen A (jobs 8)..."; Regen $a 8
Write-Host "regen B (jobs 8)..."; Regen $b 8
Write-Host "regen C (jobs 1)..."; Regen $c 1

$bad = 0
function DiffTree($x, $y, $label) {
    $d = Compare-Object (Get-ChildItem -Recurse -File $x | ForEach-Object {
            "{0}  {1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash, ($_.FullName.Substring($x.Length))
        }) (Get-ChildItem -Recurse -File $y | ForEach-Object {
            "{0}  {1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash, ($_.FullName.Substring($y.Length))
        })
    if ($d) { Write-Host "FAIL $label ($($d.Count) differing entries)"; return 1 }
    Write-Host "ok $label"; return 0
}
$bad += DiffTree $a $b "run-to-run (jobs 8 vs jobs 8)"
$bad += DiffTree $a $c "jobs-invariant (jobs 8 vs jobs 1)"
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
if ($bad) { Write-Error "determinism check FAILED" }
Write-Host "determinism check: byte-stable across runs and worker counts"
