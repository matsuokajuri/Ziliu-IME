# 排序学生：训练数据桥接与受限工作进程

字流 / Ziliu 的离线研究工具可以将明确批准的旧训练行迁移到直接排序目标，并提供受限训练工作进程。默认命令只检查数据和生成准备摘要；实际训练需要单独批准的资源窗口及控制器。本仓库不包含训练语料、教师响应、资格报告、预测或学生权重，也不提供准确率或生产可用性结论。

模型结构见[共享上下文排序学生](shared-ranking-student.md)，原生候选和来源保护见[评估核心](evaluation-core.md)。

## 数据身份与划分

入口接受一个带外 SHA256 固定的 manifest，明确列出四份训练专用 JSON：`native_cases`、`native_admission`、`labels`、`lineage`。每份文件有固定字节摘要和大小上限；不扫描目录、不猜路径、不模糊连接或静默丢行。JSON 重复键及非有限常量会被拒绝。

|文件|检查范围|
|---|---|
|`native_cases`|原生输入、同字段上下文、候选来源 ID、完整提交和上下文预处理|
|`native_admission`|原生文件字节摘要、收集和隐私收据、逐条 scope 与上下文摘要|
|`labels`|显式 train／非 heldout 行、已准入旧标签状态、完整候选目标和响应来源摘要|
|`lineage`|逐条标签／原生行摘要、文档与家族划分、排除集合及来源收据声明|

四份文件的行 ID 集合必须完全一致。标签中的 request 必须与原生门控生成的保留上下文、拼音、候选顺序、ID 和文本完全相同。同一完整 request 不得用不同 ID 重复输入。词表只从已准入训练 request 的文本构建，不读取开发／确认文本或 language anchors 来构建词表。

划分清单拒绝文档或家族跨 train／development／confirmation 边界，且训练记录必须恰好对应已声明的 train 成员。已消费的模板、开发和探索集合，以及明确排除的文档、家族、request 摘要保持排除。哈希和不透明 ID 可以核对声明的一致性，不能自行证明签发者、历史使用、近重复隔离或真实文档独立性；这些须由获得授权的数据维护者审核。

旧接口的一致硬选择按 one-hot 目标保留，不包装成教师置信度。评估专用资格标签不等于训练准入。重复文本仍保留原始候选 ID，但无法区分的相同文本不得带矛盾硬选择；原有语义集合也继续拒绝矛盾成员资格，不自动重写标签。

## 默认准备命令

从仓库根目录执行；路径由已批准的调用方显式提供，输出须为不存在的文件：

```powershell
python -B -W error -m tools.offline.ranking_student.train --manifest ./approved-train/manifest.json --manifest-sha256 <sha256> --out ./PREPARATION.json
```

准备模式不导入 Torch、NumPy 或 ORT，不申请资源锁或执行优化。合成 fixture 永远不成为真实训练 READY。训练准备要求至少64个不同 request、8篇文档及8个家族，同时检查首次训练的 S/T token 上限；这些是工程准入条件，不是统计独立性证明。

## 工作进程与预算

`--run` 仅供经审核的控制器启动。它先检查独立 permit，再读训练数据；在导入重运行库前核对解释器、运行库版本、全部工作进程与门控源码摘要、实时资源租约和实际 Windows Job 限制。环境变量及本地收据用于防止误用，不是密码学授权。

工作进程从控制器复制**仅 QUERY 权限、不可继承**的精确 Job 句柄，确认当前进程属于该 Job，再检查内存、CPU affinity、below-normal priority、kill-on-close 和禁止 breakaway。查询句柄随后关闭。外部控制器负责原子启动、watchdog、Job 清空和释放租约；工作进程不充当控制器，也不自行杀进程。

普通训练固定为 CPU2、2GiB Job、最长300秒租约，并预留30秒清理时间。采用随机初始化、FP32、batch=1、固定种子与 SGD；固定上限64个不同训练 request，各执行一次，先覆盖已声明组再填满步骤。首次训练继续 K9，并要求 S/T≤128；不循环模板、不选开发 checkpoint、不自动延长或重试。每次更新前检查预算、有限 loss 和全部梯度，更新后检查参数有限性。

独立的 `synthetic_pipeline_qualification` permit 仅允许1–4条有界合成记录，使用4GiB Job，其余预算约束相同。它验证训练控制流程，不能授权真实数据训练或用作准确率证据。仓库不附控制器启动器；实际执行需要额外批准并满足 permit 与实时租约要求。

## 保存与回读

完成预定阶段后，工作进程以独占创建方式写 `FINAL.npz`，仅保存当前模型的命名参数。随后用 `allow_pickle=False` 回读，核对完整名称集合、FP32 dtype、shape 和逐元素一致性；合成资格路径还包含重建模型后的固定输入预测回读检查。该过程核验本轮新写出的文件，不接受外部 checkpoint、resume 或任意权重文件。

完成回读和最后预算检查后才产生有效 checkpoint 收据。超时或异常不产生完成状态；磁盘上可能已有部分文件，不能据此认定合格。`TOKENIZER.json`、`TRAINING-RESULT.json` 及模型文件属于执行侧输出，不是本仓库发布内容。

## K32 与测试范围

`Config(max_candidates=32)` 显式启用最多32项；默认仍为9项。Codec 保留每个完整候选的文本、来源 ID 和 mask，超出数量、字符或 token 边界时拒绝，不截断。模型参数量不随 K 改变；K、候选 token 长度 T 和 source 长度 S 是不同维度，K9 的测量不能替代 K32 的测量。原生桥接、训练和旧比较协议继续使用各自的 K9 门控。

独立无模型轻量测试包括[训练桥接测试](../../tests/test_ranking_student_training.py)和[精确 Job 查询／额外身份测试](../../tests/test_ranking_student_training_window.py)：合成数据身份、划分与排除、矛盾目标、预算中断、默认准备无运行库，以及 K32 的完整 UTF-8 fallback、批次和字符上限。Job API 使用模拟对象，没有创建实际 Job。控制流程的 Mock 更新不等于真实 SGD 通过；轻量测试不执行 Torch、NPZ 数值回读或任何模型。

```powershell
python -B -I -W error -m unittest discover -s tests -p test_ranking_student_training*.py -v
```

实际优化、数值保存恢复、K32 模型行为、延迟、候选准确率和生产接入是不同证据层级，须在另行批准的窗口核验。现有产品 CI 不运行这些 Python 研究测试；本增量未新增工作流，生产提升保持关闭。
