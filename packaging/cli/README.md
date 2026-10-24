# vzip-cli

[简体中文](README.zh-CN.md)

Extract the whole package and open PowerShell in its root directory. No installation, 7-Zip, GitHub CLI or administrator rights are required. Use the build matching your Windows architecture.

```powershell
.\vzip.exe inspect 'example.zip.vz'
.\vzip.exe test 'example.zip.vz'
.\vzip.exe extract 'example.zip.vz' 'payload.zip'
```

`inspect` checks framing only. `test` decodes and verifies CRC/length. `extract` refuses to overwrite an existing output and commits a temporary file only after validation. Ensure the output parent directory exists. Exit codes: `0` success, `1` processing failure, `2` invalid arguments.

VZa and VSZa are supported; VZd is not. Extraction returns the payload bytes, which need not be a ZIP. An inner ZIP requires a separate extraction step. Windows 10/11 are the targets; other older versions are not claimed compatible.

Full usage and limitations: [repository README](https://github.com/gentrydeng/vzip-cli#readme). Copyrights, licenses and AI work disclosure are in [LICENSES](../LICENSES/NOTICE.md).
