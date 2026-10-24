# vzip-cli

[简体中文](README.zh-CN.md)

A standalone command-line tool for reading **VZip (VZa)** and **VZstd (VSZa)** streams used by Steam. A **7-Zip Formats plugin** is included as an optional component. **VZd is not supported yet.**

## Features and limits

- Identify, list, test and extract a single compressed stream.
- Decode LZMA with LZMA SDK and Zstandard with the upstream Zstandard library.
- Validate framing, output length and CRC32 with bounded streaming buffers.
- Build for Windows x86, x64 and ARM64. Match the plugin to the **7-Zip process architecture**.

Windows 10/11 are the target systems. Compatibility with other older Windows versions is not claimed. VZd, archive creation and updates are not implemented. VSZa currently has synthetic test coverage; a real Valve sample remains to be validated. GUI and all historical 7-Zip versions are not covered by the automated checks.

VZip is a single-stream wrapper, and its payload need not be a ZIP. Extraction returns the payload bytes; it does not list files inside an inner ZIP. Listing alone does not verify integrity. CRC32 detects corruption and does not authenticate a Steam manifest or publisher. Format names follow SteamKit; “Valve ZIP” is not presented as a verified official expansion.

## Formats

The following framing notes are based on pinned SteamKit implementations, not a complete Valve-published specification. Multi-byte integers are little-endian, and literal markers use actual byte order.

| Format/marker | Frame | Supported |
| --- | --- | --- |
| VZip/`VZa` | `VZa`; 4-byte timestamp/CRC field; 5-byte LZMA properties; raw LZMA stream; 4-byte output CRC32; 4-byte output length; `zv`. | Yes: identify, list, test and extract. |
| VZstd/`VSZa` | `VSZa`; 4-byte CRC32; Zstandard frame; 4-byte output CRC32; 4-byte output length; 4-byte unknown field; `zsv`. | Yes: identify, list, test and extract. |
| VZip Delta/`VZd` | `VZd`; 4-byte CRC field; 5-byte LZMA properties; LZMA delta stream using the original data block as a preset dictionary; 4-byte output CRC32; 4-byte output length; `zv`. | No: not implemented; requires the original data block. |

Fixed overhead is 22 bytes for VZa/VZd and 23 bytes for VSZa. The VZa header field is not always a timestamp. No invented meaning is assigned to VSZa's unknown field, and its header/footer CRC values are checked for consistency. VZa streams can have or omit an LZMA EOS marker. VZd framing and original-block position semantics follow [SteamKit's VZipDeltaUtil](https://github.com/SteamRE/SteamKit/blob/2049844fdf746420c9f31a9575d4b75c40a5d575/SteamKit2/SteamKit2/Util/VZipDeltaUtil.cs); this project has not validated a real VZd sample.

Output length is treated as unsigned 32-bit without allocating the whole output. Default LZMA dictionary and Zstandard window limits are 128 MiB, with 64 KiB streaming buffers. Excess compressed data and concatenated frames are rejected. These are implementation policies, not a claim to cover every Valve use case.

The handler exposes one payload stream without interpreting inner archive paths. It implements `IInArchive`, not `IOutArchive`. Test mode decodes and checks CRC. Input names with suffixes after `.vz` are identified by content. The outer frame has no reliable filename, so the output name is derived from the input, falling back to `payload`. Host cancellation and I/O errors are propagated.

## Use

Download and extract the CLI package matching your Windows architecture. Open PowerShell in the extracted directory. The CLI requires neither 7-Zip nor GitHub CLI.

```powershell
.\vzip.exe inspect 'example.zip.vz'
.\vzip.exe test 'example.zip.vz'
.\vzip.exe extract 'example.zip.vz' 'payload.zip'
```

The CLI refuses to overwrite an existing output and commits its temporary file only after validation. Its exit codes are `0` success, `1` processing failure and `2` invalid arguments. `inspect` reports structure without checking payload integrity. Host-managed extraction can leave partial output on failure.

## Optional 7-Zip plugin

Use a full Windows 7-Zip installation. Obtain the **Formats** package matching the 7-Zip process architecture from a successful Actions run or a published release, then extract it. CLI packages are separate. From the extracted package directory, run these commands in an elevated PowerShell session when 7-Zip is installed under Program Files:

```powershell
$sevenZip = 'C:\Program Files\7-Zip'
New-Item -ItemType Directory -Path (Join-Path $sevenZip 'Formats') -Force | Out-Null
[IO.File]::Copy((Join-Path (Get-Location) 'Formats\VZip.dll'), (Join-Path $sevenZip 'Formats\VZip.dll'), $false)
& (Join-Path $sevenZip '7z.exe') i
```

`VZip` and `VZstd` should appear in the format list. Stop if loading fails. Copying refuses to overwrite an existing plugin; review any installed copy before replacing it. No reboot is required. To uninstall, close 7-Zip processes and remove only `Formats\VZip.dll`.


Plugin examples after installation:

```powershell
& 'C:\Program Files\7-Zip\7z.exe' t 'example.zip.vz'
& 'C:\Program Files\7-Zip\7z.exe' x 'example.zip.vz' '-opayload'
```

## Build and test

Prerequisites: Visual Studio 2022/2026 C++ Build Tools with the required architectures, Windows SDK, CMake 3.24 or later and full 7-Zip. Use a non-administrator PowerShell session in the repository root:

```powershell
.\scripts\Build.bat -Architecture x64 -RequireTests -SkipPackage -NoPause
.\tests\Test-Host.bat -Architecture x64 -HostDirectory 'C:\Program Files\7-Zip' -NoPause
.\scripts\Package.bat -Architecture x64 -RequireHostTests -NoPause
```

Use `x86` or `ARM64` for other targets. Native target execution is required by `-RequireTests`; cross-compiling does not validate execution. Stop on any failing command. Scripts prefer PowerShell 7 and support Windows PowerShell 5.1 fallback without changing system policy. `-NoPause` avoids an interactive wait; `-Silent` hides output while preserving failure status. Dependencies are version/hash pinned in CMake and [dependencies.lock.json](dependencies.lock.json). Existing verified source trees can be passed through `-SdkSource` and `-ZstdSource`.

Builds and private validation metadata go to `.local`. Component staging directories are `dist/cli-<architecture>` and `dist/formats-<architecture>`. Final ZIPs go to `release`. These generated directories are ignored. Without `-SkipPackage`, Build also creates both component packages. Local Build or Package can use `-IncludeSource` to create `release/source.zip`; Actions and Releases do not create an extra source archive.

For each of `x64`, `x86` and `arm64`, the output filenames are `vzip-cli-<architecture>.zip` and `vzip-formats-<architecture>.zip`:

```text
CLI ZIP                       Formats ZIP
vzip.exe                      Formats/VZip.dll
README/                       README/
  README.md                     README.md
  README.zh-CN.md                README.zh-CN.md
LICENSES/                     LICENSES/
```

Package READMEs contain component usage only. LICENSES retains the project license, original dependency notices and bilingual attribution. Repository documentation, dependency locks, build metadata and checksums are not embedded in binary packages.

Release notes start with the version entry from [CHANGELOG.md](CHANGELOG.md). Add a matching bilingual entry before releasing a new version.

## Build provenance

Actions builds and tests x86/x64 on `windows-2022` and ARM64 on native `windows-11-arm`. PR builds are read-only. Trusted `main` builds generate attestations for the final ZIP, DLL and CLI and verify them with GitHub CLI. No local GPG private key is stored in Actions. Preserve downloaded ZIP bytes. Releases provide one `SHA256SUMS` file for all six binary ZIPs; Actions artifacts and ZIP interiors contain no checksum file:

```powershell
Get-FileHash .\vzip-cli-x64.zip -Algorithm SHA256
Select-String -LiteralPath .\SHA256SUMS -SimpleMatch 'vzip-cli-x64.zip'
```

Download `vzip-cli-x64.zip` and `SHA256SUMS` into the same directory, run the commands there, and compare the two SHA-256 values, ignoring letter case.

Optional advanced verification requires installing [GitHub CLI](https://cli.github.com/); it is not a runtime dependency:

```powershell
gh attestation verify .\vzip-cli-x64.zip -R gentrydeng/vzip-cli
```

Attestations establish build provenance; they are not Authenticode signatures, a security audit or a claim of byte-identical reproducible builds. To promote a build, open **Actions → Promote tested build to Release → Run workflow** on `main`. Enter a successful **Build and test** run ID and a version tag matching the built CMake version, such as `v0.1.0`. Keep **Create a draft** selected to review before publication. **Validate promotion** checks the full promotion path without creating a Release or tag. The workflow requires a successful trusted-main run, unexpired component artifacts, a matching version and valid provenance, and refuses to overwrite a tag. It uploads the six original ZIPs plus one `SHA256SUMS`, with architecture-specific download links and alternating Chinese/English release notes. GitHub supplies the source ZIP/tarball links automatically. A preview of note generation runs in CI without creating a Release or checksum asset.

## License, independence and AI work

Original project code is [MIT licensed](LICENSE). Third-party works retain their own rights and licenses; see [third-party notices](THIRD-PARTY-NOTICES.md).

This is an independent third-party project with no direct affiliation, official cooperation, sponsorship or endorsement from Valve, Igor Pavlov/7-Zip, M2-Team/NanaZip, SteamRE/SteamKit, Meta, Microsoft, GitHub or OpenAI. Relevant works, names and marks belong to their respective rights holders.

OpenAI Codex performed most design, coding, testing, documentation and local build/validation work at the maintainer's request. The maintainer decides scope and publication and is responsible for human review and acceptance. This disclosure does not claim completed human review, OpenAI copyright ownership, vendor auditing or endorsement. Commits submitted by Codex use a `[Codex]` title prefix and `Submitted-by: OpenAI Codex` trailer; their email can still associate them with the maintainer's GitHub account.
