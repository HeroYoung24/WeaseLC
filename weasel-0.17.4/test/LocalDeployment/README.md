# 本机独立测试

## 2026-10-28：高级功能源码与检查

本轮七项源码已补齐，尚未部署。Word仍只普通输入；4B配置冻结。实现和下一次部署顺序见[`docs/ADVANCED_FEATURES_IMPLEMENTATION_2026-10-28.md`](../../docs/ADVANCED_FEATURES_IMPLEMENTATION_2026-10-28.md)。

构建后可运行`Test-AdvancedFeatures.ps1 -Architecture x64|Win32`，顺序检查自有UI/TSF/临时注册表/进程，不安装输入法；架构间不要并发占用同名测试管道。临时注册表检查需允许测试执行权限。

真实模型启停另用`Test-ManagedLocalModel.ps1`：拒绝已有运行时、保留原日志，显式选用调用方已有PowerShell，只运行原脚本的合成预热，完成后验证模型与ready/端口退出。普通产品窗口允许选择`pwsh.exe`，不依赖开发环境固定路径；需使用支持`StopOnInputClose`的本轮启动脚本。没有自动下载或更改用户执行策略。

最近实际安装为e5ce10d，Word普通输入及Edge验收已通过；高级功能尚未部署。纠错关闭，模型已停止。下面的旧安装版本、Word兼容方案和等待反馈状态均为历史，不作为当前执行依据。

## 历史执行记录

**2026-10-27当前决策：Word只作为普通输入法使用。** 新源码在读取/缓存/模型/提示/纠错Tab前跳过Word，原本机兼容选项已退役；旧包UpdatePatch-20261027-150517被superseded标记及更新器拒绝，不能继续安装该方案。当前安装仍f5f11f1/Off。本轮新产物下一步仅验Word普通输入，重点转Edge撤销/重做、密码/只读和跨框失效。测试页已补事件摘要，真实DOM行为仍待验收。[最新报告](../../docs/WORD_INPUT_ONLY_AND_EDGE_HISTORY_2026-10-27.md)。以下Word兼容路线为历史。

**2026-10-27最新源码：** Word特定Permission getter异常已分阶段确认，新增默认关闭、需明确选择的本机自有提交兼容模式。修复f8f22e6、包`UpdatePatch-20261027-150517`只读校验通过，未安装；实际安装仍f5f11f1，纠错Off。下一轮必须成套更新六文件（协调器协议v3），先Word基础，再于合成文档明确开启兼容、先核对整句捕获再提示/Tab。设置v4回退需恢复升级前v3快照；快照和状态见`output/LocalTest/word-irm-compat-state-20261027.json`。[定位、边界与回归报告](../../docs/WORD_IRM_COMPATIBILITY_FIX_2026-10-27.md)。下文旧状态仅作历史。

**最新：接口对照已确认 GetValue 在三时机、四种范围均不可用，范围身份/锁/枚举正常；诊断版已回退。** 已实现 LocalCommit 专用的严格原生多行纯文本兼容验证，双架构各 50 策略/15 真实控件/81 TSF 对照通过，正式构建通过；新兼容版未部署。纠错关闭、专用进程退出、原版基线一致；已发现 ChatGPT/Explorer 占用旧 DLL，部分系统进程不可枚举。释放后使用新的兼容包，先采 NativePlainText、nativeFlags 和 LastCapture，再提示/Tab。[最新报告](../../docs/NATIVE_PLAINTEXT_COMPATIBILITY_2026-10-19.md)。以下为历史。

**当前：fe82f97 真实提示仍被范围 GetValue/E_FAIL 阻断，本轮已注销回退。** 基本输入正常；新增 ScopeTrace 接口对照版已构建但未部署，双架构各 81 接口/26 协调器检查通过。用户已关闭测试记事本，专用进程退出，原版基线一致，普通测试设置保留且纠错关闭；仅 ChatGPT 加载旧 DLL。恢复应释放模块后清理/安装新接口对照包，先比较范围身份、锁、结束组合前后/只读 cookie，不重复旧整句提示测试。[专项调查](../../docs/SCOPE_EFAIL_INVESTIGATION_2026-10-19.md)。以下为历史。

**2026-10-19 当前：重启清理后 fe82f97 已安装，待人工基本输入反馈。** 旧模块零占用，Purge/安装/注册回退演练通过，实际 200 文件校验通过、原版基线一致；双架构后端合成中文检查通过。测试后端/协调器已启动，测试输入项启用，自动纠错关闭。`WeaseLC-visual-acceptance-2026-10-19.txt` 已打开并启动阶段观察器；先人工基本输入，再固定提示/Tab，范围属性 E_FAIL 仍可能阻断。恢复会话见 `output/LocalTest/broker-acceptance-active-2026-10-19.json`。以下均为历史记录。

**2026-10-19 最新：新显示修复包 fe82f97 已准备，等待释放旧 DLL 后重装。** 双架构正式构建、200 文件哈希通过；旧独立 COM/TIP 已注销、测试后端/协调器已停止，原版基线一致。旧 DLL 仍被 ChatGPT/Explorer/Edge/QQ 占用，未替换或强制卸载；旧程序与关闭状态的普通用户设置保留。用户释放后先重新扫描占用，再 Purge、安装、Start、基本输入与固定提示/Tab 测试。新修复尚未实际部署，范围 GetValue/E_FAIL 仍是独立阻断。恢复信息见 `output/LocalTest/broker-acceptance-active-2026-10-19.json`；[显示专项报告](../../docs/AUTOMATIC_HINT_VISUAL_FIX_2026-10-19.md)。以下为历史记录。

**当前：用户已退出测试，安装已回退；新配置/诊断/异步纠错通道已实现并通过定向回归。** 原版基线一致，独立注册/普通设置/词库清理，无测试后端/模型；安装目录唯一旧 DLL 被 ChatGPT/Explorer 占用，未强制删除。正式链路改为由普通后端管理的独立协调进程，普通/MSIX 八项对照与双架构各 203 自动回归通过。真实记事本候选/Tab 尚未验收，释放并 Purge 后必须使用新包再测。见 [最新修复报告](../../docs/AUTOMATIC_HINT_BROKER_FIX_2026-10-19.md)。以下旧安装状态仅作历史记录。

**最新根因方向：已复现普通环境与记事本 MSIX 包环境看不到同一纠错设置。** 配置缺失导致默认关闭；入口记录同样依赖宿主视图，不能从外部空日志断言没有回调。下一步先实现共享配置/诊断/推理协调，再做提示实测；见 [三层重新审查](../../docs/AUTOMATIC_HINT_THREE_LAYER_REVIEW_2026-10-19.md)。当前独立测试版仍安装，自动纠错已关闭；下面“已回退”属于此前阶段。

`Read-Diagnostics.py --output <工作区 output/LocalTest 下的新文件>` 只读取本项目开关和定长元数据；使用微软 `Invoke-CommandInDesktopPackage` 在指定包环境运行，可复现配置视图差异。输出不含正文、保留词或凭据；不会修改注册表。

**最新：固定响应包 `d4a9f92` 实机仍无提示。** 已补按键/生产提交/Capture 入口诊断及半角句号夹具兼容，新包待释放旧 DLL 后安装；不能认定核心故障已修复。用户已关闭测试窗口，测试注册/设置/词库回退，专用后端停止，旧 DLL 仅被当前 ChatGPT 占用。以 [入口诊断报告](../../docs/AUTOMATIC_HINT_ENTRY_2026-10-19.md) 为准。

**2026-10-19 最新：已确认记事本的显式 IS_DEFAULT 被原白名单拒绝，兼容修复及双架构回归通过；新包尚未安装。** 固定响应和无正文管线诊断已就绪，先固定响应验收提示/Tab，再接真实模型。旧测试注册/设置/词库已回退，仅旧 DLL 被当前 ChatGPT 占用待释放。实际结果、诊断字段和恢复顺序以 [本轮报告](../../docs/AUTOMATIC_HINT_PIPELINE_2026-10-19.md) 为准。下文早期状态保留作历史参考。

> 后续审查已修复无建议后丢失上文，173 项自动回归及 24 项真实本地模型链路通过（每架构）；实机范围阻断仍待 LastScope 确认，参见 [最新审查和恢复步骤](../../docs/CONTEXT_REVIEW_AND_TESTS.md)。新包尚未安装，前轮 DLL 需释放后精确 Purge。

2026-10-18 最新复测：重启后重建独立版，用户确认中文及中断恢复正常；自动提示仍缺失，前台诊断已定位 ScopeUnavailable（焦点 Ready），调用模型前被阻断。已补充细分范围诊断但尚未重新部署。测试已回退，DLL 被 ChatGPT/Explorer/Edge 加载待释放；详见 [最新范围复测](../../docs/AUTOMATIC_HINT_SCOPE_TEST_2026-10-18.md)。

适用本次用户明确授权的日常电脑测试。先读 [风险与验收范围](../../docs/LOCAL_DEPLOYMENT_RISK.md)。虚拟机包仍用于干净客体，不直接用于本机覆盖安装。

1. MSBuild Release x64 与 Win32：`weasel.sln /t:WeaselTSF,WeaselServer,WeaselDeployer /p:WEASELC_LOCAL_TEST_BUILD=1 /p:WEASEL_CORRECTION_PROBE_BUILD=0`；另构建 Release x64 `test\LocalDeployment\ProfileCheck.vcxproj`。两个宏不可同时启用。默认普通构建未改变。
2. `Prepare.ps1` 将独立输出、对应 rime.dll 与公开 data 复制到新的 `output\LocalTest\Deployment`，生成逐文件哈希。不会重新使用已有暂存目录。
3. UAC 下执行 `Install.ps1 -Action Install -ValidateRoundTrip`；或用 `Run-Elevated.ps1` 记录结果。只写入新的 Program Files 目录、新的两架构 COM/TIP 和机器设置根。简体 Profile 的机器默认可用属性为启用，不设为默认输入法；独立进程核验启用状态。遇到已有本机测试状态即退出，避免原位更新已加载 DLL。早期已安装但不可见的版本可用 `Install.ps1 -Action EnableProfile` 修复其注册元数据，无需替换 DLL。
4. 普通用户执行 `Start.ps1` 或双击 `Start.cmd`：新建独立用户词库并部署简体拼音，启动独立后端。单独验证后可用 `Start.ps1 -EnableInputMethod` 添加当前用户输入项；此选项会重启自己的后端，并要求先构建 x64 `ProfileCheck.vcxproj`。辅助工具只启用本用户新 Profile、通知设置变化，不激活 TIP 或更改默认项。配置查询成功不代表切换界面可见。
5. 在新记事本中选择“WeaseLC 本机测试”，检验 `nihao`、选词、Shift、空格、Enter、Esc、切换回搜狗。纠错仍默认关闭；基本输入通过后再单独启用本地模型和自动纠错。请勿在未验收阶段编辑重要文档。
6. `Rollback.ps1` 先移除当前用户输入项并停止自己的后端，再请求 UAC 注销新身份。结果见 `output\LocalTest\elevated-Unregister.json`。程序文件和独立词库保留；不杀死宿主进程、不安排重启删除。

`BackendCheck.vcxproj` 的两架构只向独立管道发送固定合成 `nihao ` 并比对提交 `你好`；不激活 TSF、不注入键盘。类工厂检查使用原 `ProbeActivation --class-factory/--load-only`，DLL 为独立正式代码构建，且无探测导出。

当前状态：已安装及演练注册回退，后端合成转换通过；修复 Profile 机器可用属性后，用户确认列表可见、中文输入和搜狗正常。自动纠错/多应用/Tab 与鼠标边界仍待本机验收。以后重启用 `Start.cmd` 启动独立后端，无自启动。

上段为早期部署状态，当前以顶部实测记录为准。启用工具的写入动作以 API HRESULT 返回并通知设置变化；最终安装状态由独立新进程读取确认，最多三次有界验证，不用写入进程的缓存枚举作成功门槛。子进程 stdout/stderr 进入部署转录日志，函数返回值仍是单个退出码。
