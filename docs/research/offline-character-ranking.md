# 离线字符排序研究工具

这些模块用于离线研究，未接入字流产品、TIP 或 Broker。没有随代码发布模型、训练语料、冻结评测集或实验结果，也没有准确率、性能提升或生产可用性结论。

## 模块与边界

| 模块 | 用途 | 需要外部保证的事项 |
| --- | --- | --- |
| [offline_rerank_protocol.py](../../scripts/offline_rerank_protocol.py) | 候选身份、admission、实验隔离、固定分母指标及请求字段 allowlist | 调用方必须实际使用 `scoring_request` 的返回对象；字段过滤不认证文本来源 |
| [offline_candidate_evidence.py](../../scripts/offline_candidate_evidence.py) | 将 native span、剩余输入和完整提交证据转为候选事实；未知证据保留 unknown | `qualified_observer` 是外部断言；nonce 关联身份，不提供来源认证 |
| [offline_character_model.py](../../scripts/offline_character_model.py) | 固定格式的 CPU PyTorch reader、独立重算和短生命周期缓存 | 包来源、隔离 worker、硬超时与内存预算，以及真实数值等价性 |

评分请求只包含 `prefix`、`pinyin` 和候选的 `source_index/text`。研究标签、family、split 与其他 metadata 不应传给模型。当前代码提供这一 API；完整实验调用链是否遵守约束仍需单独检查。

reader 延迟导入 Torch，不自动下载或安装依赖。实际构造 reader 要求显式提供受限的精确 Torch 版本，以及与源码固定大小、SHA-256、结构一致的模型文件。版本相等不认证包来源，非空 attribution 也不证明全部语料权利链。

缓存仅保留于进程内，由调用方传入不可变 scope。异常路径会清理引用，`clear()` 与评分串行；这不是内存擦除保证，也不是生产字段身份认证。上下文与候选有明确上限，评分使用候选 log-probability 的平均值。固定参考格式不表示与上游 Rust runtime 位级等价或具有其性能。

## 无模型测试

在仓库根目录运行，测试仅用 Python 标准库、人工输入和模拟状态：

```powershell
python -B -I -m unittest discover -s tests -p "test_offline_*.py"
```

这些测试覆盖 admission、alias 隔离、请求字段过滤、候选事实边界、输入上限和缓存异常清理。它们不构造模型、不导入 Torch、不读取权重，也不证明真实模型的准确率、数值等价或资源预算。

运行真实实验前，应另行完成语料与模型许可核查、collector/selection capture admission、请求调用链核查和 worker 资源约束。不要使用真实私人输入或把生成数据、原始日志、权重加入公开提交。

## 来源与许可

reader 是第一方 PyTorch 实现，参考 [chinese-ime-lm 的固定 reference](https://github.com/metasequoiaime/chinese-ime-lm/tree/f4a3fc007fba051695ae8300de917bb824d458ba/reference) 格式与架构。该目录采用 Apache-2.0；上游根训练代码有独立许可，不能混写。

模型来源声明参考[固定模型卡](https://huggingface.co/metasequoiaime/pinyin-ime-reranker-4M/blob/e5b1f7e768d7cb2b6ff334db4e34af153920c6ff/README.md)。这只是来源记录，本目录不分发权重。保留的上游材料包括 [reference LICENSE](../licenses/chinese-ime-lm-reference-LICENSE.txt)、[model LICENSE](../licenses/pinyin-ime-reranker-model-LICENSE.txt) 与 [model NOTICE](../licenses/pinyin-ime-reranker-model-NOTICE.txt)。再分发任何权重前仍需核对其 attribution 和完整许可义务。

项目根 [LICENSE](../../LICENSE) 保持不变；参见[第三方声明](../../THIRD_PARTY_NOTICES.md)及[研究接口设计](offline-ranking-contract.md)。
