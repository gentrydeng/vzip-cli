# Third-party notices

[简体中文](THIRD-PARTY-NOTICES.zh-CN.md)

Original project code is licensed under [MIT](LICENSE). That license does not replace third-party terms or grant rights to their names, marks or content.

## Incorporated dependencies

| Work | Attribution and license |
| --- | --- |
| LZMA SDK 26.05 | By Igor Pavlov. C decoder, CRC/allocation/CPU helpers and C++ interface headers come from the separate SDK distribution. Its `DOC/lzma-sdk.txt` declares public-domain status; the original notice is included in binary packages as `LZMA-SDK-NOTICE.txt`. This does not relabel the full 7-Zip source tree as public domain. [Official SDK](https://www.7-zip.org/sdk.html). |
| Zstandard 1.5.7 | `Copyright (c) Meta Platforms, Inc. and affiliates. All rights reserved.` The BSD-3-Clause license option is selected. Its complete original license is included in binary packages as `ZSTANDARD-LICENSE.txt`. Test builds also use its encoder to generate original vectors. [Upstream](https://github.com/facebook/zstd/tree/v1.5.7). |

Dependency versions, source archives and SHA-256 values are pinned in [dependencies.lock.json](dependencies.lock.json). Hash pinning does not independently authenticate a publisher signature.

## References and tools

SteamRE/SteamKit contributors retain their copyrights and LGPL license. The pinned [VZipUtil](https://github.com/SteamRE/SteamKit/blob/2049844fdf746420c9f31a9575d4b75c40a5d575/SteamKit2/SteamKit2/Util/VZipUtil.cs) and [VZstdUtil](https://github.com/SteamRE/SteamKit/blob/2049844fdf746420c9f31a9575d4b75c40a5d575/SteamKit2/SteamKit2/Util/VZstdUtil.cs) informed format research. SteamKit is not bundled, and its C# implementation is not copied or translated into this project.

Full 7-Zip is used as an external test host and extraction tool under its [original license](https://www.7-zip.org/license.txt); it is not bundled with the plugin. No RAR implementation is incorporated. Python's standard library supplied an independent LZMA test vector. MSVC, Windows SDK, PowerShell, CMake, GitHub Actions and other build tools retain their original terms and are not redistributed here.

## Rights, independence and AI work

Valve Corporation and the respective holders retain rights in Steam software/content/names/marks. Igor Pavlov/7-Zip, Meta/Zstandard, SteamRE/SteamKit, M2-Team/NanaZip, Microsoft, GitHub, CMake/Kitware, Python and OpenAI retain rights in their respective works, documentation, names and marks. Original notices within those works remain applicable. This project grants no right to distribute Valve clients or game content and bundles none of them.

This independent third-party project has no direct affiliation, official cooperation, sponsorship, authority to represent or endorsement from those parties. Compatibility, dependencies, references and use of AI services do not establish an official relationship.

OpenAI Codex performed most design, coding, testing, documentation and local build/validation work at the maintainer's request. The maintainer is responsible for requirements, scope, human review, acceptance and publication. This process disclosure does not designate OpenAI or a model as a copyright holder, claim completed human review or imply a vendor security audit, warranty or endorsement.
