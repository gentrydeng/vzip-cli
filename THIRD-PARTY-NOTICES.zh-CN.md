# 第三方声明

[English](THIRD-PARTY-NOTICES.md)

原创代码采用 [MIT](LICENSE)。该许可不替代第三方条款，也不授予其名称、标识或内容的权利。

## 编入的依赖

| 作品 | 署名与许可 |
| --- | --- |
| LZMA SDK 26.05 | 作者 Igor Pavlov。C 解码器、CRC／内存／CPU 辅助代码及 C++ 接口头来自单独的 SDK 发行包。其 `DOC/lzma-sdk.txt` 声明公共领域状态，原文随二进制包以 `LZMA-SDK-NOTICE.txt` 保留；这不将完整 7-Zip 源码重标为公共领域。[官方 SDK](https://www.7-zip.org/sdk.html)。 |
| Zstandard 1.5.7 | 原文：`Copyright (c) Meta Platforms, Inc. and affiliates. All rights reserved.`（著作权归 Meta Platforms, Inc. 及关联方，保留所有权利。）选择 BSD-3-Clause 许可，完整原许可随二进制包以 `ZSTANDARD-LICENSE.txt` 保留。测试构建也使用编码器生成原创向量。[上游](https://github.com/facebook/zstd/tree/v1.5.7)。 |

依赖版本、源码压缩包和 SHA-256 固定于 [dependencies.lock.json](dependencies.lock.json)。固定散列不能独立验证发布者签名。

## 参考与工具

SteamRE／SteamKit 贡献者保留其著作权和 LGPL 许可。固定提交的 [VZipUtil](https://github.com/SteamRE/SteamKit/blob/2049844fdf746420c9f31a9575d4b75c40a5d575/SteamKit2/SteamKit2/Util/VZipUtil.cs) 与 [VZstdUtil](https://github.com/SteamRE/SteamKit/blob/2049844fdf746420c9f31a9575d4b75c40a5d575/SteamKit2/SteamKit2/Util/VZstdUtil.cs) 用于格式研究；本项目不捆绑 SteamKit，也不复制或翻译其 C# 实现。

完整 7-Zip 作为外部测试宿主及解包工具，按其[原许可](https://www.7-zip.org/license.txt)使用，不随插件捆绑。未编入 RAR 实现。Python 标准库用于提供独立 LZMA 测试向量。MSVC、Windows SDK、PowerShell、CMake、GitHub Actions 等构建工具保留原条款，本项目不重新分发这些工具。

## 权利、独立性与 AI 工作

Valve Corporation 及相应权利人保留 Steam 软件、内容、名称与标识的权利。Igor Pavlov／7-Zip、Meta／Zstandard、SteamRE／SteamKit、M2-Team／NanaZip、Microsoft、GitHub、CMake／Kitware、Python、OpenAI 等保留其作品、文档、名称与标识的相应权利，作品内原始通知继续适用。本项目不授予 Valve 客户端或游戏内容分发权，也不捆绑这些内容。

本项目为独立第三方项目，与上述相关方没有直接隶属、官方合作、赞助、授权代表或背书关系。兼容、依赖、参考及 AI 服务使用不表示官方关系。

大部分设计、代码、测试、文档和本地构建验证由 OpenAI Codex 按维护者要求完成。维护者负责需求、范围、人工审阅、验收与发布。制作过程披露不将 OpenAI 或模型列为著作权人，不表示人工审阅已经完成，也不暗示厂商安全审计、保证或背书。
