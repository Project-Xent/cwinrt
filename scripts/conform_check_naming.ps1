# Fail if generated headers mix long typedefs with abbreviated ones (e.g. WUC_Compositor vs WUC_Comp).
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$bad = @(
    @{ Header = "Windows.UI.Composition.h"; Long = "WUC_Compositor"; Short = "WUC_Comp" },
    @{ Header = "Windows.UI.Composition.h"; Long = "WUC_SpriteVisual"; Short = "WUC_Sprite" },
    @{ Header = "Windows.UI.Composition.h"; Long = "WUC_ContainerVisual"; Short = "WUC_Container" },
    @{ Header = "Windows.Graphics.Capture.h"; Long = "WGC_GraphicsCaptureItem"; Short = "WGC_CaptureItem" }
)
$fail = 0
foreach ($rule in $bad) {
    $path = Join-Path $root "include/cwinrt/$($rule.Header)"
    if (-not (Test-Path $path)) {
        Write-Error "missing $path"
    }
    $text = Get-Content $path -Raw
    if ($text -match "\b$($rule.Long)\b") {
        Write-Host "FAIL $($rule.Header): found long typedef $($rule.Long) (use $($rule.Short) only)"
        $fail++
    }
}
if ($fail -gt 0) { exit 1 }
Write-Host "conform naming: no mixed long/abbrev typedefs"
