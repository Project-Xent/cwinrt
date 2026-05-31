# Register sparse package identity for compositor runtime conform test.
# Requires Windows SDK (MakeAppx, SignTool). Run once per machine/user.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$sparse = Join-Path $root "tests\conform\sparse"
$out = Join-Path $root "build\conform_sparse"
$msix = Join-Path $out "cwinrt.conform.test.msix"
$pfx = Join-Path $out "cwinrt-conform.pfx"
$cer = Join-Path $out "cwinrt-conform.cer"
$bin = Join-Path $root "build\windows\x64\release"

New-Item -ItemType Directory -Force -Path $out | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $sparse "Assets") | Out-Null

# 1x1 PNG
$png = Join-Path $sparse "Assets\StoreLogo.png"
if (-not (Test-Path $png)) {
    [IO.File]::WriteAllBytes($png, [Convert]::FromBase64String(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="))
}

$makeappx = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\makeappx.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1
$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $makeappx) { throw "MakeAppx.exe not found (install Windows SDK)" }

if (-not (Test-Path $pfx)) {
    $cert = New-SelfSignedCertificate -Type Custom -Subject "CN=cwinrt-conform" -KeyUsage DigitalSignature `
        -FriendlyName "cwinrt conform" -CertStoreLocation "Cert:\CurrentUser\My" -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")
    Export-PfxCertificate -Cert $cert -FilePath $pfx -Password (ConvertTo-SecureString -String "cwinrt" -AsPlainText -Force) | Out-Null
    Export-Certificate -Cert $cert -FilePath $cer | Out-Null
}
if (Test-Path $cer) {
    certutil -addstore -user TrustedPeople $cer | Out-Null
    certutil -addstore -user Root $cer | Out-Null
}

& $makeappx pack /o /d $sparse /nv /p $msix
& $signtool sign /fd SHA256 /f $pfx /p cwinrt $msix

$ext = $bin
if (-not (Test-Path $ext)) { $ext = (Get-Location).Path }

Get-AppxPackage cwinrt.conform.test -ErrorAction SilentlyContinue | Remove-AppxPackage -ErrorAction SilentlyContinue
Add-AppxPackage -Path $msix -ExternalLocation $ext
Write-Host "Registered sparse package; external location: $ext"
