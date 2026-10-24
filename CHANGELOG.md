# 更新日志 / Changelog

## v0.1.0

初始发布。
Initial release.

- 识别、列出、测试和解压 VZip（VZa）及 VZstd（VSZa）；暂不支持 VZd 或创建／修改压缩包。
  Recognize, list, test and extract VZip (VZa) and VZstd (VSZa); VZd and archive creation/update are not supported.

- 提供独立 vzip.exe CLI 和 7-Zip Formats 插件，分别打包 Windows x64、x86、ARM64，面向 Windows 10／11。
  Provide a standalone vzip.exe CLI and a 7-Zip Formats plugin, packaged separately for Windows x64, x86 and ARM64, targeting Windows 10/11.

- 验证帧边界、解压大小和 CRC-32；采用 LZMA SDK 和 Zstandard 解码，原创代码以 MIT 许可发布。
  Validate framing, decoded size and CRC-32; decode with the LZMA SDK and Zstandard, with original code under the MIT license.

- 通过 GitHub Actions 构建、测试并生成制品证明；发布流程复用构建所得 ZIP，提供一份 SHA256SUMS。
  Build, test and attest artifacts with GitHub Actions; the release workflow reuses built ZIPs and provides one SHA256SUMS file.
