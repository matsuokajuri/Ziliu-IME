<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/ziliu-cover-dark.svg">
    <img src="docs/assets/ziliu-cover.svg" alt="字流原创概念插画：流动字形、候选排版与主题色，非软件截图。" width="100%">
  </picture>
</p>

<h1 align="center">字流 / Ziliu</h1>
<p align="center">面向 Windows 的原生中文输入法<br>基于 Rime 与雾凇拼音，提供可定制候选栏及部分搜狗 SSF 皮肤支持</p>

<p align="center">
  <strong><a href="https://github.com/matsuokajuri/Ziliu-IME/releases/tag/v0.1.0-alpha.1">下载 Alpha</a></strong> ·
  <a href="docs/getting-started.md">安装指南</a> ·
  <a href="#配置与皮肤">皮肤配置</a> ·
  <a href="docs/DEVELOPMENT.md">开发文档</a> ·
  <a href="https://github.com/matsuokajuri/Ziliu-IME/issues">问题反馈</a> ·
  <a href="#english">English</a>
</p>
<p align="center"><strong>0.1.0-alpha.1 · 未签名测试版</strong> · Windows 11 x64 · <a href="LICENSE">GPL-3.0-only</a></p>
<p align="center"><sub>封面是原创视觉概念，展示字形、候选排版与主题色；非软件截图。</sub></p>

字流希望让日常中文输入更清爽、可控：让熟悉的 Rime 输入能力进入 Windows 原生应用，也让候选栏适应自己的桌面。**这里是字流的主仓库**，包含第一方源码、测试、数据覆盖层和构建脚本。

## 三个值得了解的特色

**本机输入引擎。** librime 与雾凇拼音提供拼音、简拼、拼写纠错及可选模糊音，支持简繁、中英文与标点切换。输入引擎在本机运行，当前产品没有云同步、在线候选或遥测模块。

**原生 Windows 集成。** 通过 Text Services Framework（TSF）接入应用，以独立 Broker 承载引擎会话，并提供原生候选栏、WinUI 3 设置和快捷菜单。应用兼容性仍在持续验证。

**候选栏由你定制。** 调整横向或纵向布局、候选数量、字体、颜色与缩放；使用主题资源，或导入部分搜狗 `.ssf` 皮肤。SSF 的裁切、布局和素材表现仍在完善，不保证与原皮肤逐像素一致。

## 三步开始试用

1. **下载并校验。** 打开 [Alpha 发布页](https://github.com/matsuokajuri/Ziliu-IME/releases/tag/v0.1.0-alpha.1)，选择图形安装器或 ZIP，按[安装指南](docs/getting-started.md)核对 SHA-256。
2. **按指南安装。** 当前包仅支持 Windows 11 x64；安装器要求使用实际使用输入法的管理员账户。安装与恢复边界见[Alpha 发布记录](docs/ALPHA-RELEASE.md)。
3. **重新进入输入会话。** 注销或重启后，通过 Windows 输入法切换器选择字流，用合成文本试用拼音、候选选择和标点，再按需要调整外观。

这是早期、未签名的测试版，尚未覆盖所有 Windows 配置、应用或安装失败情形。升级、卸载与恢复请先阅读[安装指南](docs/getting-started.md)；卸载保留设置、主题与 Rime 用户数据。

<a id="配置与皮肤"></a>

## 配置与皮肤

从设置程序调整输入与候选栏选项。准备制作或适配自己的主题时，可沿着下面的入口继续：

| 你想调整什么 | 入口 |
| --- | --- |
| 布局、字体、配色与主题资源 | [主题格式](docs/theme-format-v1.md) |
| 搜狗 `.ssf` 的资源与布局映射 | [SSF 兼容说明](docs/sogou-ssf-mapping.md) |
| Rime 方案与第一方数据覆盖 | [字流数据层](data/ziliu/README.md) |

## 当前阶段

| 状态 | 范围 |
| --- | --- |
| 已有公开测试版 | 拼音输入、候选栏与设置、部分 SSF 支持，以及 ZIP / 图形安装器；已测范围见[发布记录](docs/ALPHA-RELEASE.md) |
| 规划与实验 | 上下文排序、小模型和更深的引擎融合；[研究接口草案](docs/research/offline-ranking-contract.md)只描述设计，生产未启用，未发布模型或可承诺的 AI 收益 |
| 仍需验收 | 更广的宿主兼容性、安装恢复、复杂皮肤表现及完整输入链路的性能基线 |

后续优先完善输入兼容性与安装生命周期，再评估实验方向。[路线图](docs/ROADMAP.md)列出优先级，不承诺日期，也不把目标当成已测结果。

## 面向开发者

从[构建与开发](docs/DEVELOPMENT.md)准备 Visual Studio 2026、Windows SDK、固定依赖与 WinUI 包。源码构建不会自动注册输入法。

现有 [Windows 构建工作流](https://github.com/matsuokajuri/Ziliu-IME/actions/workflows/build.yml)构建 C++ 产品和独立 WinUI 3 工程，并运行 CTest，使用 `ZILIU_ENABLE_RIME=OFF`。具体运行结果应以记录为准；基础 CI 不代替真实 Rime、界面、安装或宿主验收。

<details>
<summary>查看架构与源码导航</summary>

![字流架构示意：TSF 与候选栏通过本机命名管道连接 Broker，Broker 使用 librime 与雾凇数据；WinUI 3 管理本地配置。](docs/assets/architecture.svg)

TSF 负责与应用交互，Broker 承载输入会话，候选栏呈现结果，设置程序管理配置和主题。详见[架构说明](docs/ARCHITECTURE.md)，或从 [TSF](src/tsf)、[Broker](src/broker)与[核心逻辑](src/core)阅读源码。图为原创示意，非产品截图。

</details>

## 参与改进

欢迎可复现的兼容性问题、主题适配、文档改进和小范围修复。报告问题时，请给出字流版本、Windows / 应用版本和最小复现步骤，使用合成输入，避免上传私人文档、个人词库或原始诊断日志。

[报告问题](https://github.com/matsuokajuri/Ziliu-IME/issues/new) · [贡献指南](CONTRIBUTING.md) · [安全问题](SECURITY.md)

感谢 [Rime](https://github.com/rime/librime)、[雾凇拼音](https://github.com/iDvel/rime-ice)及相关上游项目。字流沿用现有 [GPL-3.0-only](LICENSE) 声明；依赖保留各自许可，见[第三方声明](THIRD_PARTY_NOTICES.md)。本页原创 SVG 按仓库现有许可证提供。

<a id="english"></a>

## English at a glance

**Ziliu (字流)** is a native Chinese input method for Windows, combining librime and Rime Ice with TSF integration, a customizable candidate window, WinUI 3 settings and partial Sogou SSF skin support. This is the main source repository.

The [Alpha download](https://github.com/matsuokajuri/Ziliu-IME/releases/tag/v0.1.0-alpha.1) is **unsigned and test-only**, for Windows 11 x64. Start with the [installation guide](docs/getting-started.md) or [development guide](docs/DEVELOPMENT.md). Contextual ranking remains research; no model or demonstrated AI improvement is released. The cover is original concept art, not a product screenshot. The project retains its existing [GPL v3 license](LICENSE).
