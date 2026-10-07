# 离线评估契约与配对核心

这两个标准库模块为字流 / Ziliu 的离线研究提供输入契约、共同上下文计划、开发校准证据校验和结果配对。它们复用本目录已有的候选来源/保护策略，未接入产品、TIP 或 Broker。

本目录提供核心 Python API。三模型运行 controller、模型 adapters、进程 Job 工具、可训练模型和 policy-loss 实现不在本次公开范围内；模型角色名只是配对标识。没有附带模型、语料、评测输入或实验结果，也没有准确率、性能或生产可用性结论。

## 模块与调用边界

| 模块 | 提供的能力 |
| --- | --- |
| [offline_evaluation_contracts.py](../../scripts/offline_evaluation_contracts.py) | 共同上下文计划、显式准入与预注册字段校验、完整开发锁及收据的一致性检查 |
| [offline_pool_evaluation.py](../../scripts/offline_pool_evaluation.py) | 无标签推理输入校验、受限 scorer 回调、单一开发校准规则、封存预测核对、按原始行身份配对及固定分母计数 |

`score_case(case, scorer)`调用由调用者明确提供的`scorer.score(request)`，不会查找、下载或加载模型。请求严格只有`prefix`、`pinyin`及候选的`source_index/text`。推理case、candidate、source和预处理字段采用明确白名单；参考字段别名及未知扩展会被拒绝。

准入必须显式提供严格bool的`ordinary_scope`、`current_prefix`和`production_requested`，并注明结构性`context_quality`。来源事实与真实完整提交证据由合格采集器提供，不能由模型分数推断。收据摘要是调用者保管的出处引用，不认证实际输入场景或数据权限。

使用`context_plan(prefix, admitted_candidates)`固定共同预算`min(48, 63-max_candidate_scalars)`及实际保留的左侧后缀、摘要和候选原位置。case中的计划必须与重新计算的结果一致；保留上下文为空时不评分。原菜单位置及排除的孔位保持不变，评分最多覆盖原始前九项中的合格候选。

## 校准与计分

严格策略保持既有的条件差、上下文增益差及最佳/第二项差阈值`1 / 1 / 0.25`。条件策略默认禁用；原始dict不能作为可执行锁传入。`verify_lock`要求完整v2证据、严格bool、开发seal及当前模型/预处理/策略/计划/源码绑定一致，然后返回供受信调用链使用的typed对象。

`calibrate_conditional`只接受开发行，以不安全提议的最大gap为单一边界。安全替换与净改善分别计数：原生和提升项均可接受的替换不算correction。启用需要留存净改善覆盖至少两个家族、两个文档，并存在开发不安全边界；缺失该边界或不足收益时禁用。实际家族和可接受集必须由保管评估标签的一方在查看模型分数前独立冻结。

`verify_prediction_bundle`及`pair_results`要求三角色完整预测、相同输入/协议摘要、预测摘要和严格有序行身份；partial、timeout或错位行不能参与有效配对。结果保留所有行、参考目标分母、候选召回、correction/loss、控制干预及决策原因。可选的原引擎候选列表用于区分局部恢复与相对原产品的净变化。

`strict_reference_targets`只度量完整字符串的精确复现，不是完整语义可接受集。未知或多答案情况需另行冻结计分规则，不能根据模型结果补标签。

## 调用者仍需负责的事项

这些API检查给定对象的一致性。它们不执行完整实验流程，不打开或保管标签，不强制一次性校准目录，也不提供生产字段认证或密码学隔离。调用者须事前固定开发/确认划分、实际家族、准入、版本、证据摘要和单次校准规则；先封存所有预测，再向独立配对步骤提供标签。确认数据不得用于调阈值或改变准入。

`score_case`的超时检查发生在回调返回后，不能中断阻塞的模型调用。真实运行需要另行实现和验证硬时间/进程树内存限制、异常清理、模型环境出处及数值资格。合成测试通过不代表这些事项已通过。

## 无模型测试

在仓库根目录运行：

```powershell
python -B -I -W error -m unittest discover -s tests -p "test_offline_evaluation_core.py"
python -B -I -W error -m unittest discover -s tests -p "test_offline_*.py"
```

[独立核心测试](../../tests/test_offline_evaluation_core.py)只使用标准库、人工字符串、toy scorer和合成收据，没有导入模型runtime、adapter、训练工具或原始冻结实验fixture。现有公共helper的更严格输入guard保持原样。

项目根[LICENSE](../../LICENSE)及[第三方声明](../../THIRD_PARTY_NOTICES.md)保持不变；既有reader的来源与许可见[研究工具说明](offline-character-ranking.md)。内容摘要不授予模型或语料分发权。
