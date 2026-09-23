# 0.1.0-alpha.1 本地候选包

## 状态与范围

本文前半部分记录了 `Ziliu 0.1.0-alpha.1` 的 Windows 11 x64 本地验收历史；其中
“不公开发布”的结论属于当时的授权状态。现在用户批准公开**未签名的早期测试预发布版**，
README、安装器提示与发行包会明确标示 SmartScreen 风险。代码签名延至正式版前处理。
SHA-256 只验证完整性，不等于代码签名；guest 验收结果只对记录的精确候选包有效。

WinUI 设置程序采用 **unpackaged self-contained** Windows App SDK 部署。候选包不依赖目标
机器预装 Windows App Runtime，但打包会按固定 allowlist 要求本地 Release 输出包含完整
self-contained 文件；本地 NuGet 缓存不完整时构建应失败，不允许打包脚本联网补齐。

## 构建与打包

准备好的依赖 checkout 必须包含固定 librime headers、Rime Ice 数据和已校验的
`.cache/librime-runtime/dist/lib/rime.dll`。构建和打包示例：

```powershell
$env:ZILIU_DEPENDENCY_ROOT = 'D:\path\to\prepared-ziliu-dependencies'
scripts\build-local.cmd Release
scripts\package-alpha.cmd
```

打包器只收录：

- `ZiliuTIP.dll`、`ZiliuBroker.exe`、`ZiliuSettings.exe`、`ZiliuRegister.exe`；
- 与准备依赖根 SHA-256 一致的 `rime.dll`；
- Rime 运行数据扩展名 allowlist，排除 `.git`、`.github`、`build` 和开发文件；
- 固定的 WinUI self-contained x64 文件清单；
- Ziliu、librime、Rime Ice 许可证和第三方声明；
- 安装/卸载脚本、版本元数据和逐文件 SHA-256 manifest。

输出名包含 `unsigned-test-only`。ZIP 条目排序且时间固定；相同输入应得到相同归档字节。
`release.json` 会记录 commit 和 tracked working tree 是否干净，未签名本地候选不因此升级为
可公开发行物。

## 只读包验证

解压后可在普通 64 位 PowerShell 中执行：

```powershell
powershell.exe -NoProfile -NonInteractive -File .\Install-Ziliu.ps1 -VerifyOnly
```

此模式只验证平台、manifest 安全相对路径、无 reparse 的 payload、精确文件覆盖、逐文件
SHA-256 和 `release.json`；它在任何 `%ProgramFiles%` 创建、进程启动或注册表写入之前返回。

## 安装、升级与卸载设计

安装必须由管理员在 64 位 PowerShell 中显式运行。脚本把 payload 复制到
`%ProgramFiles%\Ziliu\0.1.0-alpha.1` 的唯一 staging 目录，在 staging 再次按 manifest
验证后原子改名，最后调用该版本的 `ZiliuRegister.exe install`。同版本目录已存在、路径穿越、
reparse、额外文件、hash 不一致或注册指向未能确认时一律失败。

安装还为执行安装的当前用户写入精确的 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`
值 `ZiliuBroker`，以便登录后先启动同一版本的 Broker。SearchHost 等受限制的输入宿主不能
可靠地在首次按键时拉起独立 Broker。升级时，只有旧启动值是普通字符串、且精确指向当前已注册
TIP 同目录的 `ZiliuBroker.exe`，才将其认定为本产品的旧值并替换；未知同名值仍拒绝覆盖。
注册切换前再次核对旧 TIP 和启动值，失败回滚时恢复可信旧值。卸载只移除仍指向本版本
Broker 的同名值，不改动其他用户的启动项。此 alpha 的自动启动仅覆盖执行安装的用户，
不能据此宣称多用户会话已验收。

升级使用版本化并存，不覆盖正在宿主进程中加载的 DLL，也不自动删除旧版本。新注册失败时，
脚本先尝试恢复并确认先前 `InprocServer32`；恢复失败会保留新目录并显式报告 `FAILED`，不会
掩盖或继续删除现场。PowerShell 脚本不是 MSI 事务：断电、类别注销失败和部分系统状态仍需
guest 验证。

注册切换后，已加载的 TIP 或旧 Broker 仍可能继续运行旧代码。必须注销或重启 Windows，
再重新打开应用，才能把升级视为收敛；安装脚本不会终止无关宿主进程。

卸载只在 `InprocServer32` 确实指向请求版本时调用该版本注册器。注册器失败或 COM key 仍在
时不删除文件。注销确认后先写 cleanup receipt，再逐个删除 manifest 管理文件；若 DLL/Broker
仍被占用则保留 receipt、manifest 和剩余文件，重启后可继续清理。脚本从不递归删除未知文件，
也不会修改 `%LocalAppData%\Ziliu`，因此设置、主题和 Rime userdb 默认保留。

## 隐私输入边界

输入策略分为阻断、受限转换和普通转换三层：

- `TF_TMAE_SECUREMODE`、系统禁用/空上下文、无效输入上下文，以及明确的 password/PIN
  input scope：把新按键交还应用，不发送到输入引擎。
- `IS_PRIVATE` 或无法确认的可选 input scope（包括读取失败）：允许中文转换，但使用独立的
  `ziliu_private` 会话方案，禁用用户词典学习；不读取文档前文，不增加输入日志或网络调用。
  该方案使用静态拼音词库，不加载普通方案的 Lua 扩展、用户短语和扩展翻译器。
- 明确的普通 input scope：保留原有普通会话和词频学习。没有类型信息不等于已经证明安全。

隐私分类或输入上下文变化时丢弃旧组句，不把旧输入带入新的学习会话。受限方案缺失或无法
选中时不得退回普通学习方案。受限输入不承诺完全没有文件写入：Rime 初始化、词库部署和
缓存维护与输入内容学习不同。也不承诺识别错误地声明为普通字段的第三方密码控件。
新 TIP/Broker 使用匹配的会话协议；升级后仍需验证旧进程退出后的二进制版本收敛。

本地 CTest 包括真实 Rime、私有桌面 UI 和 privacy 单元合同；以当前构建的测试日志为准，
不能代替新 bundle 的安装或真实应用验收。

## 2026-09-21 完整验收执行记录：未通过

被测物是 `build/release/cold-start-fix-20260921-r2/` 中的未签名 ZIP，SHA-256
`4b775c8b0b8c968f88c227595eba6ba2e5ada64616ab36853440f86a7a1500c5`。
证据位于 `build/test-artifacts/alpha-full-20260921/`，最终恢复状态以其中
`evidence.json` 为准。本次没有修改产品代码或重新打包，没有主机部署或公开发布。

| 验收单元 | 结果 | 当前证据和边界 |
|---|---|---|
| 包完整性、VerifyOnly、许可文件覆盖 | PASS | r2 哈希与 manifest 一致；VerifyOnly exit 0 |
| 无现存 Ziliu COM/版本目录/用户数据的安装 | PASS（限定） | 原用户数据先完整隔离，安装 exit 0，重启后实际按键提交精确 `你好`；这不是重装 Windows 的 pristine VM，也没有证明此前 CTF 服务根完全为空 |
| Settings self-contained 启动 | PASS | 实际进程加载包内 Microsoft.WindowsAppRuntime / Microsoft.UI.Xaml 模块 |
| loaded 旧 TIP/Broker 升级 | PASS | 旧 TIP `b64feeb2…` 与 Broker 确实已加载；安装后旧进程仍在旧路径；重启后 TIP `6ab211f3…`、Broker `a48879d7…` 均来自 alpha 目录并提交 `你好` |
| Notepad 普通输入 | PASS | 逐个虚拟键 `n i h a o Space`，机器读取 `U+4F60 U+597D` |
| Edge 合成普通/密码/PIN-like/返回普通字段 | PASS（限定） | `edge-fields.json`：普通及返回普通字段为 `你好`；密码为 `nihao`、PIN-like 为 `1234`，后两者没有 composition 事件；HTML PIN-like 不等于真实 `IS_NUMERIC_PIN` |
| 真正 IS_PRIVATE / IS_NUMERIC_PIN 及不学习 | BLOCKED | 原生 EDIT fixture 普通对照不产生中文；只读诊断 GetFocus 返回 S_FALSE，没有取得文档上下文及 scope readback，因此不把 SetInputScope S_OK 当成验收通过，也未宣称真实 private 不学习通过 |
| 开始菜单 / 任务栏搜索 | FAIL | 两处逐键输入直接得到 `nihao`，无候选框；SearchHost 加载了 alpha TIP；重新选择字流和等待激活后仍复现，根因尚未确认 |
| custom SSF 导入、应用、样式控件禁用 | PASS（限定） | skin-c 导入并绑定 SHA `4105ee8f…`；相关控件禁用；不等于完整视觉回归通过 |
| custom SSF 缺字体回退 | FAIL | guest 缺少皮肤声明的荆南麦圆体，H1 实际候选和 V1 预览显示方框字；提交文字仍正确；旧 bundle 也复现，未归因于本包新回归 |
| Settings 最大化布局 | FAIL | 最大化时内容右移、导入按钮被窗口裁切；普通窗口尺寸正常，证据 `05-theme-maximized-clipped.png` |
| 卸载文件 / COM / profile / categories / 数据 | PARTIAL | 加载中先产生 cleanup receipt；重启重试 exit 0、版本目录和 COM 消失、profile 不再枚举、categories 为空、30 个用户文件哈希不变；但 `EnumInputProcessorInfo` 重启后仍列出服务 CLSID，HKLM CTF/TIP 服务根仍在，因此完整清理 FAIL |
| 当前本地 CTest | PASS | 17/17，13.81 秒；真实应用 gate 不能由这些单元测试替代 |
| 公开发行签名 | BLOCKED | 仍为 unsigned test-only |

原生和 HTML 合成字段夹具仅供人工启动的 guest 验收，不打包、不自动注册、不运行认证界面。
原生目标 `ziliu_alpha_input_scope_host` 是 `EXCLUDE_FROM_ALL`，不会增加 CTest 数量。
不得把此次记录描述为“完整 alpha 验收通过”或“可以公开发布”。先修复已复现缺陷、补齐有效
private/PIN 宿主及不学习证据，再对新的冻结候选包重跑相关 gate。

## 2026-09-23 r6 未签名候选包验收：本地限定通过

被测物是 `build/release/alpha-recheck-20260923-r6/Ziliu-0.1.0-alpha.1-win11-x64-unsigned-test-only.zip`，
SHA-256 `FE05792AC4FBF67AE62DEFBD331C057350CFD361F7944BBBA2873575A1693DB8`。
同输入重打包得到相同 ZIP SHA-256；本地 Release 构建和 CTest 17/17 通过。
测试只在隔离 guest `ziliu-compat-20260913`（`USERCUA-NQG4SL2`）进行，没有部署到主机，
也没有创建 tag、GitHub Release 或公开下载。

| 验收单元 | r6 结果 | 证据和边界 |
|---|---|---|
| 包完整性、VerifyOnly | PASS | 冻结 ZIP 与逐文件 manifest 校验通过；VerifyOnly exit 0 |
| 安装和登录后 Broker 启动 | PASS | 安装 exit 0；重启后 Broker 位于版本化安装目录，父进程为 Explorer，HKCU Run 指向同一版本 |
| Notepad 与 Windows 搜索 | PASS | Notepad、任务栏 SearchHost、开始菜单均用逐个真实虚拟键输入 `n i h a o`，显示 `ni'hao` 候选，Space 后机器读取 `U+4F60 U+597D`；两处搜索均有 7 个候选 |
| 原生 InputScope | PASS（限定） | TSF 原生 fixture 读回 `IS_PRIVATE=61`，输入提交 `你好`；password 得到原始 `nihao` 且无候选，numeric PIN 得到原始 `1234` 且无候选。受保护字段的编辑会话读回受系统限制，不把 `SetInputScope S_OK` 单独当作行为证据 |
| private 不学习 | PASS（限定） | 连续 4 次 private `nihao Space` 均提交 `你好`；`rime_ice.userdb` 9 个文件在操作及失焦后大小和时间戳不变。这只证明本次固定输入没有观察到用户词典写入，不是所有输入内容的通用安全证明 |
| Edge 普通/密码/PIN-like/返回普通字段 | PASS（限定） | 离线页面导出的事件与 Unicode 证据：普通及返回普通为 `你好` 并有 composition 事件；密码为原始 `nihao`，合成 PIN-like 为原始 `1234`，后两者只有 input 事件。HTML PIN-like 不代替上方原生 `IS_NUMERIC_PIN` 检验 |
| Settings 与 custom SSF | PASS（限定） | 设置进程加载包内 Windows App SDK 模块；最大化时外观页导入按钮可见；启用自定义皮肤时样式控件禁用；guest 缺失皮肤声明字体时，V1/H1 预览及实际 H1 候选仍显示可读汉字。此项不是逐像素皮肤认证 |
| loaded 旧 TIP/Broker 升级 | PASS | 升级前 Notepad 加载旧 TIP `B64FEEB2…`；新安装后旧进程未被强杀，注册切换到新目录；重启后 Notepad 加载新 TIP `5FA79088…`、新 Broker 来自安装目录，稳定聚焦输入提交 `你好` |
| 卸载完整性和用户数据 | PASS | 加载中的 DLL 触发可续清理 receipt；重启重试 exit 0。两轮卸载后版本目录、COM、CTF 服务根、HKCU Run 均消失；TSF 枚举中服务/profile 均不存在且 categories 为空。测试前 65 个用户数据文件逐一按 SHA-256 恢复并在 guest 重启后再次核对一致 |
| 公开发行签名与第三方复核 | BLOCKED | 未签名、仅本地测试；未完成正式发行前的签名和第三方声明复核 |

guest 证据保留在 `C:\ZiliuCompat\alpha-recheck-20260923\r6\`（含
`field-evidence.json`、两次卸载的注册枚举 JSON）和单独保存的测试数据目录。
验收结束后恢复了原有 `settings-fix-1` TIP 注册；重启后 `InprocServer32` 指向原 DLL，
原 DLL SHA-256 为 `B64FEEB277E65D30E62C3DCCC9AF43EE0E6242C3A1EC9BB9D6CCCBCDCDD0C848`。
`restored-registration.json` 再次确认服务 CLSID、精确中文语言 profile 和原有 categories 均可枚举。

有一次极快的自动切换/输入出现不完整 `ni`；另一次候选框出现在任务栏时，机器检查发现
实际焦点仍是任务栏而不是表面在前景的 Notepad。明确将焦点置于 Notepad 文档后候选框在光标旁
正常显示。这两次无效焦点前提不能证明定位回归；真实快速切换行为仍需另设可重复的聚焦门禁，
不可据此修改渲染器，也不可将其计入本轮通过项。当前结论仅为**冻结 r6 的本地未签名候选包验收
在上述范围内通过**，不是公开发布批准，也不是自定义 SSF 逐像素认证。

## 2026-09-23 r7 代码审计与完整本地功能验收

本轮审计了发布脚本、TSF 隐私/启动边界、IPC/Broker 与主题加载的关键路径。唯一在发布路径
复现的阻断是旧版字流已拥有 `ZiliuBroker` 启动值时，安装器将自身的可信旧值当作冲突拒绝；
已在 `625139822b2838b2018a8b8e04a67b13967708ae` 中做定向修复。此审计不是对每个源码分支
的形式化证明；下方结果仍以冻结包的实际构建和 guest 行为为准。

被测 ZIP：`build/release/alpha-audit-20260923-r7/Ziliu-0.1.0-alpha.1-win11-x64-unsigned-test-only.zip`，
SHA-256 `EC4F22358403794F00A0760487B093E7B232F2C543B0C419D6EFE03E5C10625B`。
`release.json` 记录上述 commit、`sourceTrackedClean=true`；`VerifyOnly` 通过。相同输入另行
打包一次，ZIP SHA-256 完全相同。Release x64 构建和 CTest `17/17` 通过。本轮未在主机安装
候选包，未创建 tag、GitHub Release 或公开下载。

测试 VM 为 `ziliu-compat-20260913`（ID `C35593B3-6EFD-4652-8C35-50C1D2DE6355`，guest
`USERCUA-NQG4SL2`）。主机侧截图保存在 `build/test-artifacts/alpha-r7-20260923/`；原生
InputScope 与 TSF 注册枚举的 JSON 保存在 guest 的 `C:\ZiliuCompat\alpha-r7-20260923\`。

| 验收单元 | r7 结果 | 证据和边界 |
|---|---|---|
| 旧版升级与登录收敛 | PASS | 安装前旧 TIP 为 `settings-fix-1`、旧 Broker 启动值与它同目录；升级 exit 0。重启后 COM、HKCU Run、Broker 进程和 Notepad 加载的 TIP 均指向本包版本目录，TIP/ Broker 文件 hash 与包内一致 |
| 陌生同名启动值防护 | PASS | guest 内以不匹配旧 TIP 的临时 `ZiliuBroker` Run 值调用同一安装器，明确拒绝、exit 1；旧 TIP 未变、候选包版本目录未创建、临时 Run 值未被覆盖。随后只移除本轮临时值，恢复测试前 Run 不存在的状态 |
| 无现存注册时安装 | PASS | 首轮完整卸载后确认版本目录、COM、CTF TIP、Run 消失，TSF service/profile 不再枚举且类别为空；随后本包重新安装 exit 0，重启后 Broker 自动启动、Notepad 加载本包 TIP，逐键 `n i h a o Space` 提交精确 `你好` |
| 普通输入与 Windows 搜索 | PASS | Notepad、任务栏搜索、开始菜单搜索均用单个虚拟键输入 `n i h a o` 并用 Space 提交；机器读取均为 `U+4F60 U+597D`，两处搜索显示候选框 |
| 输入法指示器菜单 | PASS | 普通输入、任务栏搜索及开始菜单打开时，右键指示器均出现字流快捷菜单；稳定截图分别为 `64-notepad-right-menu.png`、`66-taskbar-right-menu.png`、`67-start-right-menu.png` |
| 原生隐私字段及不学习 | PASS（限定） | 原生 TSF fixture 导出的 `native-field-evidence.json`：`IS_PRIVATE=61`，普通和私有字段提交 `你好`；password 为原始 `nihao`、numeric PIN 为原始 `1234`。私有字段连续四次输入及失焦后，10 个 `rime_ice.userdb` 文件的路径、大小、UTC 时间戳和 SHA-256 均未变化；仅证明本轮固定输入未观察到学习写入 |
| Edge 字段 | PASS（限定） | 离线合成页面导出 Unicode 与事件：普通及返回普通字段为 `你好` 且有 composition 事件；密码为原始 `nihao`、合成 PIN-like 为原始 `1234` 且仅有 input 事件。HTML PIN-like 不代替上方原生 PIN 验收 |
| Settings 与自定义 SSF | PASS（功能限定） | Settings 进程实际加载版本目录内的 `Microsoft.WindowsAppRuntime.dll` 和 `Microsoft.UI.Xaml.dll`；最大化外观页可用。新导入的 `Win7风格` SSF SHA-256 为 `45D2C6391FF51F806249374E96550C5B37BA2484BB760C1729A663AC409F9764`，manifest 名称/作者/版本为 `Win7风格`、`actualist88`、`4.2`，启用时样式控件禁用；Notepad 与两处 Windows 搜索均显示自定义皮肤候选框并提交 `你好` |
| H1/V1 与缺字体验证 | PASS（功能限定） | 已安装的 custom SSF `sogou.4105ee8f…` 声明“荆南麦圆体”，guest 字体注册表无此字体；横排、竖排预览和实际竖排候选文字可读，无方框字。实际竖排截图 `86-skin-c-v1-actual.png`。这不是与搜狗逐像素一致的认证 |
| 卸载、用户数据与 VM 终态 | PASS | 两轮均验证“文件被加载→保留 cleanup receipt→正常重启→再次卸载 exit 0”；版本目录、COM、CTF TIP、Run 清除，TSF service/profile 不再枚举且类别为空。原有 `settings-fix-1` TIP 路径与 SHA-256 `B64FEEB2…` 恢复，原 66 个用户文件逐文件 SHA-256 与测试前备份相同；本轮产生的 68 文件状态单独保存在 guest 测试目录。仅测试 VM 最终为 Off |
| 公开发行条件 | BLOCKED | 包仍未签名，第三方声明仍待正式发行前复核；本轮只批准本地未签名候选包测试，不代表公开发布批准 |

结论：冻结 r7 包在上述 **0.1.0-alpha.1 本地功能验收范围**内通过。不同应用/设置组合、
全部 SSF 及逐像素 parity 均不由这一轮证明；不能把测试数量或这些功能通过项计为自定义
SSF 渲染最终目标的完成度。

## 2026-09-23 图形安装器（初始本地测试记录）

`scripts/package-alpha-installer.ps1` 使用 Inno Setup 6.7.3 将上面的**精确 r7 ZIP**封装成可双击运行的
`Ziliu-0.1.0-alpha.1-win11-x64-unsigned-test-only-setup.exe`。打包前会核对固定的 ZIP SHA-256
`EC4F22358403794F00A0760487B093E7B232F2C543B0C419D6EFE03E5C10625B`；安装时再次核对
内嵌 ZIP，并调用原有 `Install-Ziliu.ps1 -VerifyOnly` 与安装路径。图形安装器只负责提升权限、
承载 ZIP 和在“应用与功能”登记卸载入口，不另写 TIP、Broker、用户数据的安装逻辑。

```powershell
powershell.exe -NoProfile -NonInteractive -File .\scripts\package-alpha-installer.ps1
```

默认输出在 r7 ZIP 同目录，同时生成 EXE 的 `.sha256` 文件。若以后换用新 ZIP，必须显式传入
`-PackageZip` 和与之匹配的 `-ExpectedSha256`，并重新做 guest 安装／卸载验收；不能沿用本节结果。
同版本目录已存在时按原脚本设计拒绝覆盖。安装与卸载均只支持执行程序的管理员账户；
标准用户通过**另一个**管理员账户的 UAC 凭据安装，可能把当前用户 Broker 启动项写到该管理员
账户，故此 alpha 不支持这种跨账户安装。安装／升级后必须注销或重启才能验证已加载 TIP 收敛。

本次安装器 EXE SHA-256 为 `A7943D6115C96F62A1BE1B2C4BD7652A00EEBE7ADEFB051BA8218AA3E2C3AE84`。
隔离 VM `ziliu-compat-20260913` 中，传入 EXE 的 SHA-256 与主机一致；从旧版 `settings-fix-1`
安装返回 0，436 个 manifest 文件、TIP 注册、HKCU Broker Run 值及 Windows 卸载入口均存在；
同版本重装拒绝（退出码 7）在安装逻辑相同、仅提示文本不同的前一编译体上验证；
最终 EXE 的安装与图形卸载器均返回 0，版本目录、COM、Run 值、卸载入口和卸载器
均消失。用户数据 66 文件的汇总 SHA-256 在安装前、卸载后均为
`D63B4F55776BBB86AB855C1C1D38E0835F5965F4771E160A770FFD14CAD96DF5`。
guest 测试日志位于 `C:\ZiliuCompat\alpha-installer-20260923\`。本节不代表首装空白系统、
跨账户安装、卸载时 DLL 被占用的恢复路径或真实 GUI 点击流程已验收；也不改变未签名、
不得公开发行的状态。图形封装仍不是 MSI 事务；如果原安装脚本已成功而 Inno 后续登记卸载
入口失败，需在隔离环境中人工审查状态，不能声称自动回滚成功。

## 公开预发布版边界

用户后来明确批准将未签名包作为 GitHub 预发布版提供，并将签名推迟到正式版前研究。
发布包必须重新包含固定上游雾凇拼音 Credits、实际使用的 Windows App SDK 许可与
NOTICE，并重新生成 ZIP、安装器和 SHA-256；上述 r7 本地包的散列及 VM 验收不能冒充
新包的散列或验收。早期测试版的未签名、Windows 11 x64 限制、跨账户安装限制、
SmartScreen 警告和剩余未验证行为均须在 README 与 Release 说明中披露。
