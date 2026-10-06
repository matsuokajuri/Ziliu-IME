# Cassotis 离线评分研究适配器

本目录是源码审阅用的独立研究工具。它不接入字流 TIP、Broker 或默认构建，不分发模型、词表、语料、ORT wheel、native binary 或实验数据。数值可靠性、真实模型效果与生产接受均未完成，不能据此宣称排序或性能提升。

## 代码导航

| 模块 | 范围 |
| --- | --- |
| [cassotis_onnx_adapter.py](scripts/cassotis_onnx_adapter.py) | 候选 packing、独立无 cache 参考和显式诊断 cache；保持原候选顺序 |
| [cassotis_runtime_provenance.py](scripts/cassotis_runtime_provenance.py) | 纯标准库：验证固定官方 wheel、vendor 文件集合与成员字节，再绑定已导入 ORT 来源 |
| [cassotis_asset_pins.py](scripts/cassotis_asset_pins.py) / [audit_cassotis_assets.py](scripts/audit_cassotis_assets.py) | 固定来源与 required inventory，独立检查 Merkle tree、必要文件及 model parts |
| [prepare_offline_ort.py](scripts/prepare_offline_ort.py) | 从调用方已有且通过 pin 的 wheel 准备新的离线目录；不下载依赖 |
| [cassotis_float_reference.py](scripts/cassotis_float_reference.py) | 小型合成 FP32 / activation QDQ 语义对照；不调用真实 ONNX 模型 |

当前目录不包含评测 worker、原冻结 controls 或其绑定测试。合成单元测试使用独立的 toy scorer、人工输入和临时 fixture；不读取私人评测集。

## 使用边界

cache 默认禁用，只能显式作为诊断启用。它受 owner thread 和不可变 scope 约束；缺 scope、非法输入、异常与清理拒绝有单元覆盖。清理释放引用，不提供内存擦除保证。

`score_independent` / `score_independent_sums` 对每个候选分别计算无 past 的参考分数，并从整个候选池推导共同保留 context，避免因候选顺序更改 prefix。sum 与 mean 是不同的评分约定；当前字符接口不包含 EOS。独立参考、packed 与 cache 的真实数值等价仍需独立验收，合成 FP32 或 QDQ 对照不能定位真实 ORT kernel 根因。

实际加载 session 前，调用方需要可信、独占的 worker：先运行纯标准库验证，再使 vendor 可导入，最后加载 adapter/NumPy。直接导入 adapter 会导入当前环境的 NumPy；ORT wheel 校验不是整个 Python 环境、NumPy 或系统 DLL 的来源认证。验证后目录还须由调用方保持不可变。

调用方另负责隐私 admission、完整候选事实、固定请求/阈值、整个进程的硬超时与内存/CPU 预算。单次 ORT 超时不等于全程 harness 预算。资产摘要、inventory 与 Git 对象完整性不授予模型或训练语料的分发权限；权重与语料权利链仍待核查。

## 无模型测试

已有环境需提供 NumPy；不自动安装依赖。在仓库根目录运行：

```powershell
python -B -I -m unittest discover -s research/cassotis/tests -p "test_cassotis_*.py"
```

这些测试不构造真实 ORT session、不读取模型，也不证明模型准确率、量化分数可靠性或生产表现。`-B` 防止创建未验证 bytecode；验证器会拒绝 vendor 的额外文件。

来源、GPL attribution 与运行依赖声明见 [NOTICE](NOTICE.md)。项目根 [LICENSE](../../LICENSE) 保持不变，另见[第三方声明](../../THIRD_PARTY_NOTICES.md)和[离线接口设计](../../docs/research/offline-ranking-contract.md)。
