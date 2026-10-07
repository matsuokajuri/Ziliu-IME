# 构建与开发

[← 项目首页](../README.md) · [贡献指南](../CONTRIBUTING.md) · [架构](ARCHITECTURE.md)

## 环境与依赖

- Windows 开发机与 x64 工具链；当前发布包只支持 Windows 11 x64。
- Visual Studio 2026，安装“使用 C++ 的桌面开发”及 MSVC v145 的头文件、库与工具。
- CMake 3.28+；使用 `windows-x64` preset 时，CMake 还需支持 Visual Studio 18 2026 generator。
- Git 与 7-Zip（`7z.exe` 可从 PATH 找到）。
- WinUI 3 工程使用 Windows SDK `10.0.28000.0`、Windows App SDK `2.2.0` 与 C++/WinRT `3.0.260715.1`。

`build-winui3.cmd` 保留 Windows SDK `10.0.28000.0` 作为本地默认；可通过环境变量 `ZILIU_WINDOWS_SDK_VERSION` 显式选择已安装版本。CI 选择官方 runner 提供的 `10.0.26100.0`，不修改工程的默认 SDK、工具集或 NuGet 版本。
- CodeGraph CLI，用于符号定位和结构变更后的索引同步。

详细约束见 [AGENTS.md](../AGENTS.md)。不直接修改 `third_party/`；数据定制放在[字流覆盖层](../data/ziliu/README.md)。

固定公共依赖：

| 依赖 | 固定版本 |
| --- | --- |
| librime | `33e78140250125871856cdc5b42ddc6a5fcd3cd4` |
| 雾凇拼音 | `b681a34f788795034b3b288830f4861980bc8b0d` |
| Windows 运行时 | librime 官方 `1.17.0` Release 的 `rime-33e7814-Windows-msvc-x64.7z` |

运行时归档 SHA-256 为 `7478c7caa4ff6b37de86daba1f7ce4a994a4f5ba24872a820fb2b3a9b01fed15`。脚本验证后解压到被 Git 忽略的 `.cache`，第三方许可见[声明文件](../THIRD_PARTY_NOTICES.md)。

## 常规构建

在普通 PowerShell 终端中执行：

```powershell
git clone --recurse-submodules https://github.com/matsuokajuri/Ziliu-IME.git
cd Ziliu-IME
scripts\fetch-librime-runtime.ps1
scripts\build-local.cmd Debug
scripts\build-local.cmd Release
```

`build-local.cmd` 定位 Visual Studio、加载 MSVC 环境，配置 CMake、构建并运行 CTest。省略参数时默认 Debug。产物分别位于 `build/local-x64-Debug/bin` 与 `build/local-x64-Release/bin`。

该脚本启用 Rime 适配器；依赖缺失时开发构建可以退回确定性 Stub。要验证真实拼音能力，应准备固定 submodule 和已校验运行时，检查构建输出与真实 Rime 测试结果，不能把 Stub 测试当作完整输入功能验收。

在 Visual Studio Developer PowerShell 中也可使用 presets：

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
```

此 preset 当前设置 `ZILIU_ENABLE_RIME=OFF`，用于基础构建与测试；产物在 `build/windows-x64/bin/Debug`。

## WinUI 3 设置程序

设置界面采用独立 MSBuild 工程；CMake 的 `ZiliuSettings` 默认构建目标会调用下述脚本，完整构建也需要它的 SDK 和 NuGet 依赖。先准备工程所需的 NuGet 包，也可单独执行：

```powershell
scripts\build-winui3.cmd Release build\local-x64-Release\bin\
```

当前脚本把 `RestoreSources` 限定为当前用户的本地 NuGet 缓存。若依赖缺失，应按工程的 `PackageReference` 准备对应版本；脚本不会自动从在线源补齐。不要把个人 NuGet 配置或缓存提交到仓库。

## 测试与 CI 的边界

第一方 C++ 警告按错误处理。行为修改应运行相关的确定性测试，并说明未执行的环境验证。源码构建和 CTest 不自动注册开发 TIP。

仓库现有 [build 工作流](https://github.com/matsuokajuri/Ziliu-IME/actions/workflows/build.yml) 固定 `windows-2025-vs2026` runner，初始化固定的一级公共 submodule，并从官方 NuGet 源恢复工程固定的 WinUI 包到 runner 缓存；使用 Visual Studio 2026 generator、`ZILIU_ENABLE_RIME=OFF`，执行包含独立 WinUI 3 工程的 CMake 构建和 CTest。支持 push、pull request 与手动触发；手动运行应记录所选分支和实际 `head_sha`。这不会覆盖真实 Rime、界面交互、打包或安装验收；请以具体运行记录评估结果。

修改结构后执行 `codegraph sync .`。开发期注册工具的 `install` / `uninstall` 会改变 Windows 状态，须独立、显式执行，不加入自动测试或登录启动项。

## 生成未签名测试包

先完成 Release x64、真实 Rime 与独立 WinUI 3 构建，再指定准备好的依赖 checkout：

```powershell
$env:ZILIU_DEPENDENCY_ROOT = (Get-Location).Path
scripts\package-alpha.cmd
```

打包脚本检查运行时、数据、WinUI self-contained 文件清单、四个产品的 `0.1.0-alpha.1` 版本资源、第三方许可与逐文件 SHA-256。已有同名产物时默认拒绝覆盖。图形安装器制作、只读校验和安装恢复边界见[Alpha 发布记录](ALPHA-RELEASE.md)。

本地构建成功、生成 ZIP、CI 通过和实际安装验收是不同证据，应分别记录版本、源提交和测试范围。
