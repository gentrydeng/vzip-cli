# SPDX-License-Identifier: MIT
# Workflow helper: validate the six binary packages and render release material.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Repository,
    [Parameter(Mandatory = $true)][string]$Tag,
    [Parameter(Mandatory = $true)][string]$SourceSha,
    [Parameter(Mandatory = $true)][string]$RunId,
    [Parameter(Mandatory = $true)][string]$PackageDirectory,
    [Parameter(Mandatory = $true)][string]$NotesPath,
    [string]$ChecksumPath
)
$ErrorActionPreference = 'Stop'
$projectPath = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if ($PSVersionTable.PSVersion.Major -eq 5) {
    Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
}
if ($Repository -notmatch '^[A-Za-z0-9][A-Za-z0-9-]*/[A-Za-z0-9_.-]+$' -or
    $Tag -notmatch '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z][0-9A-Za-z.-]*)?(\+[0-9A-Za-z][0-9A-Za-z.-]*)?$' -or
    $SourceSha -notmatch '^[0-9a-f]{40}$' -or $RunId -notmatch '^[1-9][0-9]*$') {
    throw 'Invalid release identity.'
}
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
. (Join-Path $projectPath 'scripts\Package.Common.ps1')
$expected = @()
foreach ($architecture in @('x64', 'x86', 'arm64')) {
    foreach ($component in @('cli', 'formats')) {
        $name = 'vzip-' + $component + '-' + $architecture + '.zip'
        Assert-PackageArchive (Join-Path $PackageDirectory $name) $component $architecture
        $expected += $name
    }
}
$actual = @(Get-ChildItem -LiteralPath $PackageDirectory -File -Recurse | ForEach-Object { $_.Name } | Sort-Object)
if ($actual.Count -ne 6 -or (Compare-Object @($expected | Sort-Object) $actual -CaseSensitive)) {
    throw 'Release input must contain exactly the six expected ZIP files.'
}
$base = 'https://github.com/' + $Repository
$download = $base + '/releases/download/' + [Uri]::EscapeDataString($Tag)
$sourceDownload = $base + '/archive/refs/tags/' + [Uri]::EscapeDataString($Tag)
$englishReadme = $base + '/blob/' + $SourceSha + '/README.md'
$chineseReadme = $base + '/blob/' + $SourceSha + '/README.zh-CN.md'
$cliLinks = @()
$formatsLinks = @()
foreach ($architecture in @('x64', 'x86', 'arm64')) {
    $cli = 'vzip-cli-' + $architecture + '.zip'
    $formats = 'vzip-formats-' + $architecture + '.zip'
    $label = if ($architecture -eq 'arm64') { 'ARM64' } else { $architecture }
    $cliLinks += '[' + $label + '](' + $download + '/' + $cli + ')'
    $formatsLinks += '[' + $label + '](' + $download + '/' + $formats + ')'
}
$cliDownloads = $cliLinks -join ' · '
$formatsDownloads = $formatsLinks -join ' · '
$changelog = Get-Content -LiteralPath (Join-Path $projectPath 'CHANGELOG.md') -Raw -Encoding UTF8
$section = '(?ms)^## ' + [regex]::Escape($Tag) + '\r?\n(?<entry>.*?)(?=^## |\z)'
$entry = [regex]::Match($changelog, $section)
if (!$entry.Success -or [string]::IsNullOrWhiteSpace($entry.Groups['entry'].Value)) {
    throw 'Add the version entry to CHANGELOG.md before preparing a release.'
}
$changes = $entry.Groups['entry'].Value.Trim()
$notes = @"
## 更新日志 / Changelog

$changes

## 下载与使用 / Download and usage

本版本读取 VZa 和 VSZa，暂不支持 VZd 或创建／修改压缩包。
This version reads VZa and VSZa; VZd and archive creation/update are not supported.

完整说明与限制请参考[仓库中文 README]($chineseReadme)。
See the [repository README]($englishReadme) for full instructions and limitations.

按用途选择组件，再选择架构；源码归档由 GitHub 自动提供。
Choose a component by purpose, then its architecture; GitHub provides source archives separately.

### 独立命令行工具 / Standalone CLI (vzip.exe)

$cliDownloads

### 7-Zip 格式插件 / 7-Zip Formats plugin (VZip.dll)

$formatsDownloads

CLI 选择与 Windows 匹配的架构；Formats 必须匹配 7-Zip 进程的架构，即使操作系统是 64 位。
Match CLI architecture to Windows; match Formats to the 7-Zip process, even on a 64-bit OS.

### 源码下载 / Source downloads

[ZIP]($sourceDownload.zip) · [tar.gz]($sourceDownload.tar.gz)

使用 CLI：完整解压 CLI 包，在包根目录打开 PowerShell，按下列示例运行。
Use the CLI: extract the entire CLI package and run these examples in PowerShell from its root.

``````powershell
.\vzip.exe test 'example.zip.vz'
.\vzip.exe extract 'example.zip.vz' 'payload.zip'
``````

安装 Formats：完整解压对应包，把 Formats 文件夹复制／合并到 7-Zip 安装目录，关闭旧 7-Zip 进程后重新打开。Program Files 下的复制可能需要管理员权限；替换已有插件前先检查旧版本。
Install Formats: extract the matching package and copy/merge its Formats folder into the 7-Zip installation. Close existing 7-Zip processes and reopen it. Copying under Program Files may require administrator rights; review an installed plugin before replacing it.

包内 README 文件夹提供对应组件的中英文用法，LICENSES 文件夹保留许可和版权通知。
Each package's README folder contains component-specific English/Chinese usage, and LICENSES retains licenses and copyright notices.

SHA256SUMS 只包含上述六个二进制 ZIP 的 SHA-256，不包含 GitHub 自动生成的源码归档。
SHA256SUMS covers only the six binary ZIPs above, excluding GitHub-generated source archives.

以 x64 CLI 为例：下载 [vzip-cli-x64.zip]($download/vzip-cli-x64.zip) 和 [SHA256SUMS]($download/SHA256SUMS)，保存到同一文件夹，在该文件夹打开 PowerShell。
For the x64 CLI, download [vzip-cli-x64.zip]($download/vzip-cli-x64.zip) and [SHA256SUMS]($download/SHA256SUMS) into the same folder and open PowerShell there.

``````powershell
Get-FileHash .\vzip-cli-x64.zip -Algorithm SHA256
Select-String -LiteralPath .\SHA256SUMS -SimpleMatch 'vzip-cli-x64.zip'
``````

比较计算结果与清单中的 SHA-256 值，忽略字母大小写；两者应完全一致。
Compare the computed SHA-256 with the value in the list, ignoring letter case; they must match exactly.
"@
New-Item -ItemType Directory -Path (Split-Path ([IO.Path]::GetFullPath($NotesPath)) -Parent) -Force | Out-Null
[IO.File]::WriteAllText($NotesPath, $notes.Replace("`r`n", "`n") + "`n", [Text.UTF8Encoding]::new($false))
if ($ChecksumPath) {
    $lines = @($expected | Sort-Object | ForEach-Object {
        '{0}  {1}' -f (Get-FileHash -LiteralPath (Join-Path $PackageDirectory $_) -Algorithm SHA256).Hash.ToLowerInvariant(), $_
    })
    New-Item -ItemType Directory -Path (Split-Path ([IO.Path]::GetFullPath($ChecksumPath)) -Parent) -Force | Out-Null
    [IO.File]::WriteAllLines($ChecksumPath, $lines, [Text.UTF8Encoding]::new($false))
}
Write-Host 'Release inputs validated and bilingual notes prepared / 已验证发布输入并生成双语说明。'
