# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

<#
.SYNOPSIS
    Loads the Visual Studio x64 developer environment into the current PowerShell session.

.DESCRIPTION
    Command-line builds use Ninja + MSVC, which need the developer environment (cl.exe,
    link.exe, Windows SDK). Dot-source this script once per terminal, then use the presets:

        . .\scripts\devShell.ps1
        cmake --preset dev-windows-msvc
        cmake --build --preset dev-windows-msvc
        ctest --preset dev-windows-msvc

    Visual Studio, VS Code (CMake Tools) and CLion load the environment by themselves.

    The presets find vcpkg through VCPKG_ROOT: the one set before, such as the CI's checkout at the
    manifest's baseline, else the copy bundled with Visual Studio.
#>
[CmdletBinding()]
param()

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. Install Visual Studio 2026 with the 'Desktop development with C++' workload."
}

$vsPath = & $vswhere -latest -prerelease -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw 'No Visual Studio installation with the MSVC x64 toolset was found.'
}

# VsDevCmd calls vswhere by name; make it resolvable to keep the output clean.
$env:PATH = "$(Split-Path $vswhere);$env:PATH"

# VsDevCmd sets VCPKG_ROOT to the copy bundled with VS, over the one set before it: a vcpkg without
# its ports, which fetches the registry from GitHub at every configure. The one set before is kept.
$vcpkgRootBefore = $env:VCPKG_ROOT

Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

# The presets resolve the vcpkg toolchain through VCPKG_ROOT: the one set before, else VS's copy.
$env:VCPKG_ROOT = if ($vcpkgRootBefore) { $vcpkgRootBefore } else { Join-Path $vsPath 'VC\vcpkg' }

Write-Host "Visual Studio : $vsPath"
Write-Host "CMake         : $((cmake --version | Select-Object -First 1))"
Write-Host "vcpkg         : $env:VCPKG_ROOT"
