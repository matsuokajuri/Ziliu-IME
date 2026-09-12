# 字流开放主题格式 ZLT v1

ZLT（Ziliu Theme）是字流的开放候选窗口主题格式。格式目标是容易制作、容易检查、
可离线导入，并能作为搜狗 SSF、百度 BPS 等外部格式的统一转换目标。

## 包结构

`.zlt` 是一个标准 ZIP 文件，根目录必须包含 UTF-8 编码的 `manifest.json`：

```text
example.zlt
├─ manifest.json
├─ preview.png                 可选
└─ assets/
   ├─ horizontal.png
   ├─ vertical.png
   ├─ previous.png
   └─ previous-hover.png
```

ZLT v1 的 ZIP 约束：

- 文件名使用 UTF-8 和 `/` 分隔符，名称区分大小写。
- 可以使用 Store 或 Deflate 压缩，不允许加密、ZIP64、符号链接和硬链接。
- `manifest.json` 必须位于包根目录，不能重命名。
- 最多 128 个条目，总解压大小不超过 32 MiB，单个资源不超过 8 MiB。
- 禁止绝对路径、反斜杠、盘符、空路径段、`.` 和 `..`。
- 图片只允许 PNG 或 APNG；单边不超过 4096 像素，总解码像素不超过 64 Mi。
- 不允许内嵌字体、脚本、DLL、EXE 或其他可执行内容。字体字段只引用系统已安装字体。

导入器负责在设置进程中验证和规范化主题包。`ZiliuTIP.dll` 不直接解包或解析未经验证的
第三方文件。

## 清单

完整结构由 [`ziliu-theme-v1.schema.json`](schemas/ziliu-theme-v1.schema.json)
描述。最小主题只需要元数据、浅色调色板和字体：

```json
{
  "format_version": 1,
  "id": "org.example.clean",
  "name": "清简",
  "author": "Example Author",
  "version": "1.0.0",
  "license": "CC-BY-4.0",
  "source_format": "zlt",
  "base_dpi": 96,
  "appearances": {
    "light": {
      "palette": {
        "preedit_text": "#202124",
        "candidate_text": "#202124",
        "highlighted_candidate_text": "#0067C0",
        "background": "#FAFAFA",
        "highlighted_background": "#E1EFFF",
        "muted_text": "#6B7280",
        "separator": "#D1D5DB"
      },
      "typography": {
        "chinese_font_family": "Source Han Sans SC",
        "english_font_family": "Segoe UI Variable Text",
        "font_size": 17
      }
    }
  }
}
```

### 版本和标识

- `format_version`：清单主版本。v1 读取器只接受数值 `1`。
- `id`：稳定主题标识，只能包含小写 ASCII 字母、数字、`.`、`_`、`-`；
  `org.ziliu.default` 是内置主题保留标识，外部主题不得使用。
- `name`、`author`、`version`、`license`：必填。`license` 推荐使用 SPDX 标识；
  从外部格式导入且无法确认时使用 `LicenseRef-Unknown`。
- `source_format`：来源格式，例如 `zlt`、`sogou-ssf`、`baidu-bps`。
- `base_dpi`：素材设计基准 DPI，范围 72–384，默认 96。
- `preview`：可选预览图路径。

读取器忽略未知属性，以便在不改变主版本的情况下增加可选元数据。已知属性类型错误、
重复 JSON 属性或不安全资源路径仍然会被拒绝。

### 颜色

颜色使用：

- `#RRGGBB`：完全不透明。
- `#RRGGBBAA`：最后两位为 Alpha。

清单中的七个颜色分别控制拼音文本、普通候选文本、焦点候选文本、窗口背景、焦点背景、
弱化文本和分隔线。

### 深浅外观

`appearances.light` 必填，`appearances.dark` 可选。未提供深色外观时，系统深色模式仍使用
浅色外观；设置界面应向用户明确提示该主题没有原生深色版本。

每个外观独立声明调色板、字体和横排/竖排表面，因此导入器可以保留厂商主题在不同布局
下的差异。

### 图片和九宫格

表面背景示例：

```json
{
  "background": {
    "asset": "assets/horizontal.png",
    "layout": {
      "horizontal": "stretch",
      "vertical": "tile"
    },
    "stretch": [12, 10, 12, 10]
  },
  "content": {
    "preedit": [18, 12, 18, 8],
    "candidates": [18, 8, 18, 12]
  },
  "separator": {
    "color": "#D1D5DB",
    "left": 18,
    "right": 18,
    "thickness": 1
  }
}
```

- `stretch`、`content.preedit` 和 `content.candidates` 的顺序均为
  `[左, 上, 右, 下]`，单位是 `base_dpi` 下的像素。
- `stretch` 表示不可随意变形的四周边缘；中心区域承担拉伸或平铺。
- `layout.horizontal` 和 `layout.vertical` 分别控制水平、垂直方向：
  `stretch` 拉伸中心区域，`tile` 平铺中心区域，`fixed` 保持该方向的源图尺寸且不拉伸、
  不平铺。二者缺省时均为 `stretch`。
- `content.preedit` 与 `content.candidates` 分别控制拼音区和候选区边距，不能合并为一套
  边距；这保证从 SSF 同窗布局导入时不丢失 `pinyin_marge` 和 `zhongwen_marge`。
- `separator` 可声明表面专属颜色或 PNG/APNG、左右留白和线宽。未声明 `color` 时回退到
  调色板的 `separator`；声明图片时图片优先于纯色。
- 未提供背景时，候选窗继续使用字流原生圆角背景。

### 同窗叠加层

不应随底图九宫格变形的装饰可以作为 `overlays` 保存：

```json
{
  "overlays": [
    {
      "asset": "assets/character.png",
      "custom_index": 0,
      "draw_order": 0,
      "align": [0, -1, 2, -3, 4, -5, 6, -7, 8, -9]
    }
  ]
}
```

- 一个表面最多保存 128 个叠加层。
- `custom_index` 保留外部主题的 `customN` 原始索引；`draw_order` 从小到大决定绘制顺序，
  相同值按清单数组顺序保持稳定。
- `align` 必须恰好包含 10 个 32 位有符号整数。ZLT v1 对这十个位置不臆造字段含义，
  以便无损保留搜狗 `customN_align`，由对应来源格式的定位适配器解释。
- 叠加层在九宫格背景之后、候选文字和控件之前绘制。缺少或不安全的 `asset` 会使清单
  校验失败。

### 按钮状态

可定义 `previous`、`next`、`expand`、`collapse` 和 `menu` 五类按钮：

```json
{
  "buttons": {
    "menu": {
      "normal": "assets/menu.png",
      "hover": "assets/menu-hover.png",
      "pressed": "assets/menu-pressed.png"
    }
  }
}
```

`normal` 必填；缺少 `hover` 或 `pressed` 时回退到 `normal`。未定义的按钮继续使用字流
原生矢量图标。

## 兼容与安全

- APNG 属于合法资源，但首版渲染器可以只显示第一帧；完整动画不要求提升清单版本。
- 多行候选是字流扩展能力。主题背景按九宫格规则增高，不能安全拉伸时回退到原生背景。
- Windows 系统输入法指示器不属于候选窗口，ZLT 不定义其外观。
- 搜狗 SSF 首版只导入 `Scheme_H1` 和 `Scheme_V1` 同窗布局；`Scheme_H2`、`Scheme_V2`
  分窗布局以及 `StatusBar` 不进入 ZLT。字段级对应关系见
  [`sogou-ssf-mapping.md`](sogou-ssf-mapping.md)。
- 外部主题的作者、许可证和来源必须保留；导入主题不意味着获得重新分发其素材的权利。

核心 C++ 数据模型、解析器和校验器位于
`src/core/include/ziliu/core/theme_manifest.h`，它不依赖 Windows、WinUI 或第三方 JSON 库。
