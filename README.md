# 字流 Ziliu

字流是一款面向 Windows 的自由开源中文输入法。项目坚持五个原则：现代、简洁、高效、
低占用、纯粹。

当前仓库处于 **0.1 骨架阶段**。它已经固定长期技术栈并建立 TSF、候选窗、后台进程、
设置程序和输入核心的编译边界；尚未接入 librime，也尚未成为可日常输入中文的版本。

## 技术栈

- C++23 与 MSVC
- Windows Text Services Framework（TSF）
- Win32、Direct2D、DirectWrite、UI Automation
- CMake、CTest、Visual Studio 2026
- librime 输入引擎与雾凇拼音词库（下一阶段接入）
- GPL-3.0-only

项目不使用 Electron、WebView、常驻 .NET、遥测 SDK 或广告组件。

## 仓库结构

```text
src/core       平台无关的组合态、候选和引擎接口
src/tsf        加载进宿主进程的最小 TSF COM DLL
src/ui         Direct2D/DirectWrite 候选窗
src/broker     每用户单实例、零轮询的后台进程
src/settings   仅在打开时运行的原生设置程序
tools/register 开发期 TSF 注册工具
data/ziliu     雾凇拼音的最小、可追溯覆盖层
tests          无外部测试框架的核心测试
docs           架构决策、开发和路线图
```

## 构建

要求：Windows 10/11、Visual Studio 2026 C++ 工具链、Windows SDK、CMake 3.28 以上。

普通终端可以直接运行统一脚本，它会定位 Visual Studio、加载 MSVC 环境、配置、构建并测试：

```powershell
scripts\build-local.cmd
```

在 Visual Studio Developer PowerShell 中也可使用 CMake presets：

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
```

脚本构建产物位于 `build/local-x64/bin`；preset 构建产物位于
`build/windows-x64/bin/Debug`。

开发期注册工具已经能够注册和注销 TSF 配置，但骨架阶段不会自动运行，也不要把它加入
登录启动项：

```powershell
ZiliuRegister.exe install
ZiliuRegister.exe uninstall
```

## 当前边界

- TSF DLL 完成 COM 生命周期和按键事件接入，但始终放行按键。
- `ziliu_core` 使用确定性的假引擎验证模块协议。
- Broker 只提供单实例消息循环，尚未开放 IPC。
- Settings 展示原生 Direct2D 界面骨架，尚未写入配置。
- 不包含联网、同步、遥测和自动更新代码。

详细设计见 [架构文档](docs/ARCHITECTURE.md) 和
[ADR-0001](docs/adr/0001-native-windows-stack.md)。

## 许可证

字流以 GNU GPL v3 发布。第三方组件继续使用各自许可证，详见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
