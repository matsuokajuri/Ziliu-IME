# 第三方组件声明

当前构建会把雾凇拼音数据放入 Broker 数据目录，并在本地缓存存在时放入经过校验的
librime 官方运行时：

| 项目 | 用途 | 许可证 | 固定 commit |
|---|---|---|---|
| rime/librime | 输入引擎 | BSD-3-Clause | `33e78140250125871856cdc5b42ddc6a5fcd3cd4` |
| iDvel/rime-ice | 默认拼音方案与词库 | GPL-3.0 | `b681a34f788795034b3b288830f4861980bc8b0d` |
| BYVoid/OpenCC | 简转繁词组及多义字词典 | Apache-2.0 | `556ed22496d650bd0b13b6c163be9814637970ae`（librime 的依赖锁定版本） |
| amzxyz/rime-wanxiang | 用户学习长度参数参考 | CC BY 4.0 | `wanxiang` 分支（2026-07-23） |

librime Windows MSVC x64 运行时取自官方 `1.17.0` Release，资产名
`rime-33e7814-Windows-msvc-x64.7z`，SHA-256 为
`7478c7caa4ff6b37de86daba1f7ce4a994a4f5ba24872a820fb2b3a9b01fed15`。

雾凇拼音内部数据还包括 Unicode License、Public Domain、MIT、LGPL-3.0、CC BY 3.0
等来源。发行前必须从上游 `Credits.md` 生成完整、逐项可追溯的声明，不能只保留本表。

所有第三方项目保持其原许可证；字流的 GPL 许可证不会替换这些声明。

`data/ziliu/opencc/STPhrases.txt`、`STCharacters.txt` 未修改地取自
`https://github.com/BYVoid/OpenCC/tree/556ed22496d650bd0b13b6c163be9814637970ae/data/dictionary`。
完整许可证保留于同目录的 `LICENSE.OpenCC`。
本地 `s2t.json` 沿用该版本的词组优先转换链，仅将 `ocd2` 数据格式改为
OpenCC 原生支持的 `text` 格式，以免要求额外的词典编译工具。

源文件 SHA-256：

- `STPhrases.txt`: `17fece21a28f3db2dc32397abd73d21ae73261c3f295fa6a3fe64b5e8d8b554b`
- `STCharacters.txt`: `ed1d268e0ad028511dcf5b0089faed0a980ad332449ec11d481ceefde6879f41`
