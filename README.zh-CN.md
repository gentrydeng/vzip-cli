# vzip-cli

[English](README.md)

用于读取 Steam 所用 **VZip（VZa）** 与 **VZstd（VSZa）** 数据流的独立命令行工具，附带可选的 **7-Zip Formats 插件**。**暂不支持 VZd**。

## 功能与限制

- 识别、列出、测试及解压单个压缩数据流。
- 复用 LZMA SDK 和上游 Zstandard 库解码。
- 使用有界流式缓冲，校验帧结构、输出长度与 CRC32。
- 构建 Windows x86、x64、ARM64 版本；插件架构须匹配 **7-Zip 进程架构**。

目标系统为 Windows 10／11，不声明兼容其他旧版 Windows 系统。未实现 VZd、创建或更新压缩包。VSZa 目前有自制向量覆盖，真实 Valve 样本仍待验证。自动检查不涵盖 GUI 及全部历史 7-Zip 版本。

VZip 是单流封装，其负载不一定是 ZIP。本插件提取负载原始字节，不列出内层 ZIP 中的文件。列出成功不代表完整性通过。CRC32 用于检测损坏，不验证 Steam 清单或发布者身份。名称沿用 SteamKit，不把“Valve ZIP”写成已确认的官方全称。

## 格式

以下为依据固定版本 SteamKit 实现整理的帧结构，不是 Valve 发布的完整规范。多字节整数按小端读取，字面标记按实际字节顺序书写。

| 格式／标记 | 帧结构 | 是否支持 |
| --- | --- | --- |
| VZip／`VZa` | `VZa`；4 字节时间戳／CRC 字段；5 字节 LZMA 属性；原始 LZMA 流；4 字节输出 CRC32；4 字节输出长度；`zv`。 | 是：识别、列出、测试与解压。 |
| VZstd／`VSZa` | `VSZa`；4 字节 CRC32；Zstandard 帧；4 字节输出 CRC32；4 字节输出长度；4 字节未知字段；`zsv`。 | 是：识别、列出、测试与解压。 |
| VZip Delta／`VZd` | `VZd`；4 字节 CRC 字段；5 字节 LZMA 属性；使用原始数据块作预置字典的 LZMA 增量流；4 字节输出 CRC32；4 字节输出长度；`zv`。 | 否：暂未实现，需要原始数据块。 |

VZa／VZd 固定开销为 22 字节，VSZa 为 23 字节。VZa 的头部字段并不总是时间戳；VSZa 的未知字段不赋予猜测含义，并检查头尾 CRC 一致。VZa 流可带或不带 LZMA EOS 标记。VZd 的结构和原始块位置语义参考 [SteamKit 的 VZipDeltaUtil](https://github.com/SteamRE/SteamKit/blob/2049844fdf746420c9f31a9575d4b75c40a5d575/SteamKit2/SteamKit2/Util/VZipDeltaUtil.cs)，本项目尚无真实 VZd 样本验收。

输出长度按无符号 32 位处理，不分配完整输出。默认 LZMA 字典与 Zstandard 窗口上限为 128 MiB，流式缓冲为 64 KiB。拒绝多余压缩数据和拼接帧；这些是实现策略，不声明覆盖所有 Valve 场景。

处理器只暴露一个负载数据流，不解析内层归档路径；实现 `IInArchive`，不提供 `IOutArchive`。测试模式实际解码并校验 CRC。`.vz` 后带额外后缀的输入通过内容识别。外层帧没有可靠文件名，输出名从输入推导，无法推导时使用 `payload`。宿主取消与读写错误原样传递。

## 使用

下载匹配 Windows 架构的 CLI 包并完整解压，在解压目录打开 PowerShell。CLI 不需要安装 7-Zip 或 GitHub CLI。

```powershell
.\vzip.exe inspect 'example.zip.vz'
.\vzip.exe test 'example.zip.vz'
.\vzip.exe extract 'example.zip.vz' 'payload.zip'
```

CLI 拒绝覆盖已有输出，临时文件完成验证后才提交。退出码为 `0` 成功、`1` 处理失败、`2` 参数错误。`inspect` 只检查结构，不校验负载完整性。宿主管理的解压在失败后可能留下部分输出。

## 可选的 7-Zip 插件

使用完整 Windows 版 7-Zip，从成功的 Actions 运行或已发布版本取得匹配 7-Zip 进程架构的 **Formats** 包并解压；CLI 包单独提供。若 7-Zip 位于 Program Files，在提权 PowerShell 中进入解压后的包目录，执行：

```powershell
$sevenZip = 'C:\Program Files\7-Zip'
New-Item -ItemType Directory -Path (Join-Path $sevenZip 'Formats') -Force | Out-Null
[IO.File]::Copy((Join-Path (Get-Location) 'Formats\VZip.dll'), (Join-Path $sevenZip 'Formats\VZip.dll'), $false)
& (Join-Path $sevenZip '7z.exe') i
```

格式列表应出现 `VZip` 与 `VZstd`；加载失败时停止。复制命令拒绝覆盖已有插件，如已安装须先检查旧文件再决定替换。无需重启。卸载时关闭 7-Zip 进程，只移除 `Formats\VZip.dll`。


安装插件后的使用示例：

```powershell
& 'C:\Program Files\7-Zip\7z.exe' t 'example.zip.vz'
& 'C:\Program Files\7-Zip\7z.exe' x 'example.zip.vz' '-opayload'
```

## 构建与测试

前提：Visual Studio 2022／2026 C++ 生成工具及所需架构组件、Windows SDK、CMake 3.24 或以上、完整 7-Zip。在普通权限 PowerShell 中进入仓库根目录：

```powershell
.\scripts\Build.bat -Architecture x64 -RequireTests -SkipPackage -NoPause
.\tests\Test-Host.bat -Architecture x64 -HostDirectory 'C:\Program Files\7-Zip' -NoPause
.\scripts\Package.bat -Architecture x64 -RequireHostTests -NoPause
```

其它目标改为 `x86` 或 `ARM64`。`-RequireTests` 要求实际运行目标程序，交叉编译不等于运行验收。任何命令失败即停止。脚本优先 PowerShell 7，支持 Windows PowerShell 5.1 回退，不改变系统策略。`-NoPause` 不等待交互，`-Silent` 隐藏输出但保留失败状态。依赖版本与散列固定于 CMake 和 [dependencies.lock.json](dependencies.lock.json)；已有经核验源码可用 `-SdkSource`、`-ZstdSource` 指定。

构建与内部验证元数据位于 `.local`，组件暂存分别位于 `dist/cli-<架构>` 和 `dist/formats-<架构>`，最终 ZIP 位于 `release`；这些生成目录均被忽略。省略 `-SkipPackage` 时，Build 会同时生成两种组件包。本地 Build 或 Package 可使用 `-IncludeSource` 生成 `release/source.zip`；Actions 和 Releases 不额外生成源码包。

对 `x64`、`x86`、`arm64` 各生成 `vzip-cli-<架构>.zip` 和 `vzip-formats-<架构>.zip`：

```text
CLI ZIP                       Formats ZIP
vzip.exe                      Formats/VZip.dll
README/                       README/
  README.md                     README.md
  README.zh-CN.md                README.zh-CN.md
LICENSES/                     LICENSES/
```

包内 README 只介绍对应组件用法；LICENSES 保留项目许可、原始依赖通知与双语署名。二进制包不放仓库说明、依赖锁、构建元数据或校验清单。

Release 说明以 [CHANGELOG.md](CHANGELOG.md) 中对应版本的更新日志开头；发布新版本前须补充对应的中英双语条目。

## 构建来源证明

Actions 在 `windows-2022` 构建与测试 x86／x64，在原生 `windows-11-arm` 构建与测试 ARM64。PR 构建只读；可信 `main` 构建为最终 ZIP、DLL 和 CLI 生成证明，并由 GitHub CLI 验证。本机 GPG 私钥不进入 Actions。下载后保留 ZIP 原字节。Releases 提供一份覆盖六个二进制 ZIP 的 `SHA256SUMS`，Actions 制品及 ZIP 内部没有校验清单：

```powershell
Get-FileHash .\vzip-cli-x64.zip -Algorithm SHA256
Select-String -LiteralPath .\SHA256SUMS -SimpleMatch 'vzip-cli-x64.zip'
```

将 `vzip-cli-x64.zip` 和 `SHA256SUMS` 下载到同一目录，在该目录执行上述命令，比较两处 SHA-256 值，字母大小写可忽略。

可选的进阶来源校验需要先自行安装 [GitHub CLI](https://cli.github.com/)，它不是运行本工具的前提：

```powershell
gh attestation verify .\vzip-cli-x64.zip -R gentrydeng/vzip-cli
```

证明用于确定构建来源，不等于 Authenticode、安全审计或逐字节可复现构建声明。发布构建时，在 `main` 的 **Actions → Promote tested build to Release → Run workflow** 中填写成功的 **Build and test** 运行 ID，以及匹配构建时 CMake 版本的标签，例如 `v0.1.0`。默认勾选 **Create a draft**，先检查草稿再发布；勾选 **Validate promotion** 可只验证完整流程，不创建 Release 或标签。工作流要求可信 main 构建全部成功、组件制品未过期、版本匹配且证明有效，不覆盖已有标签；上传六个原始 ZIP 和一份 `SHA256SUMS`，生成明确的架构下载链接及中英交替说明。GitHub 自动提供源码 ZIP／tarball 链接。CI 会测试说明生成，但不创建 Release 或校验清单资产。

## 许可、独立性与 AI 工作

原创代码采用 [MIT](LICENSE)，第三方作品保留各自权利与许可，见[第三方声明](THIRD-PARTY-NOTICES.zh-CN.md)。

本项目是独立第三方项目，与 Valve、Igor Pavlov／7-Zip、M2-Team／NanaZip、SteamRE／SteamKit、Meta、Microsoft、GitHub 或 OpenAI 没有直接隶属、官方合作、赞助或背书关系。相关作品、名称与标识归各自权利人。

本项目大部分设计、代码、测试、文档和本地构建验证由 OpenAI Codex 按维护者要求完成。维护者决定范围与发布，并负责人工审阅和验收。本声明不表示人工审阅已经完成、OpenAI 拥有著作权或厂商提供审计／背书。Codex 提交的 commit 使用 `[Codex]` 标题前缀和 `Submitted-by: OpenAI Codex` 正文标记；提交邮箱仍可能使其关联到维护者的 GitHub 账户。
