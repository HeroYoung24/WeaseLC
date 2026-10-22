# 纠错接口离线检查

2026-10-22：质量工具新增测试专用wide-015、wide-035、repeat-light、thinking-16参数，报告含实际请求参数；显式profile和可选uint32种子位于原质量命令末尾。`EvaluateTuning.py --round-two`使用[第二轮计划](../../docs/MODEL_TUNING_PLAN_2026-10-22.md)的固定进入门槛，最终只接受tuning-final，不允许更换提示。9组630请求均拒绝，生产默认不变，详见[结果](../../docs/MODEL_TUNING_REPORT_2026-10-22.md)。

2026-10-20：默认离线检查已为每架构130项。新增仅合成语料的质量排查工具：

`CorrectionClient.exe --quality <回环chat/completions地址> <模型名> <提示方案> <语料组> <新JSON文件绝对路径>`。

提示方案为baseline（冻结旧生产提示）、current（当前生产）、phonetic/verify/minimal（未采纳的语义试验）、contract（输出契约澄清）；语料组为development、validation、diagnostic或contract-audit。只处理编译样例，输出必须在本项目output/LocalTest且不得覆盖旧文件。实际0.90门槛不变；零分重解析仅为同一响应的诊断，保护规则仍生效。

退出0表示该组严格质量通过，1表示仍有质量/协议失败，2表示调用或工具错误。`CompareQuality.py --pair <原版JSON> <候选JSON> [--pair ...] --output <新判定JSON>` 另做逐例相对回归比较；相对通过不代表严格质量通过。详见 [质量排查报告](../../docs/MODEL_QUALITY_INVESTIGATION_2026-10-20.md)。不修改用户配置、输入法注册或安装，也不提供任意文本输入。

`CorrectionClient.vcxproj` 支持 Release x64/Win32，编译生产模型/设置编解码代码，注入内存模拟传输。现有 110 项离线检查覆盖回环与 HTTPS 授权约束、关闭/密码/取消不发送、版本/目标匹配、UTF-16/转义、分数过滤、异常响应、请求/响应大小、传输错误、配置不保存密钥/授权、响应单次消费、请求去抖/取消/失效、代码/网址保护，以及建议操作/显式保留词的界限。默认不调用模型、CLI、网络或系统注册，不读取用户正文，不保存设置。

`--storage-checks`：7 项真实临时注册表读写检查，只使用新建 `HKCU\Software\WeaseLC-Lexicon-Test-<进程号>`，验证重开读取、非法保存不覆盖、损坏值拒绝和清空，结束后删除临时键。不写真实保留词。

真实 HTTP 边界：先使用 Python 启动 `http_fixture.py 18962`（只监听回环），再运行 `CorrectionClient.exe --transport-checks http://127.0.0.1:18962`。9 项检查覆盖普通/分块响应、重定向拒绝、长度/流式上限、提前断开、主动取消、总截止时间及连续取消后恢复。测试完成后停止自己的 Python 进程。

管理窗口另有 `WeaselDeployer.exe /correction-selftest`，创建隐藏的真实资源控件，检查三个语言资源、生产后台执行/轮询、模拟结果显示、真实编辑控件通知取消、过期结果丢弃、恢复默认及关闭。不调用网络或保存设置，当前工作目录只写 `CorrectionDialogChecks.log`（检查名、布尔结果与语言编号）。

另可用 `/correction-demo` 查看可见的固定响应演示；标题和状态明确标记模拟，不调用真实模型，关闭不保存设置。`/correction` 是实际配置界面，手动检查时会使用已配置的 HTTP 服务。Codex CLI 入口不开放。

详细构建命令、协议与验收限制见 [`docs/CORRECTION_MODEL_INTERFACE.md`](../../docs/CORRECTION_MODEL_INTERFACE.md)。

`/correction-local-selftest` 为显式真实模型诊断：只连接 `127.0.0.1:18963` 的 `weasel-qwen3-4b`，向真实 GUI 后台路径提交内置合成文本；7 项验证结果显示、应用、撤销、内存保留词后的过滤和关闭取消。隐藏运行，不保存设置或真实词表，写 `CorrectionLocalDialogChecks.log`。先按 [本地部署记录](../../docs/LOCAL_MODEL_TESTS.md) 启动模型，使用合成语料才运行本开关。

`/rime-learning-selftest`：在源码当前目录下启动，只用 `output/RimeLearningTests` 内全新的微型词库；两个 translator 各 12 项检查，验证选择提交 5 次后的持久词频和候选排名，不连接日常服务或真实词库，写 `RimeLearningChecks.log`。详见 [纠错操作与用户习惯](../../docs/CORRECTION_REVIEW_AND_LEARNING.md)。GUI 离线自检另生成两个自有隐藏窗口的 BMP 预览，收尾删除，不捕获桌面。
