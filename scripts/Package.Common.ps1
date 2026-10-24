# SPDX-License-Identifier: MIT
# Shared implementation for package creation and release validation.
function Get-PackageLayout([string]$Component) {
    $payload = if ($Component -eq 'cli') { 'vzip.exe' } elseif ($Component -eq 'formats') { 'Formats/VZip.dll' } else { throw 'Unknown package component.' }
    return @($payload, 'LICENSES/LICENSE', 'LICENSES/LZMA-SDK-NOTICE.txt', 'LICENSES/ZSTANDARD-LICENSE.txt',
        'LICENSES/NOTICE.md', 'LICENSES/NOTICE.zh-CN.md', 'README/README.md', 'README/README.zh-CN.md')
}

function Get-PeArchitecture([byte[]]$Bytes) {
    if ($Bytes.Length -lt 64 -or $Bytes[0] -ne 0x4d -or $Bytes[1] -ne 0x5a) { throw 'Invalid PE header.' }
    $offset = [BitConverter]::ToInt32($Bytes, 60)
    if ($offset -lt 64 -or $offset -gt $Bytes.Length - 6 -or [BitConverter]::ToUInt32($Bytes, $offset) -ne 0x4550) {
        throw 'Invalid PE signature.'
    }
    switch ([BitConverter]::ToUInt16($Bytes, $offset + 4)) {
        0x014c { return 'x86' }
        0x8664 { return 'x64' }
        0xaa64 { return 'arm64' }
        default { throw 'Unsupported PE architecture.' }
    }
}

function Assert-PackageArchive([string]$Path, [string]$Component, [string]$Architecture) {
    $expected = @(Get-PackageLayout $Component | Sort-Object)
    $archive = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $names = @($archive.Entries | ForEach-Object { $_.FullName } | Sort-Object)
        if ($names.Count -ne $expected.Count -or (Compare-Object $expected $names -CaseSensitive)) {
            throw ('Package layout differs: ' + [IO.Path]::GetFileName($Path))
        }
        foreach ($entry in $archive.Entries) {
            if ($entry.Length -gt 64MB) { throw 'Package entry exceeds the validation limit.' }
        }
        $payload = if ($Component -eq 'cli') { 'vzip.exe' } else { 'Formats/VZip.dll' }
        $inputStream = $archive.GetEntry($payload).Open()
        $buffer = [IO.MemoryStream]::new()
        try {
            $inputStream.CopyTo($buffer)
            if ((Get-PeArchitecture $buffer.ToArray()) -ne $Architecture.ToLowerInvariant()) { throw 'Package PE architecture differs.' }
        } finally { $inputStream.Dispose(); $buffer.Dispose() }
    } finally { $archive.Dispose() }
}

function Write-VerifiedZip([string]$Destination, [object[]]$Files) {
    $temporary = $Destination + '.tmp-' + [guid]::NewGuid().ToString('N')
    try {
        $archive = [IO.Compression.ZipFile]::Open($temporary, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($file in $Files) {
                if ((Get-Item -LiteralPath $file.Source -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                    throw 'Reparse-point package input rejected.'
                }
                if ($file.Target -match '(^/|\\|(^|/)\.\.(/|$))') { throw 'Invalid ZIP entry path.' }
                $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.Source, $file.Target)
            }
        } finally { $archive.Dispose() }
        $check = [IO.Compression.ZipFile]::OpenRead($temporary)
        try {
            if ($check.Entries.Count -ne $Files.Count) { throw 'ZIP entry count differs.' }
            foreach ($file in $Files) {
                $entry = $check.GetEntry($file.Target)
                if (!$entry) { throw 'ZIP entry missing.' }
                $inputStream = $entry.Open()
                $hash = [Security.Cryptography.SHA256]::Create()
                try { $actual = ([BitConverter]::ToString($hash.ComputeHash($inputStream))).Replace('-', '') }
                finally { $hash.Dispose(); $inputStream.Dispose() }
                if ($actual -ne (Get-FileHash -LiteralPath $file.Source -Algorithm SHA256).Hash) { throw 'ZIP bytes differ.' }
            }
        } finally { $check.Dispose() }
        Move-Item -LiteralPath $temporary -Destination $Destination -Force
    } finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force } }
}
