# 7-Zip Formats 插件

[English](README.md)

选择匹配 **7-Zip 进程架构**的包并完整解压。若 7-Zip 位于 Program Files，在提权 PowerShell 中进入包根目录，执行：

```powershell
$sevenZip = 'C:\Program Files\7-Zip'
New-Item -ItemType Directory -Path (Join-Path $sevenZip 'Formats') -Force | Out-Null
[IO.File]::Copy((Join-Path (Get-Location) 'Formats\VZip.dll'), (Join-Path $sevenZip 'Formats\VZip.dll'), $false)
& (Join-Path $sevenZip '7z.exe') i
```

格式列表应出现 `VZip`、`VZstd`；加载失败时停止。复制命令拒绝覆盖已有插件，请先检查旧版本。无需重启。替换或移除 `Formats\VZip.dll` 前关闭 7-Zip 进程。

```powershell
& 'C:\Program Files\7-Zip\7z.exe' t 'example.zip.vz'
& 'C:\Program Files\7-Zip\7z.exe' x 'example.zip.vz' '-opayload'
```

支持 VZa／VSZa，暂不支持 VZd。只提取一个负载流，不列出内层 ZIP 文件。列出不代表完整性校验，宿主解压失败时可能保留部分输出。目标 Windows 10／11，不声明兼容其他旧版系统。

完整安装、用法与限制见[仓库 README](https://github.com/gentrydeng/vzip-cli/blob/main/README.zh-CN.md)。版权、许可和 AI 工作声明见 [LICENSES](../LICENSES/NOTICE.zh-CN.md)。
