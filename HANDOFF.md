# Ziliu 交接：当前工作目录（2026-09-23）

本节优先于下方历史记录。下方出现的旧 worktree 路径、当时的 HEAD、失败状态与下一步，
仅用于追溯，不再代表当前工作环境或最新验收结论。阅读本文不构成继续工作的授权。

- 唯一项目目录及 Git worktree：`D:\Projects\Ziliu-IME`，分支 `main`。本次目录整合前的
  `main` 为 `cfca8527327d2247d8d4296fa90a24965c61924c`；后续请用 `git rev-parse HEAD`
  核对当前提交。`Ziliu-IME-host-recovery-20260913` 与 `Ziliu-IME-clean-base-001` 已移除，
  不要按下方历史路径运行命令。旧恢复备份已迁入 `.local-recovery/`（仅本地、Git 排除）。
- 两份旧工作树的未提交源码未丢弃：主旧树快照
  `refs/archive/worktree/pre-recovery-20260923`（原 216 项），clean-base 快照
  `refs/archive/worktree/clean-base-20260923`（原 12 项）。对应历史提交仍在
  `archive/pre-recovery-20260913` 与 `archive/salvage-20260814` 分支。不要把这些旧实验
  自动合并到 `main`。
- 最新本地未签名 Alpha 包位于
  `build/release/alpha-audit-20260923-r7/Ziliu-0.1.0-alpha.1-win11-x64-unsigned-test-only.zip`，
  SHA-256 为 `EC4F22358403794F00A0760487B093E7B232F2C543B0C419D6EFE03E5C10625B`。
  最新 guest 截图在 `build/test-artifacts/alpha-r7-20260923/`；旧截图在
  `build/legacy-test-artifacts/`，旧研究证据在 `build/research/`，回滚备份及恢复资料在
  `.local-recovery/`。这些是本地证据，不是产品源文件或公开发行物。
- r7 的本地功能验收范围与限制以 `docs/ALPHA-RELEASE.md` 最后一节为准：功能检查通过，
  但未签名、未公开发布，也没有完成所有自定义 SSF 的逐像素认证。目录整合没有更改产品
  源码或重新运行构建/VM 验收；Git 连通性和工作树清洁度已核对。

# 历史交接：自定义 SSF 三皮肤几何对比

更新：2026-09-14（Asia/Tokyo）。当前状态核对时间：2026-09-13 21:03:42 UTC。
本文用于接手工作，不是继续执行授权。阅读本文不代表用户授权启动任务。

## 后续恢复记录（优先于下方历史交接状态）

### 2026-09-21：完整 alpha 验收尝试 FAIL/BLOCKED，未发布

- 用户要求完成完整 alpha 验收，额度门禁暂时禁用。本轮没有修改产品代码或二进制，没有主机部署、commit、push 或公开发布。工作目录仍为 `D:\Projects\Ziliu-IME-host-recovery-20260913`，main HEAD `43b70191be7be680b463559d5abf1b65f3ddc4ed`；保留所有此前未提交修改。
- 被测 r2 ZIP：`build/release/cold-start-fix-20260921-r2/Ziliu-0.1.0-alpha.1-win11-x64-unsigned-test-only.zip`，SHA256 `4b775c8b0b8c968f88c227595eba6ba2e5ada64616ab36853440f86a7a1500c5`。最终证据 `build/test-artifacts/alpha-full-20260921/evidence.json` 明确 `passed=false`。详细范围见 `docs/ALPHA-RELEASE.md` 最后章节。
- PASS：包校验/许可/VerifyOnly；隔离原 Ziliu COM、候选版本目录及用户数据后的安装与重启输入；Settings 包内 runtime 加载；旧 TIP/Broker 确实加载时升级、重启后版本收敛并提交精确 `你好`。这不是全新 Windows，也没有证明安装前 CTF 服务根为空。
- Edge 合成字段通过：普通及从密码返回普通字段实际逐键提交 `你好`；密码输入 `nihao`、PIN-like 输入 `1234` 且无 composition 事件。HTML PIN-like 不等于真实 `IS_NUMERIC_PIN`，不能替代 private/PIN/不学习验收。
- FAIL 1：开始菜单及任务栏搜索直接得到英文 `nihao`、无候选框；已确认 SearchHost 加载当前 alpha TIP，重新选择字流后仍复现。根因 UNKNOWN，不能直接归咎于隐私策略，更不能为通过测试移除安全保护。
- FAIL 2：guest 缺少 skin-c 声明的荆南麦圆体时，H1 实际候选和 V1 预览显示方框字，提交文字正确。旧 bundle 也复现；需要通用缺字/缺字体 fallback 修复，不是安装特定字体掩盖问题。
- FAIL 3：1280x720 guest 中 Settings 最大化导致内容右移、导入按钮被裁切；普通窗口正常。
- FAIL 4：卸载重启重试 exit 0，版本目录/COM/profile/categories 清理完成、用户数据哈希保留，但 TSF `EnumInputProcessorInfo` 仍枚举服务 CLSID，HKLM CTF/TIP 服务根仍在。`tools/register/register_main.cpp` 当前只 UnregisterProfile，未完整 Unregister 服务；不能声称该残留由 r2 新引入。
- BLOCKED：原生 EDIT fixture 的普通正对照不产生中文；只读 TSF 诊断 GetFocus=S_FALSE、无 document manager/context/scope readback。SetInputScope=S_OK 不是有效 scope 证据，真实 IS_PRIVATE/IS_NUMERIC_PIN 及不学习尚未验收。不要把空候选框算成功，不要新增测试平台绕过这个缺口。
- 本轮测试文件：新增 `tests/alpha_input_fields.html`、`tests/alpha_input_scope_host.cpp`，以及 `tests/CMakeLists.txt` 的 manual-only EXCLUDE_FROM_ALL target。一个 Terra Medium worker 完成受限测试夹具，主智能体审查和执行 guest 验收。手动 fixture Release 构建 PASS，CTest 17/17 PASS（13.81 秒），diff check PASS。未发现 active checkout 中 VM-free Python helper tests。
- 清理完成：测试包已卸载，原 `C:\ZiliuCompat\settings-fix-1\ZiliuTIP.dll` 注册恢复；65/65 原用户文件 SHA 一致，无额外文件。原始备份保留在 `C:\ZiliuCompat\alpha-full-20260921\pre-userdata`，测试数据保留在同目录 `post-userdata`。正常关机后 exact VM `C35593B3-6EFD-4652-8C35-50C1D2DE6355` CIM EnabledState=3=Off 已确认；其他 VM 和主机安装未操作。
- 下一步：处理四项已复现缺陷并建立有效 private/PIN 验收证据后，冻结新候选包、重跑受影响 gate。签名仍未解决，不能公开发布。主题 GUI 删除和完整视觉回归没有在本轮重新通过；不把历史视觉验收当作当前 alpha 全覆盖。

### 2026-09-21：alpha 冷启动首键修复，限定 VM 验收通过；完整 alpha 仍未通过

- 本轮用户明确暂时忽略额度门禁。工作目录为 `D:\Projects\Ziliu-IME-host-recovery-20260913`，main HEAD `43b70191be7be680b463559d5abf1b65f3ddc4ed`；原始 `D:\Projects\Ziliu-IME` 不修改、不构建。已有 alpha 打包和隐私修复均保留；本轮没有 commit、push、公开发布或主机部署。
- 根因源码链：Broker 在发布 IPC 管道之前执行 Rime 首次部署；旧 TIP 在管道未就绪时创建会话失败并放行按键。新路径以 NMPWAIT_NOWAIT 检查管道，用同 TSF 线程、绑定 context/selection/focus/privacy/generation 的有界队列延迟重放；每批最多 8 键，2048 键上限、30 秒同上下文原文 fallback，切换焦点/隐私/模式取消旧队列。未增加 TIP 中的部署等待。
- 本轮产品/测试文件：`src/ipc/include/ziliu/ipc/pipe_client.h`、`src/ipc/src/pipe_client.cpp`、`src/tsf/include/ziliu/tsf/text_service.h`、`src/tsf/src/text_service.cpp`、新增 `src/tsf/src/startup_key_queue.h`、`tests/CMakeLists.txt`、新增 `tests/tsf_startup_key_queue_tests.cpp`。其中 TSF 文件同时包含前轮隐私修复，不可整文件回滚。
- 一名 Sol Medium worker 实现；主智能体审查焦点、选择区、代际取消、顺序、超时、DLL 窗口生命周期等关键逻辑。最后修正同代 target 验证失败必须取消队列、旧代回调不得清除新代状态。Release x64 PASS、CTest 17/17 PASS、diff check PASS、CodeGraph 已同步。当前 TIP SHA256 `6ab211f39149d90236912bea397d234db0270c455711b2e99091c4325ca257c6`。
- 最终未签名本地候选包：`build/release/cold-start-fix-20260921-r2/Ziliu-0.1.0-alpha.1-win11-x64-unsigned-test-only.zip`，SHA256 `4b775c8b0b8c968f88c227595eba6ba2e5ada64616ab36853440f86a7a1500c5`。r1 只安装诊断，未作为最终验收。
- 证据目录 `build/test-artifacts/alpha-cold-start-20260921`，`evidence.json` 的 passed 仅指其 gate_scope，绝不是完整 alpha PASS。当前 exact VM `ziliu-compat-20260913 / C35593B3-6EFD-4652-8C35-50C1D2DE6355`，guest `USERCUA-NQG4SL2`，本轮 endpoint `172.18.245.60:8000`（以后需重新核实）。
- 第一次诊断在点击 profile 后 94ms 就开始输入，得到 `nih傲`，完整失败证据保留。解释为 Windows 尚未完成 profile activation 仍只是未独立插桩的时序假设，不能宣称已证明或丢弃这个失败。后续隔离 Broker/Rime 冷启动时先给 profile 500ms 激活，不预热 Rime。
- 冷启动正式场景：Broker 不存在，Rime 生成缓存完整移出并保留；profile 激活后500ms逐键 `n i h a o Space`，输入全部结束于04:24:07.702Z，private schema 生成于04:24:24.483Z。初始化完成后剪贴板机器读取精确 `你好`（U+4F60 U+597D），无英文前缀。`cold-stable-actions.json`、`cold-stable-text.json`、`06-cold-stable-final.png`。
- 焦点场景：重新冷部署，排队 `n i`，Ctrl+N 切新标签并 Ctrl+Shift+Tab 返回，等待部署后旧标签保持空白，无旧文字重放；随后新输入精确提交 `你好`。另验证热输入及真实候选框 `ni'hao`，Space 精确上屏 `你好`。没有直接注入 Unicode，没有用截图替代提交文字读取。
- VM 清理：最终包卸载第一次 exit1/rime.dll 占用并生成 cleanup receipt；正常重启后重试 exit0，候选目录已消失。恢复 `C:\ZiliuCompat\settings-fix-1\ZiliuTIP.dll` 注册，65个原用户数据文件逐一 SHA 一致、无额外文件；本轮数据保留在 `C:\ZiliuCompat\alpha-cold-start-20260921\post-userdata`。正常关机后 exact CIM EnabledState=3=Off。主机应用、输入法和其它VM未操作。
- 未完成：密码/PIN/private/cross-focus 的真实应用隐私验收、完整 clean-install/卸载类别清理及其余 alpha 流程；这些不能由17项本地测试或本轮 Notepad 通过代替。签名未解决，仍仅未签名本地测试包，禁止公开发布。下一步以当前修改集继续这些明确门禁，不恢复皮肤研究或新建 VM 基础设施。

### 2026-09-18：竖排适配第一批完成，待用户目检；门禁按用户再次指令临时禁用

- 用户在最新20%门禁后再次明确“暂时禁用额度门禁”，随后执行本批。工作树/HEAD未新提交，保留此前所有未提交修改；主机安装未动。
- 本批文件：src/core/src/sogou_theme.cpp、tests/sogou_theme_tests.cpp（一个bounded Astra Low代理执行，主智能体review）；src/ui/include/ziliu/ui/candidate_window.h、src/ui/src/candidate_window.cpp、tests/ui_preview_window_tests.cpp（主智能体）。其它文件diff属于前轮。无皮肤ID/资源哈希分支。
- 缺V1.pic不再拒绝整个有效H1包；V1残余样式清空，仍检查显式声明的数值/路径/资源。无任何有效H1/V1背景仍拒绝。B原包本次导入exit0。渲染器对缺失SSF布局选择完整字流默认appearance及native路径，保留selected SSF id，因此切回H1仍原皮肤。
- 支持V1 use_gdip=1且无用户字体字号override时，复用已有GDI文字路径；编号以点连接、行距nominal fontsize+4；preedit使用实际GDI cell height（最多nominal font size）。末尾宽度测量与绘制一致并保留caret。A/C实测24/28行距、框高271/334与旧搜狗参考一致；多数所测文字行位置差1px，非精确raster parity。
- V1纵向footer保留从A/C测得的52px pager/menu区域，绘制pager状态和菜单并复用已有quick_menu_action。重要限制：本次未给pager箭头新增鼠标翻页回调，V1菜单guest点击未验收；不得宣称完整操作区交互已完成。不要把旧sogou_horizontal_layout.h里的其它未用常量当本轮新证据。
- C横向切片137+229>=源宽347导致旧背景拒绝。保留右侧原图像素，重叠收敛为1px seam，按原stretch/tile绘制；实机背景已恢复。非当前已测等价类/其它DPI仍待验证。
- 第一版A preedit末尾裁切，已在第二版修正V1测量方式并用同一Latin ink advance绘制。第二版Release x64及CTest13/13全部通过（13.99s），git diff --check通过，CodeGraph sync完成。active worktree scripts中没有VM-free Python tests或旧test-cua-skin-vm脚本，未新建framework，也未借用旧脏树gate；本批是当前既有VM手工编排的真实输入证据，不是旧gate命令PASS。
- 证据build/test-artifacts/vertical-fix-20260918：captures.json、result.json，a-final/c-final/b-fallback/native-vertical/b-horizontal/h1-regression.png。A/B/C完整包未改。A最终97x271，C347x334，B回退444x364（含阴影）。B回退与直接选择字流default逐像素diff=0；希罗H1与此前ssf-background-fix/02-ziliu.png逐像素diff=0。原搜狗参考在original-three-vertical。
- guest最终TIP SHA b64feeb277e65d30e62c3dccc9af43ee0e6242c3a1ec9bb9d6cccbcdcdd0c848；本轮最初的旧TIP备份C:\ZiliuCompat\VerticalFix20260918\before.dll（SHA5a4106...）。第一版DLL受系统占用，保留为settings-fix-1\ZiliuTIP-v1-loaded-backup.dll后写入第二版；注册路径未变。只关闭本轮guest测试Notepad PID1168/1576以重载，未关闭主机应用。最后真实输入Notepad PID7600、Broker PID1336，逐键nihao+Space后剪贴板精确读取你好（PID/HWND关机后作废）。
- 既有VM C35593B3-6EFD-4652-8C35-50C1D2DE6355新IP172.18.254.185，经精确NIC查询确认；guest hostname USERCUA-NQG4SL2。两次无前台窗口误报均先WTS确认flags1=UNLOCKED，再正常launch/activate既有测试窗口恢复。未改CUA、网络、VM配置/注册。session字体仍原Kingnam SHA778584...。
- 最终恢复完整settings.ini SHA4ff103afe8f00d372b2a449904492187894bd9774537cfddd8ac97a152c2a861（horizontal/default）；正常shutdown返回0，精确CIM EnabledState3=Off。无host部署、无commit。等待本批视觉反馈，不宣称所有竖排、DPI或交互完成。

### 2026-09-18：竖排适配获授权；实时额度14%，尚未开始实施

- 最新用户授权：按此前横排方法处理自定义SSF竖排；SSF明确引用的通用素材不得忽略；皮肤本身不支持竖排时fallback到字流默认皮肤（不是复刻搜狗默认外观）。
- 接续目标：A/C有V1资源应按数据绘制并对照既有搜狗竖排证据；B缺Scheme_V1.pic应保留有效H1导入能力，V1使用字流默认皮肤。不可按skin ID或素材朴素程度分支。背景、文字几何和缺失布局处理要分别验证，并回归已验收横排。
- 新提供的替换版AGENTS.md再次明确remainingPercent<=20硬暂停，时间顺序晚于旧“暂时禁用”指令。本轮官方get_usage_limits实测usedPercent=86，即剩余14%；观察时间2026-09-17 23:05:19 UTC（2026-09-18 08:05:19 JST）。State=PAUSED_QUOTA。
- 本轮未修改产品代码、未检查/改动源码工作树、未构建/测试、未启动VM或代理。仅读取额度、记忆索引及本文件头并记录接续状态。当前HEAD/工作树/VM状态尚未重新核验，不把旧状态当成当前事实。
- 门禁解除后从当前active worktree的HEAD/status及原三皮肤证据检查开始；先CodeGraph定位converter布局有效性、主题fallback与竖排绘制/测试，按小范围实现，不重建测试环境。

### 2026-09-14：纠正CUA假锁屏诊断；原三皮肤竖排现状已采集

- 用户已明确暂时禁用额度门禁，并肉眼验收前一轮五皮肤背景修复。随后只要求原始A/B/C竖排现状及搜狗对照，不授权顺手修竖排。
- 曾错误地仅凭CUA返回Windows desktop is locked反复要求用户手工解锁。用户授权限时诊断后，独立只读证明：guest User/session1/WinSta0/Default；OpenInputDesktop也为Default；WTSQuerySessionInformationW(WTSSessionInfoEx)返回level1、session1、state0、SessionFlags1=UNLOCKED。Explorer存在，但GetForegroundWindow=0。
- 读取既有computer_server/handlers/windows.py发现is_desktop_locked仅判断GetForegroundWindow()==0；这是这次误报的来源。Microsoft GetForegroundWindow文档明确NULL并不等于锁定。没有修改/禁用检查、没有输入密码、没有重启服务/改任务/修网络/重建VM。
- 在Windows独立确认UNLOCKED且Default交互桌面后，通过既有guest CUA launch(notepad.exe)正常打开测试程序，截图立即恢复。用户手工操作0次。未来同类报错先独立核实OS锁定状态；若真正锁定/Winlogon则不得绕过；不要再把CUA一句报错当完整诊断。并非永久修复了第三方CUA误判函数。
- 竖排证据在build/test-artifacts/original-three-vertical/result.json。产品TIP SHA仍5a4106af100b513045e0c9f21cc90b61add1087080a2db6465fe4743833f4270，没有源代码修改、构建或提交。
- 原始A若叶睦、C卡提希娅经当前产品SSF importer安装成功；B不要抢走我的小祥被原产品拒绝：Scheme_V1.pic: same-window scheme must provide a background picture。不得伪造B字流截图，也不为截图删除B的V1字段。
- 字流A竖排101x331；字流C竖排347x353且背景仍为纯色。搜狗A138x271；B121x254为不支持自定义V1时实际默认回退；C370x334。均96DPI、7候选、逐键nihao，字流与搜狗候选不同。五张真实候选图a/c-ziliu.png和a/b/c-sogou.png，另b-sogou-notice.png是搜狗明确提示不支持竖排合窗口模式的证据，不是第六张候选框图。没有声称竖排通过。
- 首次字流输入首键n直出、剩余ihao组合，取消并删除该测试n后重新逐键完整nihao取得正式截图。未使用Unicode注入代替真实按键。
- 搜狗仅GUI切横排到竖排，候选数维持7；一度误打开候选数菜单但未改值，收起后正确切方向；结束GUI恢复横排。字流仅candidate_layout和active_theme_id临时更改，结束完整settings.ini SHA恢复4ff103afe8f00d372b2a449904492187894bd9774537cfddd8ac97a152c2a861。搜狗最后自定义皮肤C保持启用。
- 正常guest shutdown /s /t0返回0，精确CIM EnabledState3=Off。主机安装、键鼠、配置和其它VM未触碰。下一步等待用户对竖排现状的指令；B缺V1资源处理、C竖排背景、A/C字距与行高皆未修。

### 2026-09-14：H1零/重叠纵向切片背景修复；额度门禁PAUSED

- 用户授权“开始”修复五个背景缺失样本，要求正常五个无回退，不改字体、间距、菜单或设置。本轮产品仅改src/ui/src/candidate_window.cpp，测试仅改tests/ui_preview_window_tests.cpp；保留所有既有未提交工作，无commit/主机部署。
- 证据：旧搜狗5张图右侧末100列、前60行的全不透明像素，与源PNG同纵坐标完全一致，像素数分别3994/3471/3074/3632/3056，排除整体纵向压缩假设。源图高147/129/147/147/130，上下切片95+52造成零/重叠。
- 新路径仅限custom SSF、H1、无用户字号/字体override、纵向切片相加>=源高、横向切片有效、目标可容纳自然图高及固定左右区。按完整源高度画横向三段，不推断vertical layout、字体override或目标缩小的未知规则。正常九切片路径未修改。未改变window sizing、文字、菜单、设置、SSF或converter。
- 新增在私有桌面上的逐行像素测试：正常/零中段/重叠三组，验证源图每行左右边缘保留。第一次测试错误地把144DPI主机物理像素当96DPI源行（y=1实际仍源行0），仅诊断追加后确认dpi_scale=1.5、431x330；改为临时96DPI DC绑定并RAII恢复。曾因基类指针到DCRenderTarget编译错误，通过COM QueryInterface修正。未放宽像素断言。最终Release构建PASS，CTest13/13 PASS，14.19s。
- 本轮实机证据目录build/test-artifacts/ssf-background-fix，captures.json记录10张原始截图，NN-ziliu.png为原HWND矩形无缩放裁剪，commit-full.png为真实提交截图。前轮搜狗对照仍在random-ten-ssf/NN-sogou.png，本轮没有操作搜狗或重抓reference。
- 5个原失败样本1/3/7/8/10全部恢复背景；同一右侧不透明ROI对前轮搜狗参考，分别3994/3471/3074/3632/3056像素全部相同，mismatch=0,sum_abs_rgb=0。此仅证明该ROI，不冒充整图parity。
- 正常样本2/4/5/6/9新旧候选框裁剪逐像素相同，different_pixels=0。10个样本窗口尺寸全部与修复前一致。真实Notepad PID9420、candidate HWND131938（已过期）；逐键n/i/h/a/o，Space提交，Ctrl+A/C后CUA剪贴板机器读取精确你好；ZiliuBroker PID7316已确认。
- exact VM仍ziliu-compat-20260913 / C35593B3-6EFD-4652-8C35-50C1D2DE6355，guest USERCUA-NQG4SL2，既有endpoint172.20.54.221。只启动这一台且无新基础设施。guest TIP更新SHA5a4106af100b513045e0c9f21cc90b61add1087080a2db6465fe4743833f4270，旧TIP SHAe2408d...备份C:\ZiliuCompat\SsfBackgroundFix20260914\ZiliuTIP-before.dll；注册路径未变。临时字体仅session加载，未修改持久字体配置。
- 结束guest设置完整SHA恢复4ff103afe8f00d372b2a449904492187894bd9774537cfddd8ac97a152c2a861；shutdown.exe /s /t 0返回0；精确CIM核实EnabledState=3。主机安装、键鼠、IME、其它VM未触碰。
- 官方额度最后sample usedPercent=80，remainingPercent=20，记录时间：2026-09-14 01:44:21 UTC。State=PAUSED（quota gate）。此后只写本交接记录，不再调用代码/VM/测试工具。
- 后续CodeGraph sync与git diff --check尚未在本轮末尾执行（已触发门禁）；下次额度恢复且获继续授权后先做这两个收尾检查。不得将新背景路径扩大为未知DPI/字号/竖排规则；等待用户目检本批截图。本次仅背景缺失修复，不声明所有SSF或完整几何parity完成。

### 2026-09-14：随机十皮肤实机采集完成，等待用户目检

- 用户要求随机10个自定义SSF，不调整设置直接安装，交付字流/搜狗20张候选框截图；另行明确允许先补齐原始SSF导入。未授权在本批测试中调renderer以消除失败。
- 当前branch/HEAD仍为codex/custom-ssf-geometry-20260914 / 5b2be8c92e5f23df180e29ed9307d4da37fc8a4d。没有新commit，没有主机部署。
- 新增src/settings/sogou_theme_import.h/.cpp、tests/sogou_ssf_import_tests.cpp；修改container.h/.cpp支持安全ZIP+原Skin-v3解码、SHA256内容地址ID、资源验证/PNG转换、原子发布与失败清理。MainWindow.xaml.cpp和vcxproj接入SSF文件选择，tests/CMakeLists.txt注册测试。导入器不激活、不改设置、不改字体。Release构建通过，CTest13/13通过；git diff --check通过（vcxproj有LF/CRLF提示）。本任务未改candidate_window.cpp；该文件现存diff属于此前用户验收的几何/菜单修改。
- 证据目录：build/test-artifacts/random-ten-ssf。selection.json冻结seed=10285432334467308632，从126个去重包随机选10，未重选。guest-install-results.json记录10个实际产品导入均exit0。guest缺Settings程序，本批通过测试程序--install调用与新Settings入口完全相同的产品安装函数；不是Settings GUI安装验收。
- captures.json列出20张交付原图及9张不参与验收的旧诊断图。crops.json记录20张候选HWND矩形原尺寸裁剪及SHA；NN-ziliu.png与NN-sogou.png为交付图片。没有重绘、缩放、像素修图或私有桌面渲染替代。真实Notepad内逐键nihao，Rime Broker参与；两输入法候选不同，宽度不比较。
- 随机顺序：1橘雪莉1.3、2希罗、3爱弥斯、4安安、5希罗2.1、6尼古喵喵1.3、7汉娜1.2、8雪莉2.1、9安安2、10驹泽乃依1.2。
- 实际观察：字流1/3/7/8/10背景没有绘制，2/4/5/6/9绘出背景。背景切片有效性分支拒绝上下切片相加大于等于图片高度，是已发现的相关缺陷；本轮未修改。不可宣称这10组通过，也不可把有背景5组自动算成几何验收通过。
- 采集失误已纠正：只改active_theme_id但连续在同一输入焦点打字，初始2-10仍显示皮肤1。发现颜色/高度与manifest不符后，重新取得焦点使主题刷新，重新采集2-10。有效字流2-10来自NN-ziliu-activated-full.png；初始NN-ziliu-full.png仅诊断，不能拿来配对。不要重复声称全部10个缺背景。热刷新原因未完全诊断，不在本轮顺手修。
- 所有图片在ziliu-compat-20260913 / C35593B3-6EFD-4652-8C35-50C1D2DE6355 / USERCUA-NQG4SL2中取得，96DPI。guest注册路径仍C:\ZiliuCompat\settings-fix-1\ZiliuTIP.dll。更新为本次TIP SHA e2408dda6b211126a96d7fead948d88b2b7ae0467275543e1dbf8a3242b1058c，旧TIP保存在C:\ZiliuCompat\RandomTenSsf20260914\backup\ZiliuTIP.dll。未改注册；Broker仍原有真实Rime后端。
- SSF在guest的C:\ZiliuCompat\RandomTenSsf20260914\skins\01.ssf至10.ssf；安装主题在User LocalAppData Ziliu/themes/sogou.<完整SHA>。只切active_theme_id，并逐次核对其他配置字节不变；结束恢复原org.ziliu.default，整个settings.ini SHA重新等于4ff103afe8f00d372b2a449904492187894bd9774537cfddd8ac97a152c2a861。
- 搜狗仅用已有SSF association打开10个包，未操作其他选项；最后激活第10皮肤。实际参考runtime16.8.0.5293，不是clean4385认证。结束SogouTSF.ime SHA仍1b4bfcdd2ac2c069fb9c65b90007ed635e520465586a583d1f3ae197d7944ca2；SogouExe SHA仍bfd47d65536c3be6f1149bb0b4a66a1d3e41f7c1be9315dbc15bf30ce38b2711。磁盘仍有16.6.0.4385和16.8.0.5293目录，不能把目录存在误述为唯一版本安装。这批为用户目检对照，不升级为旧pinned4385矩阵认证。
- guest正常shutdown /s /t 0返回0，之后精确CIM核实EnabledState=3（Off）。其他VM未动，无新基础设施、无主机键鼠/IME/设置操作。临时session字体随关机卸载。
- 下一步：先让用户看20图；如用户授权修缺陷，再用一个已失败SSF验证通用零/负中间切片处理，独立处理主题刷新。不要改皮肤参数或重选样本掩盖缺陷。额度最后读取剩余29%，不扩展本轮任务。

### 最新用户验收与三条杠菜单恢复

- 用户已明确“肉眼验收通过”，认可上一轮三对图的文字/几何效果；随后只要求最后候选右边恢复三条杠菜单，不调整其他选项。
- 已按该范围完成：只新增菜单bounds与三条线，使用现有quick_menu_action_点击回调；原有63px预留区不变，未改尺寸/字体/间距/图像。
- 新输出a/b/c-menu-restored.png相对用户认可的*-review-ziliu-final.png，只有16x10图标区像素变化：A[502,110,518,120]、B[611,212,627,222]、C[589,108,605,118]，其余像素完全相同。
- Release及CTest12/12通过，私有桌面点击消息测试调用菜单回调一次。本轮没有VM、主机部署或commit；修改仍在当前未提交工作树中。后续不要把旧“右侧菜单未恢复”的历史描述当成当前状态。


### 最新：连续修复已达到选定几何指标，待用户六图目检

- 当前HEAD仍5b2be8c；未提交改动现在是7文件+294/-3：原5文件use_gdip补丁，加candidate_window.cpp和tests/ui_preview_window_tests.cpp。不要按下文旧“五文件/renderer未改”处理当前树。
- 当前renderer已实现H1显式use_gdip的GDI字形mask、编号/正文分run、同测量路径的宽度/光标、不同英文字体ASCII拼音按字形右边界推进及cell对齐、固定10px候选间距、右侧操作区占位、抑制H1纯颜色旧分隔线。没有皮肤ID或字体名常量分支，没有新增FreeType产品依赖。
- A拼音现在[35,77,90,92]，与参考完全一致。B/C拼音与光标也完全一致；首候选XYWH、编号正文间距、首/次候选间距、拼音到候选距离均在±1px。机器数据：build/test-artifacts/ssf-geometry-20260914/selected-geometry-final.json。
- 六图：a/b/c-review-reference.png（真实搜狗guest裁剪），a/b/c-review-ziliu-final.png（当前候选窗私有非输入桌面绘制导出，不是真实TSF截图）。全部位于上述证据根。不要再展示旧*-geometry-fixed/ink-advance/split-gdi等中间图冒充最终图。
- 完整Release x64及CTest12/12最终通过；32次重绘GDI对象数检查、默认外观返回、非ASCII拼音fallback等测试通过。分隔线测试的失败因DIP像素采样坐标错误，改正确坐标后保留白色精确断言通过。一个更早的默认动画时序测试曾失败一次，未改断言复跑通过，最终整套通过。
- 主机未部署，没有新commit，VM已正常shutdown并精确CIM确认Off，所有Frida已detach。
- 只报告选定几何3/3，不等于全皮肤产品3/3：真实原始SSF导入、所有字体/字符串/DPI/V1、右侧菜单/操作图标完整行为仍未验收。右侧现在保留空间避免人物压住末尾候选，但不宣称图标已复刻。
- 新取证和失败判断见run.json的continuous_repair_result；其新状态优先于更早NOT_FIXED、A未知、renderer已撤回等历史字段。
- 主机Consolas与guest字体hash不同，已把guest-consola.ttf作为忽略的本地证据保存，未安装/提交；差异不是本次几何误差的已证实根因。修复来自实际字形边界与分run度量，不是换字体版本。


- 用户再要求“行，修复一下”后，仅完成一个有界ANSI接口核验与本地Consolas字号/编号宽度测量；未得到A拼音实际参数，故可见缺陷仍NOT_FIXED。结果见run.json的requested_fix_followup及a-ansi-path-diagnostic.json。不能把这一轮称作修复完成，也不要继续堆同类API探针。五文件flag补丁未变，未部署主机；VM正常shutdown后精确CIM确认Off。

- 再后续A取证：a-final-o-text-trace.json确认A候选词Kingnam/-20/19px行高、label(31,106)及text(45,106)合成；未捕获英文preedit。新串GDI W/GDI+检查及字形编号检查亦无preedit正面证据，不能推断实际字体错误。该API探针路线已结束，不再盲扫。细节见run.json的a_font_role_investigation及a-glyph-index-trace.json。
- a-emoji-off-full.png是A新原图，SHA89fb139e418d50f7f1f9ac76726a8e6f64d7d255dd281a8f7767295cffb56e04。本轮没有产品源码修改，五文件flag修复仍未提交；英文拼音问题仍UNKNOWN。

- 用户之后明确说“好，继续工作”，已恢复工作；下方PAUSED描述仅是当时交接快照，不再代表最新授权。
- HEAD及五文件+59/-0未提交修复未变。新证据见run.json的resumed_after_handoff。
- 已取得正常末键o的GDI/AlphaBlend完整有界记录：b-final-o-text-trace.json。方向键重绘触发候选编辑的记录被降级为diagnostic。
- B皮肤单变量诊断：仅将Guest搜狗emoji开关关闭，首候选ink框从[141,211,189,227]变成[141,208,189,224]，纵向上移3px、高度不变。说明旧参考的emoji内容影响了行位置，不能只忽略总宽度就认为其他几何条件相同。
- 新图b-emoji-off-full.png和emoji-off-settings.png已保存。Guest的emoji目前留为OFF，颜文字及其他开关不变；这不是产品禁用emoji方案。恢复采集须明确这一状态。
- 当前GDI试验仍隔离、未重新应用；B候选纵向与该试验一致，但编号/文字横向差2px及A字体角色仍未解决，完整通过数仍0/3。
- 新阶段没有产品源码修改或主机部署；所有Frida session已detach，Guest已请求正常关机。状态以最新工具记录/run.json为准，不复用历史PID。

## 0. 历史交接：PAUSED / HANDOFF

- 用户最新指令是“可以开始办任务交接了”。本轮只整理交接，结束后保持 PAUSED，等待用户明确继续。
- 本次不是额度耗尽：官方最新样本 usedPercent=79，剩余21%，ordinaryUsageAllowed=true。用户此前指定剩余15%办理交接，现在主动要求提前交接。
- 不因本文的“下一步”、旧“全部允许”或旧自动化规则启动开发、VM、测试或代理。恢复后使用最新用户指令和官方实时额度，不能把本样本当当前值。
- 当前完整皮肤验收仍为 **0/3**；六张最终通过截图未交付。框高一致、测试通过、单个字形包围框一致都不等于皮肤通过。
- 当前唯一工作目录：D:\Projects\Ziliu-IME-host-recovery-20260913。
- 当前 branch：codex/custom-ssf-geometry-20260914。
- 当前 HEAD：5b2be8c92e5f23df180e29ed9307d4da37fc8a4d。
- 当前 tracked 修改：5个文件，+59/-0，全部是主 Codex 本阶段新增的 use_gdip 数据修复，尚未提交。另有未跟踪 HANDOFF.md。
- candidate_window.cpp 当前无未提交修改。两个未通过的绘制试验均已撤回；不能误把试验图当当前产品输出。
- 主机安装未改变，仍是用户接受的80d37574基线。当前本地构建不是主机安装。
- 本任务最后使用的 VM 已通过正常关机关闭，精确 CIM EnabledState=3 曾确认；Frida finally detach 已确认。**本次交接没有重新查询 VM/进程，不宣称新做了主机扫描。**
- 本轮没有构建、测试、安装、部署、VM操作、代理或新基础设施；只读取工作状态、已有记录并更新本文。

## 1. 产品目标及最新验收合同

最终方向：用户启用自定义搜狗 SSF 时，字流用通用规则渲染候选框。保留字流自己的词库/排序后端。独立搜狗默认皮肤、搜狗设置/广告/云服务不是复刻目标。

用户已明确改变当前阶段验收方法：

1. 先完成下文A/B/C三个皮肤，H1横排、96 DPI、7候选、单行。
2. 测量框高、文字高度、相同字符的宽度、上下左右边距、编号到文字距离、候选项间空白等，容许约±1个原始像素。
3. **不比较候选框总宽度**；两个后端的不同候选词自身宽度也不要求相同。
4. 每项要求达到后，交付三个皮肤×两个输入法的六张候选框图，由用户肉眼反馈。
5. 不把抗锯齿阈值变化、不同候选词/emoji、透明画布或图片装饰误算成文字/布局错误。
6. 不再拿历史95/1260、103/1260或旧矩阵通过数作当前进度。没有运行新矩阵。

边界：

- 无皮肤名/ID/固定tuple专用偏移，无636332、Baidu/BPS、独立default复刻。
- 不控制主机键鼠，不切换主机IME，不注册开发TIP，不显示PowerShell/UAC/VM控制台。
- 真实搜狗/输入/设置/截图只能在隔离guest。纯绘制允许私有非输入桌面，但须标为诊断导出，不能冒充TSF输入截图。
- 不新建VM，不修网络/防火墙/HNS/APIPA/VHD/checkpoint，不扩建Oracle、wrapper、helper/framework。
- 不创建代理。不要恢复环境里列出的旧代理。
- 不自动部署皮肤实验到主机。保护既有搜索框、动画、预览、简繁、符号输入等功能。
- 用户已经取消与外部ChatGPT会话逐阶段通信，不要自行恢复浏览器接力。
- 活跃时每10分钟及长构建/VM/取证前查询官方额度；usedPercent是已用量。此前本次续作采用15%交接线，30%以下不扩范围。当前为用户主动交接，必须等新指令。

## 2. 工作树与安装基线

| 对象 | 状态 |
|---|---|
| D:\Projects\Ziliu-IME-host-recovery-20260913 | 唯一产品工作树；本次实时核对 branch/HEAD/status/diff |
| 当前HEAD | 5b2be8c92e5f23df180e29ed9307d4da37fc8a4d |
| 当前HEAD parent | 80d37574e75701d6a7cfbf43fe395789b613b72c |
| HEAD消息 | fix(theme): preserve authored custom SSF geometry |
| 用户接受的主机基线 | 80d37574e75701d6a7cfbf43fe395789b613b72c |
| 基线标签（历史已核对） | baseline/host-accepted-20260914 |
| D:\Projects\Ziliu-IME | 原始巨大脏树，只读；历史HEAD 3d176d8f4e33ed24baf9e505626e0f2cf4e66b25 |
| D:\Projects\Ziliu-IME-clean-base-001 | 旧salvage资料源，只读；历史HEAD eff997ee30614ec8e4dc43cb945ff96235a515e3，含旧实验 |
| 当前build | build\local-x64-Release；已恢复为“当前HEAD+use_gdip数据修复”，不含GDI试验 |

不要在原始或旧salvage源码树build/restore/clean/commit，不要整文件搬运它们的renderer或Settings大patch。

### 主机安装（沿用此前验证，交接未重新扫描）

注册值：
HKLM\Software\Classes\CLSID\{7B9C1D3D-9D4E-4F02-A9A6-3EBA99CDE7B1}\InprocServer32

安装TIP：
D:\Projects\Ziliu-IME\build\validated-x64-Release\bin\ZiliuTIP.dll

TIP SHA-256：
2928653ded6a33fddf40ee24282ec6a408855746435b6c5921e2b5c7b2e88dd7

同目录 ziliu-build-manifest.json 记录基线；建立基线时核对了11个文件hash。有保留的早期Settings/其他组件，不能宣称每个EXE都由80d37574新编译。

Broker SHA：
7481552a476931d2a398356b8f7ad2f2c68ab6ac8a74d311eab404ef00de10af

主机设置：
C:\Users\Matsu\AppData\Local\Ziliu\settings.ini
最后记录SHA：ce6d0f83b92876db96c062e97b336b65d78faa381c3d0ba98be4c970a355d040

### 不得破坏的近期功能

- b170293048264e6b462d635bc431cb6027c7a44e：Chromium成对符号光标。
- 7b2ce56fd70ebb0f392991cad430bfa02fd2d03e：认证的AppContainer IPC。
- 3239668df027998d2e4d6c70a3c86404a7f29ba3：Broker同步搜索框设置，Ctrl切换/横排七候选。
- b63b2302a1eab08575ad9aa0ad77a3c8e7af2b3a：异步跟随光标，随后发现竞争闪烁。
- 80d37574：QueuePresentation / PresentAtCaret统一位置来源；首帧等待决策，更新保留有效位置，超时不跳回旧TSF位置，焦点/生命周期作废旧结果。用户确认可用。
- UIA工作线程100ms超时、可见时100ms查询、每窗最多一个进行中请求，私有预览不走该定位路径。
- 其他已接受功能：紧凑/曲线圆角、阴影/宽度动画、Settings预览随父窗移动、简繁语义输出、快捷菜单暗色。皮肤实验没有重新部署这些组件。

## 3. 已提交与未提交实现

### 已提交：5b2be8c（+183/-9，两个文件）

文件：
- src/ui/src/candidate_window.cpp
- tests/ui_preview_window_tests.cpp

确定的几何修复：

1. 自定义SSF未启用用户字号覆盖时，不再将已按字号设计的边距乘以font_size/17；DPI仍另外处理。显式用户字号覆盖保留旧缩放。
2. H1拉伸/平铺也保留背景图片自然高度下限，仍受工作区最大高度限制；固定高度路径保留。
3. H1测量/绘制使用点号编号，不用两个空格。
4. H1去掉额外通用8px左内边距。
5. 无字体/皮肤ID特判，无新文字renderer，未部署主机。

测试新增了原始边距、220px合成背景高度、字号覆盖、4096px工作区限制及切回native布局检查。构建/CTest通过，但并非所有DPI/V1已验收。

同一提交已移除旧 --font-probe-ssf GDI+诊断及 tests/CMakeLists.txt 中相应链接增量。未知CLI参数返回exit1，旧probe命令不能再用。旧probe PNG保留为诊断。

### 未提交：use_gdip数据修复（本次实时核对）

ownership全部为CODEX；开始本阶段时tracked tree clean，修改来自当前主代理。

| 文件（相对唯一工作树） | diff | 内容 |
|---|---:|---|
| src/core/include/ziliu/core/theme_manifest.h | +2 | ThemeTypography新增optional<uint32_t> sogou_use_gdip；缺失不等于默认绘制策略 |
| src/core/src/sogou_theme.cpp | +9 | MapDisplay解析Display.use_gdip，只接受0/1，错误带字段位置 |
| src/core/src/theme_manifest.cpp | +13 | 可选字段读写、值域校验；旧manifest保持兼容 |
| tests/sogou_theme_tests.cpp | +21 | 缺失/0/1、非法值、converter→manifest往返 |
| tests/theme_manifest_tests.cpp | +14 | 旧manifest不注入标志；错误数值/类型/in-memory值被拒绝 |

合计+59/-0；没有新commit。HANDOFF.md为另一个未跟踪文件。不要自动丢弃、提交或覆盖。

重要：**只保留原始flag，不给use_gdip命名推导一个已证实的renderer策略。当前产品仍用DirectWrite；不是已完成GDI切换。**

### 当前代码入口与缺口

- codegraph explore/node先于源码搜索；结构修改后codegraph sync .，最后已sync。
- converter：ConvertSogouThemeIni / MapDisplay / MapSurface。
- UI：ShowInternal / EnsureDeviceResources / Paint / DrawSurfaceBackground。
- 当前preedit仍调用旧ResolveSogouPreeditDWriteFontSize（减1，旧注释谈Arial）；候选仍CENTER。这些不是新确认的通用搜狗规则，不要盲改常量。
- 当前Settings InstallThemePackage仍是ZLT导入，ThemeCatalog读manifest目录。
- DecodeSogouSsfV3只解加密Skin-v3，不解这三个ZIP型SSF；测试用只读ZIP提取的UTF-8 ini和原PNG。不能说三个SSF损坏或原始SSF端到端导入已完成。
- 三个包skin_name都为new，当前生成ID可能同为sogou.new；测试以独立目录/注入隔离。不要原样放入同一个catalog；将来需要通用包身份方案，不能按皮肤ID修渲染。

## 4. 三个确切样本

全部为ZIP SSF、UTF-16 ini、use_gdip=1、LargeFontSupport=1，H1无custom overlay。
边距原文顺序是top,bottom,left,right；manifest为left,top,right,bottom。

### A：若叶睦

路径：C:\Users\Matsu\Downloads\输入法皮肤\1.若叶睦 输入法皮肤\若叶睦输入法.ssf
SHA：9b448ade798d2f355b05e6d3a66685acd11da347b1a3c82777a90f91285142ab

- skin1.png：422×141。
- font_size=20；font_ch=荆南麦圆体；font_en=Consolas。
- layout_horizontal=0,50,180；layout_vertical=0,40,11。
- pinyin_marge=78,3,33,32；zhongwen_marge=4,0,31,120。
- separator=0xd8d8d8,33,120；anchor=11,47。

### B：不要抢走我的小祥

路径：C:\Users\Matsu\Downloads\输入法皮肤\10.不要抢走小祥 输入法皮肤\不要抢走我的小祥.ssf
SHA：f3a1fbb088cc42c32238983d41a9a42ddab39d407a88e45ce8cd49c76ee11e6d

- skin1.png：630×400，大块透明区是图片画布的一部分。
- font_size=20；中英均荆南麦圆体。
- layout_horizontal=0,149,160；layout_vertical=0,75,29。
- pinyin_marge=165,3,140,79；zhongwen_marge=19,0,140,95。
- H1无separator；anchor=67,105。V1部分字段存在但无pic，未验收。

### C：卡提希娅

路径：C:\Users\Matsu\Downloads\输入法皮肤\11.卡提希娅\卡提希娅 主体委屈 状态栏开心.ssf
SHA：4105ee8f6a419fde2bca2b4c65334d6c9e46167af0008843314751875dfc42bc

- skin1.png：469×144。
- font_size=24；中英均荆南麦圆体。
- layout_horizontal=1,111,191（tile）；layout_vertical=0,75,29。
- pinyin_marge=69,4,32,78；zhongwen_marge=4,0,31,150。
- separator=0xd8d8d8,32,196；anchor=44,37。

字体文件：
C:\Users\Matsu\Downloads\输入法皮肤\10.不要抢走小祥 输入法皮肤\荆南麦圆体（请先安装字体）\荆南麦圆体.otf
SHA：778584cf376fda04cbd165b35a1f147a9b215169ff23d539e31973263fbe5c65
英文family：Kingnam Maiyuan。

主机此前已有同名字体，不曾修改主机字体。Guest使用AddFontResourceExW(path,0,nullptr)返回2 faces，**仅当前会话加载**，未做持久注册；每次新开guest需在目标应用启动前准备，不能假定重启后仍有效。

## 5. 参考版本决定与可靠性

### 当前明确选择：16.8.0.5293

旧测试guest从16.6.0.4385自动升级。用户说“你来判断”，Codex选择16.8.0.5293为新的明确参考目标，用户随后继续授权。不是悄悄把旧参考改名。

- 16.6.0.4385目录仍在，但检查的候选进程加载SogouTSF.ime、SogouPY.ime、Resource.dll均为16.8.0.5293。
- SGTool、SogouCloud等检查到的主版本组件指向16.8目录。
- Components下存在独立版本插件DLL；不能要求它们版本字符串等于主产品版本，也不能据此宣称所有可选组件均已审计。
- 不能删除残留目录、卸载/重装/回退或修升级系统以求继续。
- 每次取证前后重核关键hash。当前未设置自动升级封锁；没有“永久冻结不会变”的保证。

当前记录的核心文件（Guest路径）：

| 文件 | SHA-256 |
|---|---|
| C:\Program Files (x86)\SogouInput\SogouExe\SogouExe.exe | bfd47d65536c3be6f1149bb0b4a66a1d3e41f7c1be9315dbc15bf30ce38b2711 |
| C:\Program Files (x86)\SogouInput\16.8.0.5293\SGMyInput.exe | 8114ab084402b36c59b8a926d704b87dbe6d564c85cc15eeabb0f85500953f49 |
| C:\Windows\System32\SogouTSF.ime | 1b4bfcdd2ac2c069fb9c65b90007ed635e520465586a583d1f3ae197d7944ca2 |
| C:\Program Files (x86)\SogouInput\16.8.0.5293\SGTool.exe | 63a47a3c95c1bca1115bca19f2f060c54103fa51f30380a3a02d04d04252dd51 |

四者FileVersion/ProductVersion已核对16.8.0.5293。
TIP GUID：{E7EA138E-69F8-11D7-A6EA-00065B844310}；InprocServer32指向System32\SogouTSF.ime。
最近一次B取证前又核对了SogouTSF和SogouExe hash与以上相同。

### 新三张参考

证据根见第6节。完整屏幕PNG的SHA：

| 样本 | 文件 | SHA | HWND crop矩形 |
|---|---|---|---|
| A | a-sogou-5293-full.png | 66ec8bdb5a716ebbd3f57b36b419097ba3ab8cc3f26f9100b185ea2c391c3bdb | [89,140,852,281] |
| B | b-sogou-5293-full.png | 615a100e1e26f25ec123572a323a964e112ea08d9c7470f7f04eecbeccac503f | [33,82,880,482] |
| C | c-sogou-5293-full.png | c70a12d74e8d941cfa591a3595692e011caddeacd74ae3d49206e58dcae97c54 | [56,150,950,294] |

窗口SoPY_Comp、96DPI，捕获时所属Notepad PID6912；PID/HWND是历史值，不可复用为新操作目标。
裁剪为a/b/c-sogou-5293.png。输入为真实逐键n/i/h/a/o。三个Guest皮肤文件SHA匹配原包。

级别：**REFERENCE_UNDER_AUDIT**，不是完整authoritative acceptance。
仍缺完整当前settings机器冻结、AllSkin副本/active-skin身份链、最终候选列表的完整机器证明。不能拿这些新图直接宣布全部通过。

历史已观察设置：H1、7候选、单行、跟随光标ON、分窗口OFF、高分屏适配ON（当前96DPI）、浅色、字体/字号/颜色覆盖OFF。云建议/emoji未完全关闭，候选会变化。

AllSkin导入副本位于C:\Users\User\AppData\LocalLow\SogouPY\AllSkin\skin-a.ssf等，是文件，不是目录；与当前active状态的完整绑定未做完。

### 不再可用的旧结论

- 16.6安装包是历史来源：E:\VMs\Cua\Ziliu-IME\fixtures\sogou\pinyin_guanwang_16.6c.exe，SHA d02982e4ed900b56aa000763b6a66476d8ae35122ed35f6b663156021b297003，221965528bytes，/S exit0。
- 旧a/b/c-sogou-fresh*.png及早期字体trace缺连续版本校验。不能继续当4385权威参考。
- 曾把“重启Notepad后字体metrics变化”归因字体缓存；现在有自动升级这一混杂因素，**不能断言完全由缓存造成**。
- 旧4385/4777污染环境不用于任何新权威参考。
- 不能把新target的GDI证据推广到所有版本、所有SSF或所有fallback路径。

## 6. 文件与证据索引

唯一当前证据根：
D:\Projects\Ziliu-IME-host-recovery-20260913\build\test-artifacts\ssf-geometry-20260914

| 文件组 | 含义 |
|---|---|
| source-a/b/c\skin.ini、skin1.png | UTF-8化的原ini、原PNG资源 |
| a/b/c-initial.png | 无效：初次主题注入被RefreshTheme覆盖成default |
| a/b/c-baseline.png | 几何修复前诊断 |
| a/b/c-geometry-1.png、geometry-2.png | 几何修复中间阶段 |
| a/b/c-geometry-committed.png | 5b2be8c输出，分别665×141、749×400、755×144 |
| a/b/c-flag-preserved.png | 当前HEAD+未提交flag修复；逐像素与geometry-committed完全相同 |
| *-resources\manifest.json、assets | 每次导出的独立资源与manifest；flag-preserved三份均有use_gdip=1 |
| a/b/c-sogou-5293-full.png、sogou-5293.png | 新target原屏幕/按HWND裁剪 |
| a/b/c-top-aligned.png | 已拒绝CENTER→NEAR试验 |
| a/b/c-gdi-mask-1.png | 已隔离GDI位图试验，非当前产品 |
| gdi-mask-experiment.patch | 撤回的完整UI试验diff，7357bytes；仅供审查，不自动git apply |
| a/b/c-font-probe*.png | 已删除CLI产生的历史字体诊断，非搜狗证明 |
| sogou-font-settings.png、sogou-layout-settings.png | 历史设置截图，不是当前机器可读状态 |
| run.json | 累积执行记录及关键trace摘录；有过时字段，按下述优先级解释 |
| paused.json | 较早暂停快照，当前HEAD/未提交文件等已过时 |

run.json有效新节：
ssf_text_flag_recovery、gdi_mask_experiment、bounded_5293_text_trace、top_alignment_experiment、resume_20260914内的新5293捕获。
但resume中的latest_worktree_state/额度26%以及末尾installer RUNNING_AT_CHECKPOINT等旧字段仍过时。**当前状态以本文实时核对为准，不用旧字段触发动作。**

部分完整Frida输出仅在本聊天工具记录；run.json保存有限成功摘录，不是完整原始trace。
Node REPL全局、functions store、旧PID都不是跨任务持久状态；不可依赖。

旧证据资料源：
D:\Projects\Ziliu-IME-clean-base-001\build\test-artifacts\three-skin-geometry-20260912T201600Z
来自旧eff997ee+未提交实验/Stub少候选，不是当前基线。B/C旧颜色ROI有装饰/emoji污染，不能搬旧通过数。

## 7. 当前参数真账

bbox为[left,top,right,bottom)，仅限已选ROI内灰度/色度像素阈值估计，不是全字形认证。

| 样本 | 新参考preedit | 当前字流preedit | 新参考首候选含编号 | 当前字流首候选 |
|---|---|---|---|---|
| A | [35,77,90,92] | [35,82,95,95] | [31,109,80,127] | [32,114,79,132] |
| B | [141,165,189,181] | [141,166,185,181] | [141,211,189,227] | [141,217,187,234] |
| C | [33,69,89,88] | [33,71,87,89] | [32,105,90,125] | [32,109,88,129] |

- 框高目前141/400/144，与参考一致；修复前163/283/184。
- 当前首候选ink顶相对参考偏低5/6/4px，完整验收0/3。
- B preedit ROI底边必须小于等于184 exclusive；扩大到185及以后会包含装饰。早期宽ROI读到边界的结果已拒绝。
- A/C装饰/框线色可能进入“首候选颜色”阈值，不能不检查ROI就自动认数。
- 忽略总宽度并不证明所有候选内容差异都无影响：emoji/fallback对行高是否有影响仍是待证假设。

## 8. 关键失败尝试及正面取证

### 8.1 仅取消候选垂直居中：已撤回

H1 candidate_format CENTER→NEAR，其余不动。Release/12tests通过。
首候选bbox变为A[32,106,79,124]、B[141,209,187,225]、C[32,103,88,123]，偏高3/2/2px。
结论：居中贡献位移，但不是全部原因。没有追加固定+2px。产品及测试断言都已恢复；当前build也已重建。

本地API事实（不是搜狗policy）：
- 20px Consolas：DWrite line height23.418/baseline18.398；GDI height23/ascent18。
- 20px Kingnam：DWrite20.260/baseline16.180；GDI19/ascent15。
- 字号不是行高；正负LOGFONT高度含义不同，不得混用。

### 8.2 新5293 B皮肤：有正面GDI→AlphaBlend证据

精确目标Notepad PID2172，先核SogouExe/TIP hashes和字体，14秒有界hook，record cap48。
READY guest epoch ms1789329615024；
真实输入host epoch ms1789329616284～1789329616565；
finally detached guest epoch seconds1789329629.0298474。
实际事件落在输入期间，因此不是上次那种时序未确认的空probe。

代表记录（完整摘录在run.json）：

- ExtTextOutW文本“ni”、face Kingnam Maiyuan、height/ascent/descent=19/15/4、flags4096、DC0x59010bcf、局部(0,0)；
  同source DC AlphaBlend目标(140,165)、大小15×19。
- “1.”在DC0x3b010a7a绘制后，AlphaBlend到(140,210)，大小12×19。
- “你好 ”（带尾空格）在DC0x3c010a7a绘制后，AlphaBlend到(154,210)，大小45×19。
- 另一个中间输入ni'ha阶段，编号AlphaBlend y为207，不是210。

已证实：所测路径把文字画到GDI内存表面后合成；编号与候选词分开；不是只有无落点的度量调用。
未证实：最终完整nihao全量记录（cap在中途达到）、所有字体LOGFONT/advances、跨皮肤通用baseline、为何y207/210变化、全部fallback/字形行为。
不能将上述坐标、两run差值或某个字号常量原样硬编码进renderer。

Frida已存在：17.18.0 wheel SHA 4bcf171a0ae184e30e95f414ce3a8f5e92e75e5c8c04ce36cea5eafe632070b1，
Guest隔离目录trace-deps；未装进CUA venv、无daemon。每次finally detach。
旧LOGFONT(-20,weight400,charset1,quality5)、dx[7,5]/[18,18,9]记录存在，但缺连续版本校验，不能当5293全量参数证明。

失败调用也要如实保留：
- 一次JS缺大括号，create_script失败，未安装hook，finally detach。
- Node语法预检new Function被运行时禁止，未绕过/修改运行时；后来修正脚本直接在已授权guest Frida执行。
- 较早DirectWrite hook报READY但没记录，输入/hook窗口未严密确认，不能证明不用DirectWrite。
- 旧GDI+/GetGlyphOutline无记录也不是“不使用”证据；不要继续盲扫所有API。
- 旧过滤return-address只匹配Sogou漏掉gdi32full转发。
- 一次precheck失败后，编排仍无条件启动Notepad；已承认错误。以后先检查内层return_code再发依赖动作。

### 8.3 GDI位图产品试验：保留patch，已全部撤回

位置：gdi-mask-experiment.patch；仅改candidate_window.cpp，约99行。
只在source_format=Sogou、H1、use_gdip=1且无用户字体/字号覆盖时尝试：

- CreateFontW负字号、内存DC/DIB、白字mask、premultiplied BGRA，再用D2D合成。
- 候选仍拼成同一label字符串，沿用旧layout origin/宽度计算；并未实现搜狗的独立run布局。
- 质量/alpha推导存在试验假设，不能凭API名称相同就认作搜狗实现。
- 初次编译因optional unsigned与1比较触发/WX，改1U后Release/12tests通过。
- B/C preedit bbox对齐，A仍偏低/过宽；首候选A[32,105,79,123]、B[141,208,187,224]、C[32,102,89,122]，均未通过。
- 已用apply_patch逆转整个UI试验；没有修改/回滚其他源码。所有当前产品绘制保持5b2be8c。
- 不自动重放此patch。若再次尝试，先形成明确的新证据假设，并检查资源清理、failure path、裁剪、fallback、cache/performance和图形质量；不能把试验函数直接当发布代码。

## 9. 构建、测试与诊断导出

最近最终构建：当前HEAD+五文件flag修复，Release x64 PASS，CTest12/12 PASS（13.68s）。
最后测试日志：
D:\Projects\Ziliu-IME-host-recovery-20260913\build\local-x64-Release\Testing\Temporary\LastTest.log
本次只读核对其12个Test Passed记录，修改时间2026-09-14 05:50:07+09:00。

警告：同目录LastTestsFailed.log仍残留01:23:31的“3:ziliu_ui_preview_window_tests”，比最终日志早；不是最新测试失败。本次没有删日志或重跑测试。

恢复授权后正常构建命令（cwd必须正确）：

    rtk proxy C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -Command '$env:ZILIU_DEPENDENCY_ROOT="D:\Projects\Ziliu-IME"; & ".\scripts\build-local.cmd" Release'

依赖根仅用现有依赖；不在原树编译/写third_party，不下载。NMake需要VS环境，不用裸cmake假设环境齐备。不要新建build helper。
当前不是旧salvage的RIME=OFF/7tests状态；本地已有真实Rime依赖，当前CTest包含Rime engine测试。不要套用旧工作卡。

已有诊断导出：

    build\local-x64-Release\bin\ziliu_ui_preview_window_tests.exe --capture-ssf-h1 <source-directory> <fresh-output.png>

- UTF-8 ini、原PNG，选General/Display/H1，经当前converter和真实CandidateWindow。
- 新输出和同名-stem-resources目录必须不存在。
- 私有非输入桌面，固定96DPI，preedit ni'hao，七候选：你好、你是、不好、拟好、你还、拨号、你。
- 不是真实Rime/TSF输入、不是Settings import gate。
- 无参数执行仍做预览父子关系、移动/裁剪/销毁、私有桌面动画和位置回归。
- 三份flag-preserved manifest均有use_gdip=1；对应PNG逐像素等于geometry-committed，证明本次flag修复未改变renderer输出。
- 不把这些结果升级为VM/功能/皮肤验收。

## 10. 已有VM与控制路径（仅新授权后）

当前测试VM：
- name ziliu-compat-20260913
- ID C35593B3-6EFD-4652-8C35-50C1D2DE6355
- hostname USERCUA-NQG4SL2
- User交互桌面，既有CUA管理员token曾验证。
- 最后endpoint http://172.20.54.221:8000，绑定到精确VM NIC已核对；恢复后不能假定IP不变。
- 最后正常shutdown /s /t 0返回0，CIM EnabledState=3确认；flag修复/本次交接没再启动。
- 所有过去PID/HWND过期；不复用它们进行hook或操作。

Guest已有目录：
C:\ZiliuCompat\SsfGeometry20260914
内容包括sogou-4385.exe、skin-a/b/c.ssf、KingnamMaiyuan.otf、frida.whl、trace-deps。
旧Guest字流bundle：C:\ZiliuCompat\settings-fix-1，未部署当前皮肤实验。

旧Oracle：
ziliu-sogou-4385-oracle-20260811 / DF75AE63-57F1-49EA-B4D6-1092B0C32C63
此前Off；192.168.254.2:8000超时。不要再修、重建、启动或追查全套基础设施；其他VM不在范围。

工具约束：

- 所有shell用rtk前缀；读C:\Users\Matsu\.codex\RTK.md。
- PowerShell用5.1，-NoProfile -NonInteractive -WindowStyle Hidden。不得传当前策略禁止的sandbox_permissions。
- 原项目/当前目录有CodeGraph索引：先explore/node，不重新index；结构编辑后sync。
- GUI前读取当时computer-use SKILL.md、guidance、confirmations。当前技能路径可能升级，以当前目录为准。
- 本阶段用Node REPL做Guest CUA HTTP操作；不调用host sky的截图/键鼠。不能用shell UIAutomation绕过技能。
- 既有CUA GET /status、/commands；POST /cmd JSON command/params，SSE data响应，截图image_data为base64。
- 已验证GUI命令：screenshot(format)、press_key(key)、hotkey(keys)、open(target)、launch(app)、left_click/right_click(x,y)、scroll_down(clicks)。
- 输入拼音必须逐键，不用type_text；观察→动作→刷新；界面有延迟，不在旧截图上重复点击。
- 不用Win键/Run/终端GUI，不自动登录或处理安全/UAC界面。
- HWND树不等于完整UIA候选文字；get_application_windows曾空、十进制HWND的get_window_position曾失败。不要因此修wrapper。
- Guest只读WindowFromPoint需基于刚观察的点，再GetWindowRect/class/PID/DPI。
- copy_to_clipboard是读取，不是自动按Ctrl+C。

Host Python：
C:\Users\Matsu\.cua\ziliu-ime\.venv\Scripts\python.exe -X utf8 -B
Guest Python：
C:\Users\User\cua-server\.venv\Scripts\python.exe -X utf8

既有backend：

    from cua_sandbox.builder.executor import LayerExecutor
    c = LayerExecutor('http://172.20.54.221:8000')
    # await c.run_command(command, timeout=10)
    # await c.write_file(guest_path, base64_contents, timeout=20)

success=true或外层进程exit0不代表guest命令成功；检查内层return_code和stderr后再继续。
Python短命令用base64传入避免Windows引号；raw字符串不能以单个反斜线结尾。不给日志打印完整base64/长traceback。
Host venv无numpy/frida/psutil；Guest win32ui DLL曾导入失败。不要安装/修补，已有Pillow/ctypes够用的地方用它们。
WinPS Get-FileHash/Get-Acl模块曾失败，必要时用.NET；UTF-8 JSON显式UTF-8读，不因mojibake误报内容损坏。

只读VM查询（新授权或必要cleanup）：

    Get-CimInstance -Namespace root/virtualization/v2 -ClassName Msvm_ComputerSystem -Filter "Name='C35593B3-6EFD-4652-8C35-50C1D2DE6355'"

EnabledState2=Running，3=Off；不用Get-VM做discovery。
NIC类Msvm_GuestNetworkAdapterConfiguration，InstanceID LIKE '%C35593B3-6EFD-4652-8C35-50C1D2DE6355%'。
已有启动路径为精确对象RequestStateChange RequestedState=[uint16]2，返回4096表示异步，不代表已就绪；本文不授权调用。
关机用Guest现有backend shutdown.exe /s /t 0，再精确只读确认Off，不操作别的VM。

## 11. GitHub调研已经做过，不重复从零搜索

未移植第三方源码；这些项目有SSF支持，不等于证明精确几何一致。

- Flygeon/9IME，f4da0404b1e1edce9c9f4e1ad00183d96df6b476：
  https://github.com/Flygeon/9IME
  crates/9ime-core/src/skin.rs、crates/9ime-server/src/window.rs。GDI/九宫格可参考；其最小边距、96/72字号转换、padding/fallback是自身选择，不能盲认搜狗规范。
- KDE/KIMToy，c8b3da65bfd289d0a0262aa673aa6b697022d4a3：
  https://github.com/KDE/kimtoy/blob/c8b3da65bfd289d0a0262aa673aa6b697022d4a3/themer_sogou.cpp
  另有themer_sogou.h、skinpixmap.cpp、kssf.cpp；存在FIXME/假设，不是官方规范。
- fkxxyz/ssfconv，4202290eda8835c36756595b7bae3c268d6804e3：
  https://github.com/fkxxyz/ssfconv
  参考格式/边距，作者承认字体偏差；也看过VOID001/ssf2fcitx、RadND/ssfconv。
- 9IME/ssfconv声明GPL-3.0；KIMToy文件许可证需逐个核对。未来实际移植必须重新核对归属和许可证，不能凭调研摘要直接复制。

## 12. 恢复后最小下一步（不构成授权）

1. 等用户明确继续。只读核branch/HEAD/五文件diff和官方额度，确认没有其他人修改；不重复全项目事故审计。
2. 先审核/固化独立use_gdip修复。它已经本地通过，但尚未commit；不要把撤回的GDI试验一起提交。
3. 剩余直接问题是文字run与行基线，不是VM：对现有B正面trace形成一个最小问题——最终稳定nihao时，各run的字体、尺寸、advance和合成落点是什么？为何中途y207/210变动？
4. 若取证确实必要，复用唯一compat VM及既有工具；只限定相关API/精确目标/字段/时间/记录上限并保证输入发生在hook活动期。先核5293与字体，再操作。不要广扫图形API或增加基础设施。
5. A的英文Consolas与B/C中文字体路径不同。需要分别证明格式字段到实际字体/字号/原点的规则；不能把Arial、font_size-1/-4、+2px等旧猜测再固化。
6. 调整renderer时先测量再绘制，编号/候选词分run、处理尾空格和fallback。当前GDI试验复用了旧layout且合并run，是未过原因之一；完整原因仍未证明。
7. 先验证三个代表样本的具体参数与native默认回归，再考虑真实SSF导入链；不得重写Settings/整个renderer以追求一次性完成。
8. 完成AllSkin/active身份、settings、版本/字体、同次capture绑定。不同文字宽度不比较，但是否改变行高须检验。
9. 真实SSF ZIP导入/包ID冲突仍是后续独立缺口；不得宣称私有桌面导出已解决。
10. 所有关键参数达标后才交六张最终图。未达标则明确未过，不调整容差或把实验导出冒充真实输入。

## 13. 一句话接力摘要

**主机80d37574基线未改；开发HEAD5b2be8c已修框高/边距，未提交五文件+59行保留use_gdip并通过12项本地测试；两个绘制试验已撤回，GDI→AlphaBlend只在所测B路径有正面证据；三皮肤仍0/3，VM最后确认Off；现在交接PAUSED，等用户新授权。**
