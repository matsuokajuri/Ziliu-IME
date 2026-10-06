<p align="center">
  <img src="docs/assets/ziliu-cover.svg" alt="字流 / Ziliu — 让输入回到文字本身。Windows 中文输入法项目的原创品牌示意。" width="100%">
</p>

<h1 align="center">字流 / Ziliu</h1>

<p align="center">面向 Windows 的本地中文输入法<br>Rime 输入引擎 · 原生 TSF 集成 · 自由定制候选栏</p>

<p align="center">
  <a href="https://github.com/matsuokajuri/Ziliu-IME/releases/tag/v0.1.0-alpha.1">0.1.0-alpha.1 · 未签名预发布</a>
  · Windows 11 x64
  · <a href="LICENSE">GPL-3.0-only</a>
</p>

<p align="center">
  <a href="docs/getting-started.md">开始试用</a> ·
  <a href="docs/DEVELOPMENT.md">构建与开发</a> ·
  <a href="docs/ARCHITECTURE.md">架构</a> ·
  <a href="docs/ROADMAP.md">路线图</a> ·
  <a href="CONTRIBUTING.md">参与贡献</a> ·
  <a href="#english">English</a>
</p>

## 字流是什么

字流希望把日常中文输入做得清爽、可控：沿用 Rime 与雾凇拼音的输入能力，以 Windows Text Services Framework（TSF）接入应用，并提供原生候选栏和 WinUI 3 设置界面。

**本仓库是字流的主仓库**，包含第一方源码、测试、数据覆盖层、构建与打包脚本。输入引擎在本机运行，当前产品没有云同步、在线候选或遥测模块。

> **当前阶段：未签名的早期测试版。** [0.1.0-alpha.1](https://github.com/matsuokajuri/Ziliu-IME/releases/tag/v0.1.0-alpha.1) 已提供 ZIP 和图形安装器，仅支持 Windows 11 x64。它经过限定范围的功能验收，仍有应用兼容性、安装恢复和皮肤表现等边界；不代表稳定正式版。下载与 SHA-256 校验方式见[试用指南](docs/getting-started.md)。

## 已有能力

| 方向 | 当前实现 |
| --- | --- |
| 拼音输入 | librime + 雾凇拼音，简拼、拼写纠错与可选模糊音 |
| 文字与标点 | 简繁切换、中英文模式、全半角标点及数字场景标点处理 |
| 候选栏 | 横向 / 纵向布局、候选数量、字体、颜色和缩放设置 |
| 外观定制 | 主题资源加载与预览，支持部分搜狗 `.ssf` 皮肤导入；兼容性仍在完善 |
| 系统集成 | TSF 输入服务、独立 Broker、WinUI 3 设置与快捷菜单 |
| 可追溯打包 | 固定公共依赖、校验过的 Rime 运行时、逐文件清单与测试包版本信息 |

上下文排序、小模型与更深的输入引擎融合属于[规划与实验](docs/ROADMAP.md)，研究接口见[离线候选评分草案](docs/research/offline-ranking-contract.md)。当前发布包没有这些 AI 功能，仓库未发布模型权重，也没有可据此承诺的效果提升。

## 从哪里开始

| 你想做什么 | 入口 |
| --- | --- |
| 下载、校验、安装或卸载测试包 | [试用指南](docs/getting-started.md) · [Alpha 发布记录](docs/ALPHA-RELEASE.md) |
| 从源码构建、了解工具链与 CI 范围 | [开发指南](docs/DEVELOPMENT.md) |
| 理解进程边界与输入数据流 | [架构说明](docs/ARCHITECTURE.md) |
| 了解后续优先级 | [路线图](docs/ROADMAP.md) |
| 制作主题或了解 SSF 兼容边界 | [主题格式](docs/theme-format-v1.md) · [SSF 映射](docs/sogou-ssf-mapping.md) |
| 报告问题或参与改进 | [Issues](https://github.com/matsuokajuri/Ziliu-IME/issues) · [贡献指南](CONTRIBUTING.md) |

准备好 Windows 开发环境后，在 PowerShell 中执行：

```powershell
git clone --recurse-submodules https://github.com/matsuokajuri/Ziliu-IME.git
cd Ziliu-IME
scripts\fetch-librime-runtime.ps1
scripts\build-local.cmd Release
```

需要 Visual Studio 2026 C++ 工具链、Windows SDK、CMake 和 7-Zip。WinUI 3 设置程序使用单独的 MSBuild 工程；完整步骤见[开发指南](docs/DEVELOPMENT.md)。源码构建不会自动安装或注册输入法。

## 架构一览

![字流常规输入架构：应用中的 TSF 与候选栏通过本机命名管道连接 Broker，Broker 使用 librime 与雾凇数据；WinUI 3 设置保存本地配置。](docs/assets/architecture.svg)

TSF 负责与应用交互，Broker 承载输入会话和 Rime 引擎，候选栏呈现结果，设置程序管理配置与主题。上图是原创架构示意，非产品截图；模块入口和数据边界见[架构说明](docs/ARCHITECTURE.md)。

## 开发状态

Alpha 发布记录覆盖有限的记事本、Edge、Windows 搜索、输入范围、主题、升级与卸载场景；所有应用和自定义 SSF 的逐像素一致性仍未验证。更详细的已测与未测范围见[发布记录](docs/ALPHA-RELEASE.md)。

仓库现有 [build 工作流](https://github.com/matsuokajuri/Ziliu-IME/actions/workflows/build.yml) 配置 Windows CMake 构建与 CTest，使用 `ZILIU_ENABLE_RIME=OFF`。该工作流的检查范围不包含真实 Rime、独立 WinUI 3 工程、安装器或交互验收；它的状态不能替代发布包验证。

当前优先级是完善输入兼容性和安装生命周期，建立可复核的发布与性能基线，再评估实验方向。路线图不承诺日期，也不把目标指标当成已测结果。

## 贡献与许可

欢迎可复现的兼容性问题、文档改进、合成测试用例和小范围修复。请先阅读[贡献指南](CONTRIBUTING.md)，在[新建 Issue](https://github.com/matsuokajuri/Ziliu-IME/issues/new)时提供版本、应用和最小复现步骤，使用合成输入代替私人文档或个人词库。安全问题请按 [SECURITY.md](SECURITY.md) 的入口报告。

项目采用现有 [GNU GPL v3 许可证](LICENSE)，沿用仓库的 GPL-3.0-only 声明。感谢 [Rime](https://github.com/rime/librime)、[雾凇拼音](https://github.com/iDvel/rime-ice)及相关上游项目；依赖保留各自许可，见[第三方声明](THIRD_PARTY_NOTICES.md)与[数据来源](data/ziliu/README.md)。本页封面和架构图为项目原创 SVG，按仓库现有许可证提供。

<a id="english"></a>

## English at a glance

**Ziliu (字流)** is a local-first Chinese input method for Windows, combining librime and Rime Ice with native TSF integration, a configurable candidate window, and WinUI 3 settings. This is the main source repository.

The [0.1.0-alpha.1 prerelease](https://github.com/matsuokajuri/Ziliu-IME/releases/tag/v0.1.0-alpha.1) provides an **unsigned, test-only** ZIP and installer for Windows 11 x64. It is an early test release with limited acceptance coverage. Contextual ranking and small-model integration remain research directions; no model or demonstrated AI improvement is released here.

Start with the [trial guide](docs/getting-started.md), [development guide](docs/DEVELOPMENT.md), [architecture](docs/ARCHITECTURE.md), or [contribution guide](CONTRIBUTING.md). Documentation is primarily in Chinese. The project retains its existing [GPL v3 license](LICENSE); third-party components retain their own terms.
