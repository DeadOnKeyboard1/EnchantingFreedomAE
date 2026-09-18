# Copyright (C) 2026 DeadOnKeyboard
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [string]$VcpkgRoot,
    [string]$CommonLibSSEPath,
    [string]$DependencyPrefix,
    [switch]$Clean,
    [switch]$NoPackage
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$buildDirectory = Join-Path $projectRoot 'build-ninja'
$distDirectory = Join-Path $projectRoot 'dist'

function Find-ExistingDirectory {
    param([string[]]$Candidates)
    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
        try {
            if (Test-Path -LiteralPath $candidate -PathType Container) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        } catch {}
    }
    return $null
}

function Test-CommonLibSSEV5Support {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) { return $false }

    $versionHeader = Join-Path $Path 'include\SKSE\Version.h'
    $idDb = Join-Path $Path 'src\REL\IDDB.cpp'
    $interfaces = Join-Path $Path 'include\SKSE\Interfaces.h'

    foreach ($file in @($versionHeader, $idDb, $interfaces)) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            return $false
        }
    }

    $versionText = Get-Content -LiteralPath $versionHeader -Raw
    $idDbText = Get-Content -LiteralPath $idDb -Raw
    $interfacesText = Get-Content -LiteralPath $interfaces -Raw

    return (
        $versionText.Contains('RUNTIME_SSE_1_7_104') -and
        $idDbText.Contains('case 5:') -and
        $idDbText.Contains('SSEv5') -and
        $interfacesText.Contains('kVersionIndependentEx_AddressLibraryV5')
    )
}

function Find-VsWhere {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return $candidate
        }
    }
    return $null
}

function Import-MsvcEnvironment {
    $vswhere = Find-VsWhere
    if (-not $vswhere) { return $null }

    $installationPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath |
        Select-Object -First 1)
    if (-not $installationPath) { return $null }

    $vsDevCmd = Join-Path $installationPath 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path -LiteralPath $vsDevCmd -PathType Leaf)) {
        return $installationPath
    }

    $command = '"{0}" -no_logo -arch=x64 -host_arch=x64 >nul && set' -f $vsDevCmd
    $environment = & cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "Visual Studio developer environment could not be initialized."
    }

    foreach ($line in $environment) {
        $separator = $line.IndexOf('=')
        if ($separator -le 0) { continue }
        Set-Item -Path ("Env:" + $line.Substring(0, $separator)) -Value $line.Substring($separator + 1)
    }

    return $installationPath
}

Write-Host '=== Enchanting Freedom AE 1.3.0 Complete Freedom Build ===' -ForegroundColor Cyan
Write-Host 'Targets: Skyrim 1.6.1170 and 1.7.104.0 (single Address Library DLL)'
Write-Host "Source: $projectRoot"

$cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $cmake) { $cmake = Get-Command cmake -ErrorAction SilentlyContinue }
if (-not $cmake) { throw 'CMake 3.25 or newer was not found in PATH.' }

$vsInstallation = $null
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Write-Host 'MSVC is not in PATH; trying Visual Studio x64 developer environment...'
    $vsInstallation = Import-MsvcEnvironment
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'MSVC cl.exe was not found.'
}

if (-not (Get-Command ninja.exe -ErrorAction SilentlyContinue) -and
    -not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    if (-not $vsInstallation) {
        $vswhere = Find-VsWhere
        if ($vswhere) {
            $vsInstallation = (& $vswhere -latest -products * -property installationPath | Select-Object -First 1)
        }
    }
    if ($vsInstallation) {
        $ninja = Join-Path $vsInstallation 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
        if (Test-Path -LiteralPath $ninja -PathType Leaf) {
            $env:PATH = (Split-Path -Parent $ninja) + [IO.Path]::PathSeparator + $env:PATH
        }
    }
}
if (-not (Get-Command ninja.exe -ErrorAction SilentlyContinue) -and
    -not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    throw 'Ninja was not found.'
}

if (-not $VcpkgRoot) {
    $VcpkgRoot = Find-ExistingDirectory -Candidates @(
        $env:VCPKG_ROOT,
        (Join-Path $projectRoot '..\_toolchain\vcpkg'),
        (Join-Path $projectRoot '_toolchain\vcpkg'),
        'C:\vcpkg',
        (Join-Path $env:USERPROFILE 'vcpkg')
    )
}
if (-not $VcpkgRoot) {
    throw 'vcpkg was not found. Pass -VcpkgRoot or set VCPKG_ROOT.'
}
$VcpkgRoot = (Resolve-Path -LiteralPath $VcpkgRoot).Path
$vcpkgToolchain = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path -LiteralPath $vcpkgToolchain -PathType Leaf)) {
    throw "vcpkg toolchain file was not found: $vcpkgToolchain"
}

$PinnedCommonLibCommit = '1504349dddfc622d4d25704bba19e2ade669dc5a'

# Reproducible build rule:
# Do NOT auto-select an arbitrary CommonLib checkout from _toolchain.
# If the caller explicitly supplies -CommonLibSSEPath, accept it only when its
# git HEAD is exactly the pinned revision. Otherwise CMake FetchContent clones
# the pinned revision from CMakeLists.txt.
if ($CommonLibSSEPath) {
    $CommonLibSSEPath = (Resolve-Path -LiteralPath $CommonLibSSEPath).Path

    if (-not (Test-Path -LiteralPath (Join-Path $CommonLibSSEPath 'CMakeLists.txt') -PathType Leaf)) {
        throw "CommonLibSSE-NG was not found at '$CommonLibSSEPath'."
    }

    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if (-not $git) { $git = Get-Command git -ErrorAction SilentlyContinue }
    if (-not $git) {
        throw 'Git is required to verify an explicitly supplied CommonLibSSE-NG checkout.'
    }

    $actualCommit = (& $git.Path -C $CommonLibSSEPath rev-parse HEAD 2>$null).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($actualCommit)) {
        throw "Could not read the CommonLibSSE-NG git revision at '$CommonLibSSEPath'."
    }

    if ($actualCommit.ToLowerInvariant() -ne $PinnedCommonLibCommit) {
        throw "Wrong CommonLibSSE-NG revision. Expected $PinnedCommonLibCommit but found $actualCommit. Remove -CommonLibSSEPath and let CMake fetch the pinned revision."
    }

    Write-Host "Using exact pinned CommonLibSSE-NG: $CommonLibSSEPath ($actualCommit)" -ForegroundColor Green
} else {
    Write-Host "Using exact pinned CommonLibSSE-NG commit $PinnedCommonLibCommit via CMake FetchContent." -ForegroundColor Green
}

if ($Clean) {
    if (Test-Path -LiteralPath $buildDirectory) { Remove-Item -LiteralPath $buildDirectory -Recurse -Force }
    if (Test-Path -LiteralPath $distDirectory) { Remove-Item -LiteralPath $distDirectory -Recurse -Force }
}

$configureArgs = @(
    '-S', $projectRoot,
    '-B', $buildDirectory,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain",
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md'
)
if ($CommonLibSSEPath) { $configureArgs += "-DCOMMONLIBSSE_PATH=$CommonLibSSEPath" }
if ($DependencyPrefix) { $configureArgs += "-DDEPENDENCY_PREFIX=$DependencyPrefix" }

Write-Host 'Configuring...' -ForegroundColor Cyan
& $cmake.Path @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE." }

Write-Host 'Building Release...' -ForegroundColor Cyan
& $cmake.Path --build $buildDirectory --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }

if (-not $NoPackage) {
    Write-Host 'Creating Vortex/MO2 package...' -ForegroundColor Cyan
    & (Join-Path $projectRoot 'tools\Package.ps1') -BuildDirectory $buildDirectory -OutputDirectory $distDirectory -Force
    if ($LASTEXITCODE -ne 0) { throw "Packaging failed with exit code $LASTEXITCODE." }
}

Write-Host 'BUILD SUCCESSFUL' -ForegroundColor Green
