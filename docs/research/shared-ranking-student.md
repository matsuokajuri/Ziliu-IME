# 共享上下文直接排序学生（研究原型）

字流 / Ziliu 的可训练排序源码原型：同一候选池共享左上下文与拼音编码，每个候选独立编码，再输出直接排序 logits。默认配置按源码计算为 **10,126,849 个参数**。

目前提供代码与合成测试，**没有发布训练后的学生权重**，没有准确率提升、默认 10M 实例实际延迟、量化等价或生产可用性结论。生产排序保持关闭。本模块未接入 TIP、Broker、设置页或默认产品构建。

## 结构与输入

```text
prefix + SEP + pinyin → 共享 context encoder → 一次 K/V 投影
each candidate       → 独立 candidate encoder → Q → context attention
                     → 共享标量 head → 每个原始 source_index 的 rank logit
```

默认几何为 width 256、4 heads、FFN 1024、7 层 context encoder、2 层 candidate encoder、8192 项共享 embedding 与 `1024 → 512 → 1` head。候选之间不做 self-attention，不输入候选 ID、原始名次或标签作为特征。

| 部件 | 参数 |
| --- | ---: |
| 共享 token embedding | 2,097,152 |
| source / candidate position | 66,304 / 64,768 |
| segment embedding | 768 |
| context encoder | 5,528,320 |
| candidate encoder | 1,579,520 |
| encoder final norms | 1,024 |
| cross projections 与 norm | 263,680 |
| score head | 525,313 |
| 合计 | **10,126,849** |

模型 payload 严格限于：

```python
{
    "prefix": "同字段左上下文",
    "pinyin": "yuanyangpinyin",
    "candidates": [{"source_index": 0, "text": "完整候选"}, ...],
}
```

每池最多 9 项，保留原始 ID 和 native leader；相同文字的不同 ID 继续保留。最多 48 个左文标量、64 个 ASCII 拼音字符、63 个候选标量及 8 个请求一批。字符表与 UTF-8 byte fallback 保留不同 OOV 字符，不做 Unicode 归一化；source/candidate token 上限为 259/253。超界拒绝整池，不截断候选。

这些是模型容量边界。同池比较仍采用已有 [evaluation core](evaluation-core.md) 的共同上下文预算 `min(48, 63-max_candidate_scalars)`；训练收集方也须预先应用并冻结相同处理。词表来自另行批准的 train 材料并钉住摘要，本目录不附真实词表或语料。

## 模块与工程接法

实现位于 [tools/offline/ranking_student](../../tools/offline/ranking_student)：

| 模块 | 用途 |
| --- | --- |
| `config.py` | 有界几何与逐模块参数账本 |
| `codec.py` | 字符编码、byte fallback、batch padding 与原始 ID 映射 |
| `contracts.py` | 模型 payload、仅 train 监督及声明的分组隔离检查 |
| `reference.py` | 标准库损失与梯度参考 |
| `model.py` | 延后导入 PyTorch 的模型与一次更新原语 |
| `bridge.py` | 复用已有候选准入、共同上下文和保护 gate 的诊断桥 |
| `__main__.py` | 仅输出参数账本，默认不导入 tensor 库 |

从仓库根目录运行参数检查与轻测试：

```powershell
python -B -m tools.offline.ranking_student
python -B -I -W error -m unittest discover -s tests -p test_ranking_student_stdlib.py -v
```

源码包和标准库套件不需要新增依赖。`bridge` 调用前需按既有离线工具惯例把仓库 `scripts/` 加入模块路径；它使用现有 `offline_pool_evaluation`、`offline_evaluation_contracts` 及其 source/protection helper，不替换这些模块的 guard。

模型分配和更新另外需要已审 PyTorch、单独获准的资源窗口及外部限时/限内存 Job。`runtime_authorized=True` 是调用方的前置条件断言，不申请租约，也不证明权限。没有 install、下载权重、教师调用或训练 CLI。真实使用时由调用方验证依赖、来源、数据准入和资源回收。

## 训练与保护边界

温度固定为 1，损失为每池等权的 `listwise + 0.2 * pairwise`：教师分布使用 CE 与 soft pair BCE；独立语义集合使用可接受集合的概率质量，只在可接受与不可接受之间建 pair。两个正确答案不互为负例。

仅接受声明为 train、非 heldout、用于 optimizer 且与完整有序请求摘要一致的监督。未决、无答案、并列、分歧或拒答不伪造 native 正例，没有已解决监督就拒绝更新。语义集合中同原样文字的候选必须具有一致成员资格；冲突时拒绝并交回标注审查，保留所有 ID，不静默重写标签或教师分布。

文档、家族、准入与监督元数据不进入模型张量。SHA 与 split 声明只检查引用/一致性，不能自行证明许可、隐私或历史暴露隔离；真实材料须先经独立准入。代码的教师名称 allowlist 不授予教师权重或数据使用权。

诊断桥在模型前拒绝非普通 scope、过期或空上下文、生产请求、弱/未知上下文以及受保护或未知 native 来源，继续保留已有 userfixed/history 规则。输出为 `rank_logits`，`promotion=None`、`production_enabled=False`；不伪装成旧 C/BOS/gain，不沿用其阈值，也未注册为已有评估 CLI 的新模型角色。

没有跨请求的上下文或 KV cache；每次 forward 重新计算本请求 context，K/V 在池内共享。tokenizer 摘要首次使用后固定。这个脚手架不认证生产 field identity、dictionary epoch 或调用方提供的 scope facts。

## 验证与后续工作

[标准库套件](../../tests/test_ranking_student_stdlib.py) 覆盖参数公式、编码、契约、损失参考、重复文字标签与既有保护桥。[延后 tensor 套件](../../tests/test_ranking_student_runtime.py) 使用随机 tiny 配置，覆盖 shape/numel、梯度、损失参考、S/T padding、批次位置、候选扰动隔离、共享计算次数与请求/词表绑定。它默认跳过，须在另行获准的 owned Job 内执行；不要把环境开关当成资源授权。

合成资格检查与默认 10M 模型训练、真实候选效果、P95 延迟及量化图等价是不同证据。已有产品 CI 的 CTest 不执行 Python research 套件，本变更没有新增 workflow。后续实验先核对原生池中是否有可接受答案，分别报告全拼、简拼和混拼召回，再谈同池排序；保留原 native 基线、文档/家族分组及完整分母。

本代码沿用根 [LICENSE](../../LICENSE)。已有参考 reader 的 attribution 与 [第三方声明](../../THIRD_PARTY_NOTICES.md) 保持原样；本变更不复制其权重、词表、语料或依赖二进制。
