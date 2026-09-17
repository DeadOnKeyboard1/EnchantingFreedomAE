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
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        try {
            if (Test-Path -LiteralPath $candidate -PathType Container) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        } catch {}
    }
    return $null
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
    if (-not $vswhere) {
        return $null
    }

    $installationPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath |
        Select-Object -First 1)
    if (-not $installationPath) {
        return $null
    }

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
        if ($separator -le 0) {
            continue
        }
        $name = $line.Substring(0, $separator)
        $value = $line.Substring($separator + 1)
        Set-Item -Path "Env:$name" -Value $value
    }

    return $installationPath
}

Write-Host '=== Enchanting Freedom AE 1.2.1 Universal Build ===' -ForegroundColor Cyan
Write-Host 'Targets: Skyrim 1.6.1170 and 1.7.104.0 (single Address Library DLL)'
Write-Host "Source: $projectRoot"

$cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $cmake) {
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
}
if (-not $cmake) {
    throw 'CMake 3.25 or newer was not found in PATH.'
}

$vsInstallation = $null
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Write-Host 'MSVC is not in PATH; trying to initialize the Visual Studio x64 developer environment...'
    $vsInstallation = Import-MsvcEnvironment
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'MSVC cl.exe was not found. Install Visual Studio/Build Tools with Desktop development with C++.'
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
    throw 'Ninja was not found. Install Ninja or add it to PATH.'
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
    throw 'vcpkg was not found. Pass -VcpkgRoot "C:\path\to\vcpkg" or set VCPKG_ROOT.'
}
$VcpkgRoot = (Resolve-Path -LiteralPath $VcpkgRoot).Path
$vcpkgToolchain = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path -LiteralPath $vcpkgToolchain -PathType Leaf)) {
    throw "vcpkg toolchain file was not found: $vcpkgToolchain"
}
Write-Host "vcpkg: $VcpkgRoot"

if (-not $CommonLibSSEPath) {
    $CommonLibSSEPath = Find-ExistingDirectory -Candidates @(
        (Join-Path $projectRoot '..\_toolchain\CommonLibSSE-NG'),
        (Join-Path $projectRoot '..\_toolchain\CommonLibSSE'),
        (Join-Path $projectRoot '..\_toolchain\CommonLibVR'),
        (Join-Path $projectRoot '_toolchain\CommonLibSSE-NG'),
        (Join-Path $projectRoot '_toolchain\CommonLibSSE'),
        (Join-Path $projectRoot '_toolchain\CommonLibVR')
    )
}
if ($CommonLibSSEPath) {
    $CommonLibSSEPath = (Resolve-Path -LiteralPath $CommonLibSSEPath).Path
    if (-not (Test-Path -LiteralPath (Join-Path $CommonLibSSEPath 'CMakeLists.txt') -PathType Leaf)) {
        throw "COMMONLIBSSE_PATH does not contain CMakeLists.txt: $CommonLibSSEPath"
    }
    Write-Host "CommonLibSSE-NG: $CommonLibSSEPath"
} else {
    Write-Host 'CommonLibSSE-NG: no local checkout detected; CMake FetchContent will download the pinned source.' -ForegroundColor Yellow
}

if ($Clean) {
    Write-Host 'Cleaning previous build/dist folders...'
    if (Test-Path -LiteralPath $buildDirectory) {
        Remove-Item -LiteralPath $buildDirectory -Recurse -Force
    }
    if (Test-Path -LiteralPath $distDirectory) {
        Remove-Item -LiteralPath $distDirectory -Recurse -Force
    }
}

$configureArgs = @(
    '-S', $projectRoot,
    '-B', $buildDirectory,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain",
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md'
)
if ($CommonLibSSEPath) {
    $configureArgs += "-DCOMMONLIBSSE_PATH=$CommonLibSSEPath"
}
if ($DependencyPrefix) {
    $configureArgs += "-DDEPENDENCY_PREFIX=$DependencyPrefix"
}

Write-Host ''
Write-Host 'Configuring...' -ForegroundColor Cyan
& $cmake.Path @configureArgs
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

Write-Host ''
Write-Host 'Building Release...' -ForegroundColor Cyan
& $cmake.Path --build $buildDirectory --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE."
}

$dllCandidates = @(
    (Join-Path $buildDirectory 'EnchantingFreedomAE.dll'),
    (Join-Path $buildDirectory 'Release\EnchantingFreedomAE.dll')
)
$dll = $dllCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $dll) {
    $dll = Get-ChildItem -LiteralPath $buildDirectory -Recurse -File -Filter 'EnchantingFreedomAE.dll' |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $dll) {
    throw 'Build completed but EnchantingFreedomAE.dll was not found.'
}

Write-Host ''
Write-Host "DLL: $dll" -ForegroundColor Green

if (-not $NoPackage) {
    Write-Host 'Creating Vortex/MO2 package...' -ForegroundColor Cyan
    $packageScript = Join-Path $projectRoot 'tools\Package.ps1'
    & $packageScript -BuildDirectory $buildDirectory -OutputDirectory $distDirectory -Force
    if ($LASTEXITCODE -ne 0) {
        throw "Packaging failed with exit code $LASTEXITCODE."
    }
}

Write-Host ''
Write-Host 'BUILD SUCCESSFUL' -ForegroundColor Green
Write-Host 'Use the same DLL on Skyrim 1.6.1170 and 1.7.104.0 with the matching SKSE + Address Library.'
