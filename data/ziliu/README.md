# 字流数据覆盖层

这里仅保存字流相对于雾凇拼音的差异。构建或打包时先复制固定 commit 的
`third_party/rime-ice`，再覆盖本目录中的文件。

规则：

- 不直接修改 `third_party/rime-ice`。
- 每个新增词必须记录来源或说明为字流原创整理。
- 自动生成的词表必须保留生成脚本、输入数据版本和许可证。
- 用户词库永远不进入 Git。
- 上游更新后必须运行候选排序和纠错回归测试。

当前覆盖层保留雾凇拼音的词库和 spelling algebra，并启用 librime 原生
QWERTY 邻键容错。用户学习继续写入 `rime_ice.userdb`；`core_word_length: 4`
与 `max_word_length: 7` 参考万象拼音 Pure 方案，用于学习短语片段并阻止偶然
长句污染用户词库。

逻辑来源：

- librime `NearSearchCorrector` 与 `ScriptTranslator`，BSD-3-Clause；
- iDvel/rime-ice 全拼纠错规则，GPL-3.0；
- amzxyz/rime-wanxiang Pure 的用户学习长度策略，CC BY 4.0。
