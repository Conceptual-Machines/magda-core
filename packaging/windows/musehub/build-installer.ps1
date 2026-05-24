#Requires -Version 5.1
<#
.SYNOPSIS
  Builds the MAGDA Windows installer from this bundle.

.DESCRIPTION
  Run this AFTER signing MAGDA.exe and magda_plugin_scanner.exe in place
  (in the same directory as this script). It invokes makensis on
  magda-installer.nsi to produce MAGDA-<version>-Windows-Setup.exe, which
  you then sign as the final step.

.PARAMETER Version
  Version string passed to makensis as /DVERSION. If omitted, the value is
  read from version.txt next to this script (written by the release pipeline).

.PARAMETER Makensis
  Path to makensis.exe. Defaults to "makensis" (expects it on PATH).

.EXAMPLE
  .\build-installer.ps1
  Builds using the version from version.txt and makensis on PATH.

.EXAMPLE
  .\build-installer.ps1 -Version 0.9.0 -Makensis "C:\Program Files (x86)\NSIS\makensis.exe"
#>
param(
    [string]$Version,
    [string]$Makensis = "makensis"
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition

if (-not $Version) {
    $versionFile = Join-Path $scriptDir "version.txt"
    if (Test-Path $versionFile) {
        $Version = (Get-Content $versionFile -Raw).Trim()
    } else {
        throw "No -Version supplied and version.txt not found next to this script."
    }
}

$nsi = Join-Path $scriptDir "magda-installer.nsi"
if (-not (Test-Path $nsi)) {
    throw "magda-installer.nsi not found next to this script at $nsi"
}

Write-Host "Building MAGDA $Version installer with $Makensis ..."
& $Makensis "/DVERSION=$Version" $nsi
if ($LASTEXITCODE -ne 0) {
    throw "makensis failed with exit code $LASTEXITCODE"
}

$installer = Join-Path $scriptDir "MAGDA-$Version-Windows-Setup.exe"
if (-not (Test-Path $installer)) {
    throw "Expected installer not found at $installer"
}

Write-Host ""
Write-Host "Installer built: $installer"
Write-Host "Final step: sign this installer before distribution."
