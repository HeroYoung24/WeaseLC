# 原生模型窗口与行为记录验收（2026-10-29）

## 已通过

安装版本13ee957。此前Edge基础、A/B/C Fixed、暂停/恢复及恢复后新提示/Tab已人工通过，详见[暂停读取修复](APP_PAUSE_REGISTRY_READ_FIX_2026-10-29.md)。暂停最终仅保留原chatgpt.exe记录，Edge未暂停。

用户从安装版“本地模型运行”窗口选择既有模型目录及现有PowerShell7并启动，明确反馈“已就绪”和本机18963接口。随后核对冻结身份4b、alias weasel-qwen3-4b、thinkingBudget0、ctx8192、权重SHA256、实际llama-server映像及health200均符合；已保存PowerShell选择。未下载模型或修改生产推理参数。本轮尚未验窗口关闭的自有进程退出。

切真实Model（Fixed0）后，用户确认Edge A区真实纠错、Tab改文/拼音保留/下一次选词、无错句输入退格及鼠标中断恢复三项均正常。读取实际BehaviorHistoryV1：仅1组、96字节，accepted=1、ignored=0、rejected=0。固定测试此前没有生成行为历史，无错句/鼠标中断没有误增负反馈。诊断中另有取消请求，不称所有请求成功；不把回调快照数当作接受次数或一次原子写入数，也不是端到端延迟基准。

证据`output/LocalTest/NativeModelUi-20261029-162006/real-a-evidence.json`和`behavior-after-real-a.bin`；旧模型stdout/stderr已先备份在同目录。状态/窗口PID见`output/LocalTest/advanced-acceptance-state-20261029.json`。

## 当前进行中

为区分普通Esc和取消拼音的Esc，测试脚本只将既有提示时机从0临时改为1（检测后立即提示），原设置已备份`settings-before-immediate.bin`，没有改模型/阈值。这不是界面选择提示时机的人工验收。

等待用户在A区三轮合成文字测试：无拼音时Esc应拒绝1次；实际可见约2秒后继续相邻提交应忽略1次；有活动拼音时Esc取消拼音应不记拒绝。本轮不按Tab，预期总计accepted1/ignored1/rejected1，以后续真实计数为准，尚未通过。

模型由原生窗口拥有，用户保持窗口最小化，不得在等待反馈时提前关闭、停止模型或切Off。Edge页面及后端保持。完成后还需验学习关闭/清除、恢复原提示时机、原生窗口关闭退出及更多高级功能；当前不声称全项完成。
