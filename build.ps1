# SPDX-License-Identifier: MIT
# Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

<#
.SYNOPSIS
    One-click build for the three APC-Injector projects using MSVC/msbuild + WDK.

.DESCRIPTION
    Locates Visual Studio, enters its developer environment, then builds:
      - inject\Injector.sys     (kernel driver, WDK WindowsKernelModeDriver10.0)
      - InjectDll\InjectDll.dll (user-mode payload DLL)
      - R3Comm\R3Comm.exe       (user-mode CLI)

.PARAMETER Configuration
    Debug or Release. Default: Release.

.PARAMETER Platform
    x64 or ARM64. Default: x64. All three projects support both.

.PARAMETER Clean
    Run /t:Clean before building.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Configuration Debug -Platform x64
    .\build.ps1 -Configuration Release -Platform ARM64 -Clean
#>

[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("x64", "ARM64")]
    [string]$Platform = "x64",

    [switch]$Clean
)

$ErrorActionPreference = "Stop"

function Write-Step([string]$Message) {
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if ($vsPath) {
            return ($vsPath | Select-Object -First 1).Trim()
        }
    }

    # Fallback: common default install location
    # Please change this path to your Visual Studio installation path if you are using a different edition or version.
    $fallback = "C:\Program Files\Microsoft Visual Studio\2022\Professional" 
    if (Test-Path $fallback) {
        return $fallback
    }

    throw "Visual Studio not found (install 'Desktop development with C++')."
}

$root = $PSScriptRoot

Write-Step "Locating Visual Studio ..."
$vsInstall = Find-VisualStudio
Write-Host "  Visual Studio: $vsInstall"

# Some launchers prepend PATH in a case-sensitive way, leaving both 'Path' and
# 'PATH' in the process environment block. That makes .NET's ProcessStartInfo
# (used by MSBuild to spawn CL.exe) throw "duplicate key". Normalize to one
# single entry before starting any child process.
$savedPath = $env:Path
[Environment]::SetEnvironmentVariable("Path", $null, "Process")
[Environment]::SetEnvironmentVariable("Path", $null, "Process")
[Environment]::SetEnvironmentVariable("Path", $savedPath, "Process")

# Enter the MSVC build environment and build in the SAME cmd process, so the
# INCLUDE/LIB/PATH variables set by VsDevCmd are visible to msbuild/cl.exe.
$hostArch = "x64"
$vsDevCmd = Join-Path $vsInstall "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $vsDevCmd)) {
    throw "VsDevCmd.bat not found: $vsDevCmd"
}
Write-Step "Entering MSVC dev environment (arch=$Platform, host=$hostArch)"

$projects = @(
    (Join-Path $root "inject\inject.vcxproj"),
    (Join-Path $root "InjectDll\InjectDll.vcxproj"),
    (Join-Path $root "R3Comm\R3Comm.vcxproj")
)

foreach ($proj in $projects) {
    Write-Step "Building: $proj  ($Configuration|$Platform)"

    if ($Clean) {
        & cmd.exe /d /c "`"$vsDevCmd`" -arch=$($Platform.ToLower()) -host_arch=$hostArch -no_logo && msbuild `"$proj`" /t:Clean /p:Configuration=$Configuration /p:Platform=$Platform /v:m /nologo"
        if ($LASTEXITCODE -ne 0) {
            throw "Clean failed: $proj"
        }
    }

    & cmd.exe /d /c "`"$vsDevCmd`" -arch=$($Platform.ToLower()) -host_arch=$hostArch -no_logo && msbuild `"$proj`" /t:Build /p:Configuration=$Configuration /p:Platform=$Platform /m /v:m /nologo"
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed: $proj"
    }
}

Write-Step "Build succeeded. Artifacts:"
$artifacts = @(
    @{ Name = "inject.sys";    Folder = "inject" },
    @{ Name = "InjectDll.dll"; Folder = "InjectDll" },
    @{ Name = "R3Comm.exe";    Folder = "R3Comm" }
)

foreach ($item in $artifacts) {
    $found = Get-ChildItem -Path (Join-Path $root $item.Folder) -Recurse -Filter $item.Name -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "\\$Platform\\$Configuration\\" } |
        Select-Object -First 1

    if ($found) {
        Write-Host "  $($item.Name): $($found.FullName)" -ForegroundColor Green
    } else {
        Write-Warning "  Artifact not found: $($item.Name) ($Configuration|$Platform)"
    }
}

exit 0
