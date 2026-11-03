# 增强流程暂停与恢复入口（2026-11-03）

用户要求快速保存现场并暂停。以下为当前事实，覆盖此前“等待继续测试/保持后端运行”的阶段。

## 现场与版本

- 系统独立测试版51539147003cf7b0ae382eac645fdd33b8a2dced，包output/LocalTest/UpdatePatch-20261103-155422。x64 DLL SHA256为AA46B4CF5C47C48DA57B1C89A5DBE8C72999F84E3572C7ACE2292A0193C1C025。
- 适配已实际接入TSF/窗口/配置/协调器；双架构完整回归宿主836、客户端332通过。后续仅迁移脚本、只读诊断和迁移回归有修改，未更换系统DLL；客户端新增迁移检查后双架构334/0。
- 新版普通输入、鼠标中断恢复、Edge A/B/C及搜狗由用户确认正常。
- 当前纠错Off、Fixed0；增强偏好仍保存true，但随纠错关闭而不运行。行为学习Off、原1组记录保留。专用后端和最后一轮自己的诊断已正常停止；原小狼毫后端仍在。
- Edge测试页和所有用户应用保留，未卸载或强制释放DLL，无真实模型运行。下次不能假定DLL已释放。
- 精确阶段：output/LocalTest/advanced-flow-acceptance-20261103.json。

## 已定位并修复的首次无提示

开启测试的Set-LocalTestPhase.ps1从旧v3配置升至v5时，只补增强字段，却漏了v4起必需的word_local_compatibility=false。协调器拒绝整个设置，前端LastGate为QueryFailed/ERROR_NOT_READY，尚未进入新捕获或模型检查；与模型速度/Tab绘制无关。

脚本已补字段，无需重装或重启。原生只读BackendCheck --correction-settings <独立WeaselDeployer路径>显示：修复前Exchange本身成功但status=1(Unavailable)；修复后status=0，Fixed开启时enabled=1、advanced=1、minimum=3000、maximum=15000，20ms/500ms预算各三次均立即返回。证据output/FlowAdapter-20261103/settings-after-schema-fix.log及settings-fixed-confirmed.log。

新增诊断只在workspace的BackendCheck中；仅x64诊断路线实际运行。配置编解码新增缺字段拒绝/补齐后启用检查，双架构客户端334/0，日志*-schema-regression.log。生产GUI原EncodePreferences一直写完整v5，此次缺项来自测试开启脚本。

## 尚未解决：用户观察提示约3秒自然消失

修复配置后用户看到提示，但反馈“到了三秒自然消失”，并重申真实需求：多次短词确认后同一提示至少有3秒可见时间，另设最长时间；不是显示3秒即关闭。本轮是实际TSF控制器+固定测试回答，不是只跑计时单元测试。

**不得记为通过；Tab是否成功改文也未获确认。** 暂不继续队列、真实模型等后续实测。

现有代码没有在minimumVisibleMs满足后主动结束的规则。现场证据还不足以确认三秒消失来自哪条路径：

- 第一段Shown约08:09:13.56 UTC，之后没有新的“你好”提交捕获或TabApplied记录；Cancel可能不更新诊断，因此不能把残留Shown理解为窗口持续可见。
- 另一段Shown约08:09:37.56 UTC，Expired约08:09:52.15 UTC，间隔约14.6秒，显示最长年龄逻辑在该段约为15秒；不能用这一段否定用户另一段的三秒观察。
- 当前标记不记录取消调用来源。已阅读的可疑链路包括CandidateList.cpp::_HideUI无条件Cancel、Composition.cpp::_InvalidateEditSessions、焦点/布局/源验证变化，以及建议年龄从接纳而非首显起算。这些尚无本次直接因果证据，不能宣称已定位或盲目放宽保护。
- LastScope中的Word E_FAIL以及此前LastBroker记录有10月30日旧时间；本次必须按时间过滤。PowerShell ConvertFrom-Json会自动解析日期，恢复分析应使用-DateKind String或明确DateTime比较，不能直接对日期对象作字符串-like。

现场日志：output/FlowAdapter-20261103/fixed-schema-retry.stdout.log首行记录对应JSONL的完整路径；第一轮无提示日志入口fixed-acceptance-observer.stdout.log。不要按文件名猜最新或覆盖旧证据。

## 恢复顺序

1. 先读本页、恢复JSON和实际设置，确认纠错/学习关闭及用户页面仍在；不要擅自恢复学习或继续要求重复同一测试。
2. 明确保底3秒和最长年龄的定义，检查实际短词提交/候选生命周期与窗口隐藏原因；必要时补无正文的取消来源/可见性记录，再用对应TSF路径复现。
3. 当前尚未修改候选隐藏、焦点失效或取消原因代码。保持跨框、正文变化、密码/只读和Word普通输入保护，不为强行保留窗口放宽来源验证。
4. 有具体修复后再完整构建/回归、释放占用及成套更新，先做一项精准实测，确认短词保留与Tab后再扩展队列/长按/Esc/真实4B。

本轮测试配置备份在output/FlowAdapter-20261103/before-install-settings.json；整个FlowAdapter输出目录已加入gitignore，包含机器配置的备份和日志不提交。
