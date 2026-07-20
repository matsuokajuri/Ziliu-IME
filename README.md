# 字流 Ziliu

字流是一款面向 Windows 的自由开源中文输入法。项目坚持五个原则：现代、简洁、高效、
低占用、纯粹。

当前仓库处于 **0.2 输入链路阶段**。TSF、版本化命名管道、Broker、librime、雾凇拼音和
候选窗已经连通，并由真实引擎测试验证 `zhongguo → 中国` 与 `ziliu → 字流`。候选翻页、
完整快捷键和常用 Windows 应用兼容性仍待验证，因此尚不是可日常使用的 Alpha。

## 技术栈

- C++23 与 MSVC
- Windows Text Services Framework（TSF）
- Win32、Direct2D、DirectWrite、UI Automation
- CMake、CTest、Visual Studio 2026
- librime 1.17.0 输入引擎与固定版本的雾凇拼音词库
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
scripts\fetch-librime-runtime.ps1
scripts\build-local.cmd Debug
scripts\build-local.cmd Release
```

运行时脚本从 librime 官方 Release 下载固定的 MSVC x64 包并校验 SHA-256。若不运行，项目
仍可编译和测试 IPC，Broker 会退回只含少量词的确定性 Stub 引擎。

在 Visual Studio Developer PowerShell 中也可使用 CMake presets：

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
```

脚本构建产物分别位于 `build/local-x64-Debug/bin` 和
`build/local-x64-Release/bin`；preset 构建产物位于 `build/windows-x64/bin/Debug`
或 `build/windows-x64/bin/Release`。省略脚本参数时默认构建 Debug。

开发期注册工具已经能够注册和注销 TSF 配置，但骨架阶段不会自动运行，也不要把它加入
登录启动项：

```powershell
ZiliuRegister.exe install
ZiliuRegister.exe uninstall
```

## 当前边界

- TSF DLL 仅在 Broker 会话可用时处理字母、退格、Esc、空格和数字选词，并通过 edit
  session 管理组合文本；IPC 失败时结束当前组合并恢复放行。
- `ziliu_core` 提供有大小限制的 IPC v1 编解码、会话隔离和确定性 Stub。
- Broker 使用当前用户 SID ACL 的本机命名管道，独占 librime 和用户词库写入。
- librime 缺失或不兼容时安全退回 Stub；正常构建使用雾凇拼音并加载字流 overlay。
- 候选窗已支持定位、选词和上屏，尚未实现翻页与方向键导航。
- Settings 展示原生 Direct2D 界面骨架，尚未写入配置。
- 不包含联网、同步、遥测和自动更新代码。

详细设计见 [架构文档](docs/ARCHITECTURE.md) 和
[ADR-0001](docs/adr/0001-native-windows-stack.md)。

## 许可证

字流以 GNU GPL v3 发布。第三方组件继续使用各自许可证，详见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
