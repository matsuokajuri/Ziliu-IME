# 搜狗 SSF 到 ZLT v1 的同窗映射

字流的 SSF 适配范围只包含候选窗口同窗布局：

- `Scheme_H1` 映射为 `appearances.light.surfaces.horizontal`。
- `Scheme_V1` 映射为 `appearances.light.surfaces.vertical`。
- `Scheme_H2`、`Scheme_V2` 是分窗布局，首版直接跳过。
- `StatusBar` 是搜狗自有状态栏，不改变字流当前的 Windows 输入法指示器和右键菜单。

导入器同时支持两类 SSF 容器：

- 旧版标准 ZIP（根目录包含 `skin.ini`）。
- 当前搜狗使用的 `Skin v3` 容器（AES-CBC、zlib 与内部偏移表）。

两类格式都会先经过大小、路径、条目、校验和与图片解码限制，再读取 UTF-16LE
`skin.ini`。所有被引用的 BMP、PNG、JPG 或 GIF 图片会由 WIC 解码并重新编码成干净的
PNG，最终只安装转换和复验后的 ZLT 目录。TIP 与候选窗口不会直接读取 SSF。

## 元数据

| SSF | ZLT |
| --- | --- |
| `General.skin_id` | `id`，规范化为 `sogou.<skin_id>` |
| `General.skin_name` | `name` |
| `General.skin_author` | `author` |
| `General.skin_version` | `version` |
| `General.skin_info` | `description` |
| `General.preview_square` 或 `preview_comp` | `preview` |
| 固定值 | `source_format = "sogou-ssf"` |
| SSF 未声明许可证 | `license = "LicenseRef-Unknown"` |

## 字体与颜色

| SSF `Display` | ZLT `appearance` |
| --- | --- |
| `font_ch` | `typography.chinese_font_family` |
| `font_en` | `typography.english_font_family` |
| `font_size` | `typography.font_size` |
| `pinyin_color` | `palette.preedit_text` |
| `zhongwen_color` | `palette.candidate_text` |
| `zhongwen_first_color` | `palette.highlighted_candidate_text` |
| `comphint_color` | `palette.muted_text` |

SSF 颜色整数按低字节到高字节依次表示红、绿、蓝。转换公式为：

```text
R = value & 0xFF
G = (value >> 8) & 0xFF
B = (value >> 16) & 0xFF
```

例如 `0x0080FF` 转为 ZLT `#FF8000`，不能把原整数直接当作 `#0080FF`。

## H1/V1 表面

以下规则分别应用于 `Scheme_H1` 和 `Scheme_V1`：

| SSF | ZLT surface |
| --- | --- |
| `pic` | `background.asset` |
| `layout_horizontal=mode,left,right` | `background.layout.horizontal` 与 `background.stretch` 的左右值 |
| `layout_vertical=mode,top,bottom` | `background.layout.vertical` 与 `background.stretch` 的上下值 |
| `pinyin_marge=top,bottom,left,right` | `content.preedit=[left,top,right,bottom]` |
| `zhongwen_marge=top,bottom,left,right` | `content.candidates=[left,top,right,bottom]` |
| `separator=color,left,right` | `separator.color`、`separator.left`、`separator.right` |

SSF 布局模式按方向精确映射：

| 字段 | mode 0 | mode 1 | mode 2 |
| --- | --- | --- | --- |
| `layout_horizontal` | `stretch` | `tile` | 无效，拒绝导入 |
| `layout_vertical` | `stretch` | `tile` | `fixed` |

其他 mode 同样作为错误报告，不能静默退化成 `tile`。`fixed` 保持源图高度，不在该方向
拉伸或平铺。未声明分隔线宽度的 SSF 使用 ZLT 默认 `thickness=1`。

例如：

```ini
[Scheme_H1]
pic=skin1.png
layout_horizontal=0,38,192
layout_vertical=0,33,11
separator=0xd8d8d8,20,100
pinyin_marge=47,10,25,120
zhongwen_marge=10,5,15,90
```

规范化后对应：

```json
{
  "background": {
    "asset": "assets/skin1.png",
    "layout": {
      "horizontal": "stretch",
      "vertical": "stretch"
    },
    "stretch": [38, 33, 192, 11]
  },
  "content": {
    "preedit": [25, 47, 120, 10],
    "candidates": [15, 10, 90, 5]
  },
  "separator": {
    "color": "#D8D8D8",
    "left": 20,
    "right": 100,
    "thickness": 1
  }
}
```

## customN 同窗叠加层

`customN` 用于保存不应跟随底图拉伸的人物、徽标等装饰。导入器以 `custom_cnt` 为边界，
按索引顺序处理当前格式的 `custom0..custom(count-1)`；为兼容旧生成器，也检查存在的
`custom1..custom(count)`，相同索引只处理一次。

| SSF | ZLT overlay |
| --- | --- |
| `customN_display=1` | 注册并启用该叠加层 |
| `customN` | `asset` |
| `N` | `custom_index` 和默认 `draw_order` |
| `customN_align` 的十整数 | `align`，顺序与正负号原样保留 |

只有 `customN_display` 明确等于 `1` 时才读取图片和定位值。`display=0` 的编辑残留不会注册
资源，也不会因为其路径或 `align` 内容无效而阻止导入。启用的叠加层若缺少图片、缺少
`customN_align`、十元组数量错误，或包含非 32 位有符号整数，都会返回带字段路径和
`skin.ini` 行号的导入错误。`custom_cnt` 必须位于 0–128。

ZLT v1 先无损保存十元组，不在通用清单层猜测每个位置的语义。渲染器只有在按 SSF
定位规则解释这十个值后，才可以把含叠加层的主题标记为完全兼容。

## 渲染约束

为了让同一套素材在字流与搜狗中的候选窗外观一致，渲染器必须：

1. 在 `base_dpi` 坐标系中计算图片九宫格和各区边距，再统一换算到目标 DPI。
2. 水平和垂直方向分别执行 `stretch` 或 `tile`，不能合并成一个模式。
3. 拼音区与候选区使用各自边距，不再叠加背景图片的通用内容边距。
4. 表面分隔线颜色优先于全局调色板，并严格保留左右留白。
5. 缺少 SSF 指定字体时明确回退到用户当前字体；这种情况不能宣称像素级一致。

## 当前兼容边界

首版映射同窗主背景、`custom*` 叠加层、颜色、字体、内容边距、分隔线与翻页按钮。
`pinyin_pic`、`zhongwen_pic` 以及分窗/状态栏资源暂不进入 ZLT v1。检测到尚未支持的
字段时，设置界面会明确显示“部分兼容”，不会静默宣称完全一致。
