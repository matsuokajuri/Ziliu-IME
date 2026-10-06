# 字流架构

[← 项目首页](../README.md) · [开发指南](DEVELOPMENT.md) · [原生技术栈决策](adr/0001-native-windows-stack.md)

字流把宿主内的输入服务与输入引擎分开：应用进程中的 TIP 处理 TSF 交互，独立 Broker 承载 librime 与用户词库，设置程序在用户打开时运行。

![字流常规输入的数据流与进程边界](assets/architecture.svg)

## 模块入口

| 模块 | 职责 | 源码 |
| --- | --- | --- |
| Core | 组合态、引擎接口、配置、主题与 IPC 编解码 | [`src/core`](../src/core) |
| TSF | COM 生命周期、按键、编辑会话、组合与提交 | [`src/tsf`](../src/tsf) |
| Candidate UI | Win32 窗口、Direct2D / DirectWrite 呈现与定位 | [`src/ui`](../src/ui) |
| IPC | 版本化本机命名管道的客户端与服务端 | [`src/ipc`](../src/ipc) |
| Broker | 会话管理、librime 适配、用户词库及资源请求 | [`src/broker`](../src/broker) |
| Settings | WinUI 3 设置、主题预览与部分 SSF 导入 | [`src/settings`](../src/settings) |
| Data overlay | 固定雾凇版本之上的首方定制 | [`data/ziliu`](../data/ziliu) |

WinUI 3 设置工程为 `ZiliuSettings.vcxproj`，与 CMake 中的基础设置目标分别构建。

## 输入与数据边界

1. TSF 输入服务检查模式与宿主状态，在编辑会话中处理组合和提交。
2. 本机命名管道向 Broker 发送输入操作，返回组合串、候选与设置等状态。
3. Broker 通过首方适配层使用 librime；雾凇系统数据只读，用户学习写入本地用户数据。
4. 原生候选栏呈现结果；设置程序保存本地配置和主题，供输入服务与 Broker 读取。

管道以当前用户 SID 限制访问。TIP 不承载大型模型或用户数据库；数据库写入由 Broker 管理。开发机缺少 Rime 时可以使用 Stub，发行测试包则要求完整且经过校验的真实运行时和数据。

程序与只读词库位于安装目录；用户设置、主题和 Rime 用户数据位于 `%LocalAppData%/Ziliu`。卸载保留这些用户数据。当前发布代码不包含云同步、在线候选或遥测模块；这些边界仍需结合实现与具体宿主验收评估，不能当成对任意应用隐私行为的保证。

## 失败与验证

输入服务应在 Broker 或编辑会话不可用时结束当前组合并安全放行。配置和主题解析采用有界数据与明确所有权；引擎、IPC、UI 和安装生命周期的具体失败场景需要各自测试。

内存、空闲 CPU、IPC 与候选刷新延迟属于持续评测目标。当前文档不把预算写成已实现的性能数字。发布记录的限定验收范围见 [ALPHA-RELEASE.md](ALPHA-RELEASE.md)。

上下文排序、小模型接入与更深融合仍处规划 / 实验阶段，不在上图的常规发布路径中，也不代表已发布 AI 能力。
