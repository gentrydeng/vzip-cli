# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86', 'ARM64')][string]$Architecture = 'x64',
    [switch]$RequireHostTests,
    [switch]$IncludeSource,
    [switch]$NoPause,
    [switch]$Silent
)
$ErrorActionPreference = 'Stop'
$projectPath = Split-Path $PSScriptRoot -Parent
$exitCode = 0
try {
    if ($PSVersionTable.PSVersion.Major -eq 5) {
        Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
    }
    if ($IncludeSource -and $env:GITHUB_ACTIONS -eq 'true') { throw 'Source archives are local-only / 源码压缩包仅在本地生成。' }
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    . (Join-Path $PSScriptRoot 'Package.Common.ps1')
    $build = Get-Content -LiteralPath (Join-Path $projectPath ('.local\build-' + $Architecture + '.json')) -Raw | ConvertFrom-Json
    if ($build.architecture -ne $Architecture) { throw 'Build architecture differs / 构建架构不符。' }
    $buildPath = Join-Path $projectPath ('.local\msvc-' + $Architecture + '\Release')
    $cli = Join-Path $buildPath 'vzip.exe'
    $plugin = Join-Path $buildPath 'VZip.dll'
    if ($build.cli_sha256 -ine (Get-FileHash -LiteralPath $cli -Algorithm SHA256).Hash -or
        $build.plugin_sha256 -ine (Get-FileHash -LiteralPath $plugin -Algorithm SHA256).Hash) {
        throw 'Binaries changed after build / 构建后产物已变化。'
    }
    if ($RequireHostTests) {
        $hostRecord = Get-Content -LiteralPath (Join-Path $projectPath ('.local\test-results\host-' + $Architecture + '.json')) -Raw | ConvertFrom-Json
        if (!$build.tests_run -or $hostRecord.passed -ne 23 -or $hostRecord.dll_sha256 -ine $build.plugin_sha256) {
            throw 'Current binaries lack passing target/host tests / 当前产物缺少通过的目标及宿主测试。'
        }
    }
    $release = Join-Path $projectPath 'release'
    New-Item -ItemType Directory -Path $release -Force | Out-Null
    $noticePath = Join-Path $projectPath ('.local\notices\' + $Architecture)
    foreach ($component in @('cli', 'formats')) {
        $archName = $Architecture.ToLowerInvariant()
        $stage = Join-Path $projectPath ('dist\' + $component + '-' + $archName)
        if (Test-Path -LiteralPath $stage) {
            $items = @(Get-Item -LiteralPath $stage -Force) + @(Get-ChildItem -LiteralPath $stage -Recurse -Force)
            if ($items | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw 'Reparse-point staging directory rejected.' }
            $existing = @(Get-ChildItem -LiteralPath $stage -File -Recurse | ForEach-Object { $_.FullName.Substring($stage.Length + 1).Replace('\', '/') })
            $layout = @(Get-PackageLayout $component)
            if ($existing | Where-Object { $_ -cnotin $layout }) { throw 'Unexpected staging file / 发现非交付文件。' }
        }
        $payload = if ($component -eq 'cli') { $cli } else { $plugin }
        $payloadName = if ($component -eq 'cli') { 'vzip.exe' } else { 'Formats/VZip.dll' }
        $inputs = @(
            @{ Source = $payload; Target = $payloadName },
            @{ Source = (Join-Path $projectPath 'LICENSE'); Target = 'LICENSES/LICENSE' },
            @{ Source = (Join-Path $noticePath 'LZMA-SDK-NOTICE.txt'); Target = 'LICENSES/LZMA-SDK-NOTICE.txt' },
            @{ Source = (Join-Path $noticePath 'ZSTANDARD-LICENSE.txt'); Target = 'LICENSES/ZSTANDARD-LICENSE.txt' },
            @{ Source = (Join-Path $projectPath 'packaging\NOTICE.md'); Target = 'LICENSES/NOTICE.md' },
            @{ Source = (Join-Path $projectPath 'packaging\NOTICE.zh-CN.md'); Target = 'LICENSES/NOTICE.zh-CN.md' },
            @{ Source = (Join-Path $projectPath ('packaging\' + $component + '\README.md')); Target = 'README/README.md' },
            @{ Source = (Join-Path $projectPath ('packaging\' + $component + '\README.zh-CN.md')); Target = 'README/README.zh-CN.md' }
        )
        $files = @()
        foreach ($item in $inputs) {
            $target = Join-Path $stage $item.Target
            New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
            [IO.File]::Copy($item.Source, $target, $true)
            $files += @{ Source = $target; Target = $item.Target }
        }
        $package = Join-Path $release ('vzip-' + $component + '-' + $archName + '.zip')
        Write-VerifiedZip $package $files
        Assert-PackageArchive $package $component $archName
    }
    if ($IncludeSource) {
        $files = @()
        foreach ($name in @('.gitattributes', '.gitignore', 'CHANGELOG.md', 'CMakeLists.txt', 'LICENSE', 'README.md', 'README.zh-CN.md',
            'THIRD-PARTY-NOTICES.md', 'THIRD-PARTY-NOTICES.zh-CN.md', 'dependencies.lock.json')) {
            $files += @{ Source = (Join-Path $projectPath $name); Target = 'vzip-cli/' + $name }
        }
        foreach ($directory in @('.github', 'src', 'tests', 'scripts', 'docs', 'packaging')) {
            foreach ($file in Get-ChildItem -LiteralPath (Join-Path $projectPath $directory) -File -Recurse -Force) {
                $files += @{ Source = $file.FullName; Target = 'vzip-cli/' + $file.FullName.Substring($projectPath.Length + 1).Replace('\', '/') }
            }
        }
        Write-VerifiedZip (Join-Path $release 'source.zip') $files
    }
    if (!$Silent) { Write-Host ('CLI and Formats packages verified / CLI 与 Formats 包已校验: ' + $Architecture) }
} catch {
    $exitCode = 1
    if (!$Silent) { [Console]::Error.WriteLine($_.Exception.Message) }
} finally { if (!$NoPause -and !$Silent) { $null = Read-Host 'Enter to finish / 按 Enter 结束' } }
# 0: packages verified; 1: build/validation/packaging failure.
exit $exitCode
