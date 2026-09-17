# Copyright (C) 2026 <COMPUTER_NAME>
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$resolvedBuild = (Resolve-Path -LiteralPath $BuildDirectory).Path
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot 'dist'
}

function Resolve-BuildArtifact {
    param([Parameter(Mandatory = $true)][string]$Name)

    $candidates = @(
        (Join-Path $resolvedBuild $Name),
        (Join-Path (Join-Path $resolvedBuild 'Release') $Name)
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $found = Get-ChildItem -LiteralPath $resolvedBuild -Recurse -File -Filter $Name -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '[\\/]CMakeFiles[\\/]' } |
        Select-Object -First 1
    if ($found) {
        return $found.FullName
    }

    throw "Required build artifact was not found: $Name"
}

$dll = Resolve-BuildArtifact -Name 'EnchantingFreedomAE.dll'
$pdb = Resolve-BuildArtifact -Name 'EnchantingFreedomAE.pdb'
$ini = Join-Path $projectRoot 'config\EnchantingFreedomAE.ini'
$license = Join-Path $projectRoot 'LICENSE'
$notice = Join-Path $projectRoot 'NOTICE.md'
foreach ($file in $ini, $license, $notice) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Required package file was not found: $file"
    }
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$resolvedOutput = (Resolve-Path -LiteralPath $OutputDirectory).Path
$output = Join-Path $resolvedOutput 'EnchantingFreedomAE-1.2.1-SE-AE-Universal.zip'
if (Test-Path -LiteralPath $output) {
    if ($Force) {
        Remove-Item -LiteralPath $output -Force
    } else {
        throw "Refusing to overwrite existing release: $output (use -Force to replace it)"
    }
}

$staging = Join-Path $resolvedOutput ('.staging-' + [guid]::NewGuid().ToString('N'))
$outputPrefix = $resolvedOutput.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $staging.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Invalid staging path: $staging"
}

try {
    $plugins = Join-Path $staging 'SKSE\Plugins'
    $docs = Join-Path $staging 'Docs\EnchantingFreedomAE'
    New-Item -ItemType Directory -Path $plugins, $docs -Force | Out-Null

    Copy-Item -LiteralPath $dll, $pdb -Destination $plugins
    Copy-Item -LiteralPath $ini -Destination (Join-Path $plugins 'EnchantingFreedomAE.ini')
    Copy-Item -LiteralPath $license -Destination (Join-Path $docs 'GPL-3.0-or-later.txt')
    Copy-Item -LiteralPath $notice -Destination (Join-Path $docs 'NOTICE.txt')

    Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $output -CompressionLevel Optimal
}
finally {
    if (Test-Path -LiteralPath $staging) {
        Remove-Item -LiteralPath $staging -Recurse -Force
    }
}

Write-Host "Package created:" -ForegroundColor Green
Write-Host $output
Write-Output $output
