# 开发指南

## 环境

- Windows 10 22H2 或 Windows 11
- Visual Studio 2026，安装“使用 C++ 的桌面开发”
- Windows 10/11 SDK
- CMake 3.28+
- Git
- CodeGraph CLI

如果 Visual Studio 已存在但脚本提示标准库缺失，请以管理员身份打开 Visual Studio
Installer，给现有实例添加组件 `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`。

## 常用命令

```powershell
scripts\build-local.cmd Debug
scripts\build-local.cmd Release
codegraph sync .
```

如果本机只有 MSVC 编译器和头文件、尚未安装链接库，可先执行严格的逐文件编译检查：

```powershell
scripts\compile-check.cmd
```

省略脚本参数时默认构建 Debug。在 Visual Studio Developer PowerShell 中也可使用
`CMakePresets.json` 的 Debug/Release presets。

## 代码规则

- 所有源文件使用 UTF-8。
- C++ 警告按错误处理。
- TIP 回调中禁止网络、等待子进程、读取大型文件或写数据库。
- Windows 句柄和 COM 引用必须具备明确所有权。
- 新增模块必须说明线程模型、失败策略和性能预算。
- 核心算法尽量放入 `ziliu_core`，以便脱离 TSF 做确定性测试。

## TSF 注册

`ZiliuRegister` 只用于开发。它按当前用户注册 COM DLL 与 TSF profile，不会把字流设置成
默认输入法。注册属于系统状态变更，自动测试不得执行。

正式发行将由 MSI 调用同样的注册逻辑，并完整支持回滚与卸载。
