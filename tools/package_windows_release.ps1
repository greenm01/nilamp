# SPDX-License-Identifier: MIT
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [Parameter(Mandatory = $true)]
    [string]$Plugin,

    [Parameter(Mandatory = $true)]
    [string]$ClapBundle,

    [Parameter(Mandatory = $true)]
    [string]$DistDir,

    [Parameter(Mandatory = $true)]
    [string]$GpgKey,

    [string]$ExistingSums = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Plugin -PathType Leaf)) {
    throw "package_windows_release: plugin not found: $Plugin"
}

if (-not (Get-Command gpg -ErrorAction SilentlyContinue)) {
    throw "package_windows_release: gpg not found"
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$distAbs = Join-Path $repoRoot $DistDir
New-Item -ItemType Directory -Force -Path $distAbs | Out-Null

$stageRoot = Join-Path ([System.IO.Path]::GetTempPath()) ([System.IO.Path]::GetRandomFileName())
$packageName = "nilamp-twd-mkii-v$Version-windows-x64"
$packageDir = Join-Path $stageRoot $packageName
$zipPath = Join-Path $distAbs "$packageName.zip"
$sumsPath = Join-Path $distAbs "SHA256SUMS"

try {
    New-Item -ItemType Directory -Force -Path $packageDir | Out-Null
    Remove-Item -LiteralPath $zipPath, "$zipPath.asc", $sumsPath, "$sumsPath.asc" -Force -ErrorAction SilentlyContinue

    Copy-Item -LiteralPath $Plugin -Destination (Join-Path $packageDir $ClapBundle) -Force
    Copy-Item -LiteralPath (Join-Path $repoRoot "LICENSE") -Destination (Join-Path $packageDir "LICENSE") -Force

    @"
@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
set "CLAP_NAME=nilamp-twd-mkii.clap"
set "SOURCE_CLAP=%SCRIPT_DIR%%CLAP_NAME%"
set "TARGET_DIR=%LOCALAPPDATA%\Programs\Common\CLAP"
set "TARGET_CLAP=%TARGET_DIR%\%CLAP_NAME%"

if not exist "%SOURCE_CLAP%" (
    echo Could not find %CLAP_NAME% next to install.cmd 1>&2
    exit /b 1
)

if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"
copy /Y "%SOURCE_CLAP%" "%TARGET_CLAP%"
if errorlevel 1 exit /b 1

echo Installed %TARGET_CLAP%
echo Restart your DAW or rescan CLAP plug-ins if nilamp was already open.
"@ | Set-Content -LiteralPath (Join-Path $packageDir "install.cmd") -Encoding ASCII

    @"
nilamp TWD MKII v$Version for Windows x64

Install:
  Double-click install.cmd, or copy $ClapBundle to:
  %LOCALAPPDATA%\Programs\Common\CLAP\$ClapBundle

REAPER scans CLAP plug-ins from:
  %COMMONPROGRAMFILES%\CLAP
  %LOCALAPPDATA%\Programs\Common\CLAP
  %CLAP_PATH%

After installing, restart your DAW or rescan CLAP plug-ins.

Verify release artifacts:
  gpg --verify SHA256SUMS.asc SHA256SUMS
  certutil -hashfile $packageName.zip SHA256
  gpg --verify $packageName.zip.asc $packageName.zip

Release signing key fingerprint:
  C3504EE1EE38410CE1C433BC372B8AAACB867F13

Keller reference:
  nilamp is based on Helmut Keller's "A Tube Amp Modeling Project".
  See https://www.helmutkelleraudio.de/ for Keller's original work.
"@ | Set-Content -LiteralPath (Join-Path $packageDir "README-Windows.txt") -Encoding ASCII

    Compress-Archive -LiteralPath $packageDir -DestinationPath $zipPath -Force

    $hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $zipName = Split-Path -Leaf $zipPath
    $sumLines = @()
    if ($ExistingSums -and (Test-Path -LiteralPath $ExistingSums -PathType Leaf)) {
        $sumLines = @(
            Get-Content -LiteralPath $ExistingSums |
                Where-Object { $_ -and ($_ -notmatch [regex]::Escape($zipName) + '$') }
        )
    }
    $sumLines += "$hash  $zipName"
    $sumLines | Set-Content -LiteralPath $sumsPath -Encoding ASCII

    Push-Location $distAbs
    try {
        & gpg --batch --yes --armor --local-user $GpgKey --detach-sign (Split-Path -Leaf $zipPath)
        & gpg --batch --yes --armor --local-user $GpgKey --detach-sign (Split-Path -Leaf $sumsPath)
    } finally {
        Pop-Location
    }

    Write-Output $zipPath
    Write-Output "$zipPath.asc"
    Write-Output $sumsPath
    Write-Output "$sumsPath.asc"
} finally {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force -ErrorAction SilentlyContinue
}
