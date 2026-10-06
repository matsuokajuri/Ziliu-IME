# 候选来源与保护语义

这是独立的离线研究接口，用于区分“候选来自哪个来源”和“是否必须保留显式偏好”。它未接入字流产品，不改变现有 `offline_rerank_protocol` 的门槛，也不提供模型效果或生产接受结论。

## 分类与比较

| 来源或证据 | 接口处理 |
| --- | --- |
| 普通系统词条或原生组句，整个输入和字面拼写已证明 | 可以进入受限离线比较 |
| 显式选择或专用固定短语 receipt | 整个候选池保留原顺序；receipt 只能增加保护 |
| 尚未解析偏好语义的用户学习词、表格来源或未知来源 | 首候选未解析时拒绝干预；其他未证明候选不参与比较 |
| 缺失前文、过期前文、禁止或未知 scope | 拒绝比较 |

普通字典命中不等同于用户的显式选择或固定短语偏好。`source_evidence()` 只解释外部提供的候选事实，`isolated_schema_qualified` 是调用者的资格断言；原生类型字符串不能自行认证任意 translator。此函数不验证 capture 文件、nonce、Windows 字段或 dictionary epoch。

`research_gate()` 接受显式候选/证据数组，最多9个槽位，前文最多16,384个 Unicode scalar。参与评分的完整候选为1–63个 scalar，不截断候选。`SourceEvidence` 与文本的逐槽对应由调用者保证，返回 indices 是本次数组的槽位，不是原始 `source_index` 的独立认证。

`decide()` 要求 conditional 分数与 contextual gain 选择同一候选，并严格超过条件分数差、上下文增益差和条件分数的第一/第二名间隔阈值。平局保留原顺序。阈值必须有限且非负，但不是校准后的干预概率；本目录没有发布任何冻结评测阈值。

调用者明示 `production_requested=True` 时直接拒绝。默认研究标志、传入的布尔值及来源对象均不建立生产权限或隐私认证，不能替代真实字段、epoch、截止时间、模型版本和资源约束。

## 无模型验证

从仓库根目录运行：

```powershell
python -B -I -m unittest discover -s tests -p "test_offline_*.py"
```

新增测试仅使用人工候选、合成分数与标准库 mock，覆盖来源/保护区分、显式偏好、未知来源、前文/scope、共同分数门槛、非有限值，以及候选容器和输入上限。它们不导入 Torch、不构造模型、不读取训练或评测数据，也不证明完整请求链、原生观察器或实际模型资格。

源码与测试基于固定研究源码 `bc22eac8c5400a7f7f160ef3367bda27aca02d3b`，公开副本另补显式容器/证据对象和编码前的前文上限检查。没有附带训练工具、冻结样本、成绩、模型或私人证据。项目根 [LICENSE](../../LICENSE) 保持不变；继续阅读[离线字符研究工具](offline-character-ranking.md)与[接口设计](offline-ranking-contract.md)。
