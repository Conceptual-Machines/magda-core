#Requires -Version 5.1
<#
.SYNOPSIS
  Sign MAGDA's Windows binaries and build a signed installer, locally.

.DESCRIPTION
  Code signing uses the Certum "Open Source Developer" cloud certificate via
  SimplySign. SimplySign needs an interactive OTP from the SimplySign Mobile
  app each session, so this CANNOT run on a GitHub-hosted runner - it must run
  on a machine where SimplySign Desktop is logged in (the cloud cert then
  appears in the Windows cert store as a virtual smart card).

  Point this at a staging directory containing the unsigned MAGDA.exe,
  magda_plugin_scanner.exe and magda-installer.nsi - i.e. the extracted
  MAGDA-<version>-MuseHub-Source.zip from a release, or the installer staging
  dir from a local build. It then:
    1. signs MAGDA.exe and magda_plugin_scanner.exe in place,
    2. runs makensis to build the installer,
    3. signs the installer,
    4. verifies every signature against the trust chain.

  Output is renamed to MAGDA-<version>-Windows-x86_64-Setup.exe to match the
  asset name the release workflow publishes, so it is a drop-in replacement for
  the unsigned installer.

.PARAMETER StageDir
  Directory holding the unsigned exes + .nsi. Defaults to the script's own
  directory (matches the MuseHub bundle layout, where this script sits beside
  the inputs).

.PARAMETER Version
  Version passed to makensis. Defaults to version.txt next to the .nsi.

.PARAMETER Thumbprint
  SHA1 thumbprint of the signing certificate. Defaults to the Certum Open
  Source Developer cert. Find it with:
    Get-ChildItem Cert:\CurrentUser\My | Format-List Subject, Thumbprint

.PARAMETER Makensis
  Path to makensis.exe. Defaults to "makensis" on PATH.

.PARAMETER SignTool
  Path to signtool.exe. Auto-detected from the newest Windows SDK if omitted.

.PARAMETER WaitMinutes
  How long to wait for the signing cert to appear in the store before giving
  up. The cert only shows up once SimplySign Desktop is logged in (email + OTP
  from the phone app). With the default 0 the script fails immediately if you
  are not logged in; in CI, pass a positive value so the job parks and polls -
  push the tag whenever, then log into SimplySign and the job proceeds the
  instant the cert appears.

.EXAMPLE
  .\sign-release.ps1
  Signs the bundle in this directory using version.txt and the default cert.

.EXAMPLE
  .\sign-release.ps1 -StageDir C:\work\magda-bundle -Version 0.13.0
#>
param(
    [string]$StageDir,
    [string]$Version,
    [string]$Thumbprint = "715B3557D43AF8FC23297999C0541C07E1B61319",
    [string]$Makensis = "makensis",
    [string]$SignTool,
    [int]$WaitMinutes = 0
)

$ErrorActionPreference = "Stop"

# Certum's RFC-3161 timestamp server. Timestamping is mandatory: it keeps the
# signature valid after the cert expires (2027-06-29).
$TimestampUrl = "http://time.certum.pl"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if (-not $StageDir) { $StageDir = $scriptDir }
$StageDir = (Resolve-Path $StageDir).Path

# --- locate signtool -------------------------------------------------------
if (-not $SignTool) {
    if (Get-Command signtool.exe -ErrorAction SilentlyContinue) {
        $SignTool = (Get-Command signtool.exe).Source
    } else {
        $SignTool = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1 -ExpandProperty FullName
    }
}
if (-not $SignTool -or -not (Test-Path $SignTool)) {
    throw "signtool.exe not found. Install the Windows 10/11 SDK or pass -SignTool."
}
Write-Host "signtool : $SignTool"

# --- resolve version -------------------------------------------------------
$nsi = Join-Path $StageDir "magda-installer.nsi"
if (-not (Test-Path $nsi)) {
    throw "magda-installer.nsi not found in $StageDir"
}
if (-not $Version) {
    $versionFile = Join-Path $StageDir "version.txt"
    if (Test-Path $versionFile) {
        $Version = (Get-Content $versionFile -Raw).Trim()
    } else {
        throw "No -Version supplied and version.txt not found in $StageDir."
    }
}
Write-Host "version  : $Version"
Write-Host "stage    : $StageDir"

# --- confirm the cert is present (waiting for SimplySign login if asked) ----
# The cert only appears once SimplySign Desktop is logged in. With -WaitMinutes
# > 0 the script parks and polls so a release job can be dispatched before you
# log in: it proceeds the instant the virtual smart card shows up.
# Search only CurrentUser\My - that is exactly where `signtool sign /sha1`
# looks without /sm, and where SimplySign Desktop places the cloud cert.
# Including LocalMachine\My here would let the pre-check pass on a cert that
# signtool can't actually use, turning a clear "not logged in" message into a
# confusing later failure.
function Get-SigningCert {
    Get-ChildItem Cert:\CurrentUser\My -ErrorAction SilentlyContinue |
        Where-Object { $_.Thumbprint -eq $Thumbprint } |
        Select-Object -First 1
}
$cert = Get-SigningCert
if (-not $cert -and $WaitMinutes -gt 0) {
    $deadline = (Get-Date).AddMinutes($WaitMinutes)
    Write-Host "Waiting up to $WaitMinutes min for SimplySign login (cert $Thumbprint)..."
    while (-not $cert) {
        if ((Get-Date) -gt $deadline) { break }
        Start-Sleep -Seconds 10
        Write-Host "  still waiting - log into SimplySign Desktop (email + phone OTP)..."
        $cert = Get-SigningCert
    }
}
if (-not $cert) {
    throw @"
Signing cert $Thumbprint not found in the Windows store.
Is SimplySign Desktop running and logged in? Open it, log in with your email +
the OTP from the SimplySign Mobile app, then re-run (or pass -WaitMinutes N to
park and poll while you log in).
"@
}
Write-Host "cert     : $($cert.Subject)"
Write-Host ""

function Invoke-Sign([string]$Path) {
    if (-not (Test-Path $Path)) { throw "Cannot sign - file not found: $Path" }
    Write-Host "Signing $Path ..."
    & $SignTool sign /sha1 $Thumbprint /fd sha256 /tr $TimestampUrl /td sha256 /v $Path
    if ($LASTEXITCODE -ne 0) { throw "signtool sign failed ($LASTEXITCODE) for $Path" }
    & $SignTool verify /pa /v $Path
    if ($LASTEXITCODE -ne 0) { throw "signtool verify failed ($LASTEXITCODE) for $Path" }
    Write-Host ""
}

# 1. Sign the two MAGDA executables in place. The ONNX Runtime and libxml2 DLLs
#    are already signed by their vendors and must not be re-signed.
Invoke-Sign (Join-Path $StageDir "MAGDA.exe")
$scanner = Join-Path $StageDir "magda_plugin_scanner.exe"
if (Test-Path $scanner) {
    Invoke-Sign $scanner
} else {
    Write-Warning "magda_plugin_scanner.exe not present in stage - skipping (out-of-process scanning will be unavailable)"
}

# 2. Build the installer from the now-signed exes. The .nsi's OutFile is a
#    relative path, so run makensis with the working directory set to StageDir
#    to guarantee the installer is written there (the .nsi resolves its File
#    inputs via ${__FILEDIR__}, so they are found regardless).
Write-Host "Building installer ..."
Push-Location $StageDir
try {
    & $Makensis "/DVERSION=$Version" $nsi
    if ($LASTEXITCODE -ne 0) { throw "makensis failed ($LASTEXITCODE)" }
} finally {
    Pop-Location
}

$built = Join-Path $StageDir "MAGDA-$Version-Windows-Setup.exe"
if (-not (Test-Path $built)) { throw "Installer not produced at $built" }

# 3. Sign the installer itself.
Invoke-Sign $built

# 4. Rename to the published asset name (matches .github/workflows/release.yml).
$final = Join-Path $StageDir "MAGDA-$Version-Windows-x86_64-Setup.exe"
Move-Item $built $final -Force

# Refresh the checksum so it matches the signed installer.
$hash = Get-FileHash -Algorithm SHA256 $final
"$($hash.Hash)  $(Split-Path -Leaf $final)" | Out-File -Encoding ascii "$final.sha256"

Write-Host ""
Write-Host "Done. Signed installer:"
Write-Host "  $final"
Write-Host "  $final.sha256"
