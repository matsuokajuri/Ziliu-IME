# 开始试用字流

[← 项目首页](../README.md) · [开发指南](DEVELOPMENT.md) · [Alpha 发布记录](ALPHA-RELEASE.md)

字流目前是 **0.1.0-alpha.1 未签名预发布版**，仅支持原生 Windows 11 x64。其他架构、全部应用兼容性和完整安装失败恢复尚未验证。

## 下载与校验

从本仓库的 [v0.1.0-alpha.1 Release](https://github.com/matsuokajuri/Ziliu-IME/releases/tag/v0.1.0-alpha.1) 下载。可选择图形安装器，或 ZIP 包及对应 `.sha256` 文件。

| 文件 | 发布页记录的 SHA-256 |
| --- | --- |
| `Ziliu-0.1.0-alpha.1-win11-x64-unsigned-test-only.zip` | `1e410ece52c080ba0700cae963eab1e58e646ca3bfe3ca00f38a58a4819d11a2` |
| `Ziliu-0.1.0-alpha.1-win11-x64-unsigned-test-only-setup.exe` | `d99026a4e1506e1b51295ca84ac090464021df76dae1c76f0e08f6f7a5cb7dca` |

在下载目录核对所选文件，例如：

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath .\Ziliu-0.1.0-alpha.1-win11-x64-unsigned-test-only.zip
```

校验和用于核对下载完整性，不能证明发布者身份。安装器和 ZIP 都未签名，Windows SmartScreen 可能警告或拦截；不要为此全局关闭 SmartScreen。

ZIP 解压后可用 64 位 PowerShell 检查内含文件清单：

```powershell
.\Install-Ziliu.ps1 -VerifyOnly
```

`-VerifyOnly` 检查平台、版本和逐文件 SHA-256，不执行安装或 TSF 注册。

## 安装与升级

图形安装器需要管理员权限，应由实际使用字流的管理员账户运行。这个 Alpha 不支持标准用户通过另一管理员账户的凭据跨账户安装。

安装器内部校验嵌入 ZIP 后执行安装；它不是 MSI 事务，不能保证从每种部分安装失败中自动恢复。ZIP 手动安装与故障边界见[Alpha 发布记录](ALPHA-RELEASE.md)。

安装或升级后请注销或重启 Windows，再重新打开要测试的应用，确保进程加载当前版本。然后通过 Windows 的输入法切换入口选择字流。

## 试用与反馈

先用合成文字验证拼音、候选选择、标点和中英文切换，再试设置与主题。部分 SSF 导入已实现，但布局、动画与素材表现不保证与原皮肤逐像素一致。

如果遇到异常，请记录包版本、Windows / 应用版本、显示缩放和最小复现步骤，在 [Issues](https://github.com/matsuokajuri/Ziliu-IME/issues) 反馈。不要附带私人聊天、用户词库、真实文档或未经脱敏的日志。

## 卸载

使用安装包提供的卸载入口或随包卸载脚本，具体流程见[Alpha 发布记录](ALPHA-RELEASE.md)。卸载保留本地设置、主题和 Rime 用户数据；要完全移除个人数据，应先自行备份并确认删除范围。

当前安装与卸载属于早期测试流程。已有发布记录的验收范围不等于对任意 Windows 配置和应用的兼容性保证。
