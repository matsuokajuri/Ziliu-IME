# ADR-0001：固定全原生 Windows 技术栈

- 状态：已接受
- 日期：2026-07-20

## 决策

字流长期采用以下技术栈：

| 层 | 固定选择 |
|---|---|
| 语言 | C++23 |
| 编译器 | MSVC |
| C/C++ 运行库 | 静态 MSVC CRT（Release `/MT`，Debug `/MTd`） |
| 输入框架 | Windows TSF / COM |
| 窗口 | Win32 |
| 2D 绘制 | Direct2D |
| 文字 | DirectWrite |
| 无障碍 | UI Automation |
| 构建 | CMake |
| 测试 | CTest + 小型自包含测试程序 |
| 输入引擎 | librime，封装在 Broker 适配层之后 |
| 初始词库 | 雾凇拼音，上游固定 commit + 字流 overlay |
| IPC | 版本化本机命名管道 |
| 安装 | WiX Toolset 生成签名 MSI |
| 客户端许可证 | GPL-3.0-only |
| 未来自托管服务端许可证 | AGPL-3.0-or-later |

## 原因

- TSF 本身是 COM/C++ 边界，原生 C++ 能减少跨运行时故障面。
- 静态链接 MSVC 运行库，避免 TIP 因目标机器缺少 VC++ 可再发行组件而加载失败；模块边界不传递运行库对象。
- Direct2D/DirectWrite 能做出现代视觉，同时没有浏览器或托管运行时常驻成本。
- 设置程序不常驻，因此无需为了设置开发速度向 TIP 引入第二套运行时。
- librime 已覆盖拼音切分、词典和用户学习；接口被隔离后，未来可替换排序器而不改 TSF。
- CMake 同时支持 Visual Studio、本地自动化和 GitHub Actions。

## 明确不采用

- Electron、WebView 或本地 HTTP UI。
- 在 TSF DLL 中使用 .NET、网络请求、数据库写入或语言模型。
- 把 Weasel 整体作为代码基底。它仍是重要的行为和兼容性参考。
- 将未经版本固定的网络词库直接加入安装包。
- 在第一阶段实现云同步、账号系统或遥测。

## 变更规则

本 ADR 只允许在当前选型无法满足 Windows 官方兼容要求、安全要求，或出现已验证的维护
终止时修改。视觉风格、开发便利或短期流行度不是更换技术栈的充分理由。
