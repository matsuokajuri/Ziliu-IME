# 第三方组件声明

当前骨架尚未把第三方源码或词库编入产物。下一阶段将以固定 commit 引入：

| 项目 | 用途 | 许可证 | 固定 commit |
|---|---|---|---|
| rime/librime | 输入引擎 | BSD-3-Clause | `d4c324ca988ed67f45e41524c2ab01d40cb55695` |
| iDvel/rime-ice | 默认拼音方案与词库 | GPL-3.0 | `b681a34f788795034b3b288830f4861980bc8b0d` |

雾凇拼音内部数据还包括 Unicode License、Public Domain、MIT、LGPL-3.0、CC BY 3.0
等来源。发行前必须从上游 `Credits.md` 生成完整、逐项可追溯的声明，不能只保留本表。

所有第三方项目保持其原许可证；字流的 GPL 许可证不会替换这些声明。
