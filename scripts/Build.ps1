# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86', 'ARM64')][string]$Architecture = 'x64',
    [string]$SdkSource,
    [string]$ZstdSource,
    [switch]$RequireTests,
    [switch]$SkipPackage,
    [switch]$IncludeSource,
    [switch]$NoPause,
    [switch]$Silent
)
$ErrorActionPreference = 'Stop'
$projectPath = Split-Path $PSScriptRoot -Parent
$exitCode = 0
function Status([string]$Message) { if (!$Silent) { Write-Host $Message } }
function Run-Checked([string]$Executable, [string[]]$Arguments) {
    if ($Silent) { & $Executable @Arguments *> $null } else { & $Executable @Arguments }
    if ($LASTEXITCODE -ne 0) { throw ('Command failed / 命令失败: {0} ({1})' -f [IO.Path]::GetFileName($Executable), $LASTEXITCODE) }
}
try {
    if ($PSVersionTable.PSVersion.Major -eq 5) {
        Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
    }
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path -LiteralPath $vswherePath)) { throw 'Visual Studio Build Tools not found / 未发现 Visual Studio 生成工具。' }
    $probe = [Diagnostics.ProcessStartInfo]::new($vswherePath)
    $probe.Arguments = '-latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json -utf8'
    $probe.UseShellExecute = $false
    $probe.CreateNoWindow = $true
    $probe.RedirectStandardOutput = $true
    $probe.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $probe
    $null = $process.Start()
    $installationJson = $process.StandardOutput.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw 'Visual Studio discovery failed / Visual Studio 查找失败。' }
    $process.Dispose()
    $installation = ($installationJson | ConvertFrom-Json) | Select-Object -First 1
    if (!$installation) { throw 'MSVC C++ tools not found / 未发现 MSVC C++ 工具。' }
    $major = ([version]$installation.installationVersion).Major
    $generator = switch ($major) { 17 { 'Visual Studio 17 2022' } 18 { 'Visual Studio 18 2026' } default { throw 'Unsupported Visual Studio generator / 不支持此 Visual Studio 版本。' } }
    $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    $cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } else { Join-Path $installation.installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }
    if (!(Test-Path -LiteralPath $cmakePath)) { throw 'CMake is required / 需要 CMake。' }
    $platform = if ($Architecture -eq 'x86') { 'Win32' } else { $Architecture }
    $buildPath = Join-Path $projectPath ('.local\msvc-' + $Architecture)
    $arguments = @('-S', $projectPath, '-B', $buildPath, '-G', $generator, '-A', $platform,
        ('-DCMAKE_GENERATOR_INSTANCE=' + $installation.installationPath + ',version=' + $installation.installationVersion))
    if ($SdkSource) { $arguments += '-DFETCHCONTENT_SOURCE_DIR_LZMASDK=' + [IO.Path]::GetFullPath($SdkSource) }
    if ($ZstdSource) { $arguments += '-DFETCHCONTENT_SOURCE_DIR_ZSTD=' + [IO.Path]::GetFullPath($ZstdSource) }
    Status ('Configure / 配置: ' + $Architecture)
    Run-Checked $cmakePath $arguments
    Run-Checked $cmakePath @('--build', $buildPath, '--config', 'Release', '--parallel', '4')
    $hostArchitecture = $env:PROCESSOR_ARCHITEW6432
    if (!$hostArchitecture) { $hostArchitecture = $env:PROCESSOR_ARCHITECTURE }
    $canRun = ($Architecture -eq 'ARM64' -and $hostArchitecture -eq 'ARM64') -or
        ($Architecture -eq 'x64' -and $hostArchitecture -eq 'AMD64') -or
        ($Architecture -eq 'x86' -and $hostArchitecture -in @('AMD64', 'x86'))
    if ($canRun) {
        $ctestPath = Join-Path (Split-Path $cmakePath -Parent) 'ctest.exe'
        Run-Checked $ctestPath @('--test-dir', $buildPath, '-C', 'Release', '--output-on-failure')
    } elseif ($RequireTests) {
        throw 'Target cannot run on this host / 此宿主不能运行目标架构测试。'
    } else {
        Status 'Cross-compiled; runtime tests skipped / 已交叉编译，未运行目标架构测试。'
    }
    $sdkPath = if ($SdkSource) { $SdkSource } else { Join-Path $buildPath '_deps\lzmasdk-src' }
    $zstdPath = if ($ZstdSource) { $ZstdSource } else { Join-Path $buildPath '_deps\zstd-src' }
    $noticePath = Join-Path $projectPath ('.local\notices\' + $Architecture)
    New-Item -ItemType Directory -Path $noticePath -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $sdkPath 'DOC\lzma-sdk.txt') -Destination (Join-Path $noticePath 'LZMA-SDK-NOTICE.txt') -Force
    Copy-Item -LiteralPath (Join-Path $zstdPath 'LICENSE') -Destination (Join-Path $noticePath 'ZSTANDARD-LICENSE.txt') -Force
    $record = [ordered]@{
        architecture = $Architecture
        configuration = 'Release'
        tests_run = [bool]$canRun
        host_architecture = $hostArchitecture
        msvc_installation_version = $installation.installationVersion
        sdk = '26.05'
        zstd = '1.5.7'
        cli_sha256 = (Get-FileHash -LiteralPath (Join-Path $buildPath 'Release\vzip.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
        plugin_sha256 = (Get-FileHash -LiteralPath (Join-Path $buildPath 'Release\VZip.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    [IO.File]::WriteAllText((Join-Path $projectPath ('.local\build-' + $Architecture + '.json')), ($record | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
    if ($SkipPackage -and $IncludeSource) { throw 'IncludeSource requires packaging / IncludeSource 需要执行打包。' }
    if (!$SkipPackage) {
        $packageArguments = @{ Architecture = $Architecture; NoPause = $true; Silent = $Silent; IncludeSource = $IncludeSource }
        & (Join-Path $PSScriptRoot 'Package.ps1') @packageArguments
        if ($LASTEXITCODE -ne 0) { throw 'Packaging failed / 打包失败。' }
    }
    Status ('Built / 构建完成: ' + $Architecture)
} catch {
    $exitCode = 1
    if (!$Silent) { [Console]::Error.WriteLine($_.Exception.Message) }
} finally {
    if (!$NoPause -and !$Silent) { $null = Read-Host 'Enter to finish / 按 Enter 结束' }
}
# 0: build succeeded (tests_run states runtime coverage); 1: build/test/package failure.
# Parameter-binding errors occur before this body and normally return 1.
exit $exitCode
