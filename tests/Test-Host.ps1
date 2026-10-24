# SPDX-License-Identifier: MIT
[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86', 'ARM64')][string]$Architecture = 'x64',
    [string]$HostDirectory,
    [string]$Extractor = 'C:\Program Files\7-Zip\7z.exe',
    [switch]$NoPause,
    [switch]$Silent
)
$ErrorActionPreference = 'Stop'
$projectPath = Split-Path $PSScriptRoot -Parent
$temporaryPath = Join-Path ([IO.Path]::GetTempPath()) ('vzip-cli-host-' + [guid]::NewGuid().ToString('N'))
$exitCode = 0
$checks = New-Object 'Collections.Generic.List[string]'
function Run-Host([string[]]$Arguments, [int]$Expected = 0) {
    $hostOutput = (& $script:hostExecutable @Arguments 2>&1) -join "`n"
    if ($LASTEXITCODE -ne $Expected) { throw ('Host exit code {0}, expected {1}: {2}' -f $LASTEXITCODE, $Expected, $hostOutput) }
    return $hostOutput
}
function Run-Cli([string[]]$Arguments, [int]$Expected = 0) {
    $cliOutput = (& $script:cliExecutable @Arguments 2>&1) -join "`n"
    if ($LASTEXITCODE -ne $Expected) { throw ('CLI exit code {0}, expected {1}: {2}' -f $LASTEXITCODE, $Expected, $cliOutput) }
    return $cliOutput
}
try {
    if ($PSVersionTable.PSVersion.Major -eq 5) {
        Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
    }
    $buildPath = Join-Path $projectPath ('.local\msvc-' + $Architecture + '\Release')
    $dllPath = Join-Path $buildPath 'VZip.dll'
    if (!(Test-Path -LiteralPath $dllPath)) { throw 'Build first / 请先构建目标架构。' }
    New-Item -ItemType Directory -Path $temporaryPath | Out-Null
    $privateHost = Join-Path $temporaryPath 'host'
    New-Item -ItemType Directory -Path $privateHost | Out-Null
    if ($HostDirectory) {
        foreach ($name in @('7z.exe', '7z.dll')) {
            Copy-Item -LiteralPath (Join-Path $HostDirectory $name) -Destination $privateHost
        }
    } else {
        if (!(Test-Path -LiteralPath $Extractor)) { throw 'Supply a full 7-Zip extractor / 请指定完整 7-Zip 解包器。' }
        $lock = Get-Content -LiteralPath (Join-Path $projectPath 'dependencies.lock.json') -Raw | ConvertFrom-Json
        $asset = $lock.host.$Architecture
        $installer = Join-Path $temporaryPath $asset.file
        Invoke-WebRequest -Uri ($lock.host.base_url + $asset.file) -OutFile $installer -TimeoutSec 60 -UseBasicParsing
        if ((Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash -ine $asset.sha256) { throw 'Host hash mismatch / 宿主散列不符。' }
        $extraction = (& $Extractor x -y ('-o' + $privateHost) $installer '7z.exe' '7z.dll' 2>&1) -join "`n"
        if ($LASTEXITCODE -ne 0) { throw ('Static host extraction failed / 宿主静态提取失败: ' + $extraction) }
    }
    $script:hostExecutable = Join-Path $privateHost '7z.exe'
    $script:cliExecutable = Join-Path $buildPath 'vzip.exe'
    $fixturePath = Join-Path $temporaryPath 'fixtures'
    & (Join-Path $buildPath 'vzip_tests.exe') --fixtures $fixturePath *> $null
    if ($LASTEXITCODE -ne 0) { throw 'Fixture/core tests failed / 夹具与核心测试失败。' }
    $bare = Run-Host @('i')
    $bareFormatRows = ($bare -split 'Formats:', 2)[-1]
    if ($bareFormatRows -match '(?m)^[^\r\n]*\s(?:VZip|VZstd)\s') { throw 'Host baseline is not isolated / 宿主基线非独立副本。' }
    $checks.Add('clean host baseline')
    New-Item -ItemType Directory -Path (Join-Path $privateHost 'Formats') | Out-Null
    Copy-Item -LiteralPath $dllPath -Destination (Join-Path $privateHost 'Formats\VZip.dll')
    $formats = Run-Host @('i')
    $formatRows = ($formats -split 'Formats:', 2)[-1]
    if ($formatRows -notmatch '(?m)^[^\r\n]*\sVZip\s' -or $formatRows -notmatch '(?m)^[^\r\n]*\sVZstd\s') { throw 'Plugin not loaded / 插件未加载。' }
    $checks.Add('both formats registered')
    foreach ($extension in @('vz', 'vsz')) {
        $source = Join-Path $fixturePath ('sample.' + $extension)
        $listing = Run-Host @('l', '-slt', $source)
        if ($listing -notmatch 'Size = 16') { throw 'Unexpected item listing / 条目列表不符。' }
        $checks.Add($extension + ' list')
        $null = Run-Host @('t', $source)
        $checks.Add($extension + ' test')
        $destination = Join-Path $temporaryPath ('中文 & ! ' + $extension)
        $null = Run-Host @('x', '-y', ('-o' + $destination), $source)
        $files = @(Get-ChildItem -LiteralPath $destination -File)
        if ($files.Count -ne 1 -or (Get-FileHash -LiteralPath $files[0].FullName -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath (Join-Path $fixturePath 'expected.bin') -Algorithm SHA256).Hash) { throw 'Extracted bytes differ / 解压内容不符。' }
        $checks.Add($extension + ' extraction and special output path')
        $bad = Join-Path $fixturePath ('bad-crc.' + $extension)
        $null = Run-Host @('t', $bad) 2
        $checks.Add($extension + ' CRC failure exit code')
        # Content detection must work for Steam names with a suffix after .vz.
        $renamed = Join-Path $fixturePath ('资源 & !.zip.vz.' + $extension + '_123')
        Copy-Item -LiteralPath $source -Destination $renamed
        $null = Run-Host @('t', $renamed)
        $checks.Add($extension + ' signature detection and special input path')
        $inspection = Run-Cli @('inspect', $source)
        if ($inspection -notmatch 'integrity_checked=false') { throw 'Inspection claimed integrity / 结构检查误报完整性。' }
        $checks.Add($extension + ' CLI inspect boundary')
        $null = Run-Cli @('test', $source)
        $checks.Add($extension + ' CLI test')
        $cliOutputPath = Join-Path $temporaryPath ('独立 & ! ' + $extension + '.bin')
        $null = Run-Cli @('extract', $renamed, $cliOutputPath)
        $expectedHash = (Get-FileHash -LiteralPath (Join-Path $fixturePath 'expected.bin') -Algorithm SHA256).Hash
        if ((Get-FileHash -LiteralPath $cliOutputPath -Algorithm SHA256).Hash -ne $expectedHash) { throw 'CLI bytes differ / CLI 内容不符。' }
        $checks.Add($extension + ' CLI extraction and Unicode paths')
        $null = Run-Cli @('extract', $source, $cliOutputPath) 1
        if ((Get-FileHash -LiteralPath $cliOutputPath -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Existing output changed / 已有输出被改变。' }
        $checks.Add($extension + ' CLI refuses overwrite')
        $failedOutputPath = Join-Path $temporaryPath ('failed-' + $extension)
        $null = Run-Cli @('extract', $bad, $failedOutputPath) 1
        if ((Test-Path -LiteralPath $failedOutputPath) -or @(Get-ChildItem -LiteralPath $temporaryPath -Filter '*.vzip-tmp-*').Count) { throw 'Failed CLI output retained / 失败后输出未清理。' }
        $checks.Add($extension + ' CLI failed-output cleanup')
    }
    $null = Run-Cli @('invalid') 2
    $checks.Add('CLI argument exit code')
    $evidencePath = Join-Path $projectPath '.local\test-results'
    New-Item -ItemType Directory -Path $evidencePath -Force | Out-Null
    $record = [ordered]@{ observed_utc = [DateTime]::UtcNow.ToString('o'); architecture = $Architecture; host_banner = ($formats -split "`n" | Where-Object { $_ -match '^7-Zip ' } | Select-Object -First 1); passed = $checks.Count; checks = @($checks.ToArray()); dll_sha256 = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash.ToLowerInvariant(); scope = 'Synthetic vectors; no GUI or Valve client execution' }
    [IO.File]::WriteAllText((Join-Path $evidencePath ('host-' + $Architecture + '.json')), ($record | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
    if (!$Silent) { Write-Host ('Host checks passed / 宿主检查通过: ' + $checks.Count) }
} catch {
    $exitCode = 1
    if (!$Silent) { [Console]::Error.WriteLine($_.Exception.Message) }
} finally {
    if (Test-Path -LiteralPath $temporaryPath) {
        $resolved = [IO.Path]::GetFullPath($temporaryPath)
        $expectedParent = [IO.Path]::GetTempPath().TrimEnd('\')
        if ((Split-Path $resolved -Parent) -ne $expectedParent -or (Split-Path $resolved -Leaf) -notlike 'vzip-cli-host-*' -or
            ((Get-Item -LiteralPath $resolved -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { $exitCode = 1 }
        else {
            try { Remove-Item -LiteralPath $resolved -Recurse -Force } catch { $exitCode = 1; if (!$Silent) { [Console]::Error.WriteLine('Temporary cleanup failed / 临时目录清理失败。') } }
        }
    }
    if (!$NoPause -and !$Silent) { $null = Read-Host 'Enter to finish / 按 Enter 结束' }
}
# 0: all checks and cleanup succeeded; 1: precondition/check/cleanup failed.
exit $exitCode
