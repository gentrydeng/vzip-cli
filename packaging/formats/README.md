# 7-Zip Formats plugin

[简体中文](README.zh-CN.md)

Choose the package matching the **7-Zip process architecture**, then extract it. From the package root, run PowerShell as administrator if 7-Zip is under Program Files:

```powershell
$sevenZip = 'C:\Program Files\7-Zip'
New-Item -ItemType Directory -Path (Join-Path $sevenZip 'Formats') -Force | Out-Null
[IO.File]::Copy((Join-Path (Get-Location) 'Formats\VZip.dll'), (Join-Path $sevenZip 'Formats\VZip.dll'), $false)
& (Join-Path $sevenZip '7z.exe') i
```

`VZip` and `VZstd` should appear. Stop if loading fails. The copy refuses to overwrite an existing plugin: review any old version first. No reboot is required. Close 7-Zip processes before replacing or removing `Formats\VZip.dll`.

```powershell
& 'C:\Program Files\7-Zip\7z.exe' t 'example.zip.vz'
& 'C:\Program Files\7-Zip\7z.exe' x 'example.zip.vz' '-opayload'
```

Supports VZa/VSZa, not VZd. Extraction returns one payload stream without listing files in an inner ZIP. Listing alone does not verify integrity; host extraction can leave partial output on failure. Targets Windows 10/11; other older versions are not claimed compatible.

Full installation, usage and limitations: [repository README](https://github.com/gentrydeng/vzip-cli#readme). Copyrights, licenses and AI work disclosure: [LICENSES](../LICENSES/NOTICE.md).
