# vzip 命令行工具

[English](README.md)

完整解压本包后，在包根目录打开 PowerShell；无需安装 7-Zip、GitHub CLI，也无需管理员权限。选择匹配 Windows 架构的版本。

```powershell
.\vzip.exe inspect 'example.zip.vz'
.\vzip.exe test 'example.zip.vz'
.\vzip.exe extract 'example.zip.vz' 'payload.zip'
```

`inspect` 只检查帧结构；`test` 实际解码并校验 CRC 和长度；`extract` 拒绝覆盖已有输出，临时文件验证后才提交。输出父目录应事先存在。退出码：`0` 成功、`1` 处理失败、`2` 参数错误。

支持 VZa、VSZa，暂不支持 VZd。提取结果是负载原始字节，不一定是 ZIP；内层 ZIP 需要另行解压。目标系统为 Windows 10／11，不声明兼容其他旧版系统。

完整用法与限制见[仓库 README](https://github.com/gentrydeng/vzip-cli/blob/main/README.zh-CN.md)。版权、许可及 AI 工作声明见 [LICENSES](../LICENSES/NOTICE.zh-CN.md)。
