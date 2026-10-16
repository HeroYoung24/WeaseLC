# 本地模型测试与吞键故障：暂停交接（2026-10-16）

## 当前结论与优先级

按用户“时间紧张，快速收尾”要求暂停。**严重吞键故障尚未修复；当前版本不能作为稳定日用版本。** 已保存失败回归和排查依据，不再安装或激活测试输入法。本记录优先于此前“检查通过”的阶段性描述；独立宿主检查不能替代真实应用验收。

## 本次真实测试

- 使用本地 Qwen3 4B Q4_K_M、llama.cpp b11640 Vulkan，回环地址 `127.0.0.1:18963`，8192 上下文、单工作槽、关闭思考；未向在线服务发送文本。
- 启动及预热约 3043ms。管理纠错窗口手动检查合成文本“这里有错子。”，得到“错子 → 错字”、95/100、界面显示 922ms；“应用到本页”后为“这里有错字。”。未验证撤销。
- 自动提示、Tab 开关开启；右侧、间距 8、宽 320、字号 14、去抖 250ms、默认阈值 0.90。
- 用户在记事本实际拼音输入后，中文正常，但**没有自动提示**；Tab 替换未验收。
- 用户随后报告：无纠错提示或鼠标点击空白中断输入后，下一次输入/退格失效，必须切换输入法才能恢复。按严重输入故障处理。
- 自动提示缺失原因未证实。输入范围隐私门控可能跳过部分宿主，但不能据此放宽隐私保护；后续应补充不含正文的跳过原因诊断。

## 已复现与代码状态

本轮只增加探测版测试钩子、失败回归和卸载清理脚本，**未实施生产逻辑修复**。探测钩子受 `WEASEL_CORRECTION_PROBE_BUILD` 条件编译保护。

在未注册、未激活的 x64 探测 DLL 中运行 `ProbeActivation --composition-checks`：原有组合/绑定/文本检查通过；新增中断检查 **19 项、9 项失败**，DLL 实际卸载成功。日志保留于 `output/swallow-before-x64.log`（被 Git 忽略；关键结果已记录在此）。新增 Win32 回归尚未运行。

失败断言分布：

| 场景 | 失败项 |
| --- | --- |
| 失焦 | 未立即脱离旧组合；排队清理被代数失效，旧组合未结束 |
| 未激活对象的 Deactivate | 早返回留下旧组合 |
| 宿主 OnCompositionTerminated | 旧组合未脱离；旧写操作未失效；旧组合清理不正确 |
| 遗失按键查询配对 | 下一次退格、不同按键、另一上下文按键均可能被错误消费 |

这证明存在与用户现象一致的代码缺陷；尚不能把整个记事本事件归因于单一原因。

### 优先排查位置

1. `WeaselTSF/KeyEventSink.cpp`：`_fTestKeyDownPending` / `_fTestKeyUpPending` 仅布尔锁存。遗失实际按键回调后，下一事件可能被当作旧事件消费，未比较按键、上下文和消息身份。新增 `TestInterruptedInput` 已复现。
2. `WeaselTSF/WeaselTSF.cpp`、`Composition.cpp`：失焦先 `_AbortComposition()` 排队结束，再 `_InvalidateEditSessions()`，会使清理任务自身过期；旧组合仍被前端持有。Deactivate 早返回也有残留路径。
3. `Composition.cpp::OnCompositionTerminated`：宿主已结束组合时再次 Abort/排队 End，未立即清理跟踪状态及旧写操作。
4. `TextEditSink.cpp::OnEndEdit`：光标移出组合后仅排队结束，需核对后端组合与前端状态是否同时复位。
5. `EditSession.h/.cpp`：组合写入失败后的恢复，以及正常 EndComposition 内联触发终止回调的重入保护，仍待修复/验证。

### 下次执行顺序

- [ ] 将按键查询去重绑定到具体上下文、按键及消息；仅匹配事件复用结果，中断时重置。
- [ ] 设计旧组合退出：立即脱离跟踪、失效旧写入；延迟清理仅结束捕获的旧组合，不能擦除宿主正文或影响下一组合。
- [ ] 宿主终止与主动正常结束分别处理，防止递归 End 或取消正常提交；补鼠标移出、重入、写入失败回归。
- [ ] 运行新增回归 x64/Win32，再运行原组合与自动提示回归；正式双架构构建，确认不带探测导出。失败回归当前预期为红，不能作为交付通过。
- [ ] 查明自动提示缺失原因，增加不含正文的诊断；修复后再约定真实应用验收，不因旧授权自动重装本机。
- [ ] 完成剩余 DLL 清理与清理状态复核；验证之后再整理临时构建产物。

源码根 `E:\WeaseLC\weasel-0.17.4`，仓库根 `E:\WeaseLC`；保持 `main`，用户授权本地提交，未授权自动推送。

探测复现命令（从源码根运行；需要先重建）：

```powershell
& .\output\ProbeDiagnostics\x64\ProbeActivation.exe .\output\Probe\v3\x64\weaselProbe.dll '{B749B45E-7DCE-4496-8414-9EE10970260A}' --composition-checks
```

MSBuild 为 `E:\Microsoft Visual Studio\MSBuild\Current\Bin\MSBuild.exe`，设置 `VCTargetsPath=E:\Microsoft Visual Studio\MSBuild\Microsoft\VC\v170\`。探测 TSF 使用 `weasel.sln /t:WeaselTSF /p:Configuration=Release /p:Platform=x64 /p:WEASEL_CORRECTION_PROBE_BUILD=1 /p:WEASELC_LOCAL_TEST_BUILD=0`；诊断工程为 `test/ProbeActivation/ProbeActivation.vcxproj`。Win32 对应替换平台。不要注册探测 DLL；测试钩子避免连接日常后端。

## 卸载与残留（不能声称已完全清理）

- 独立测试 COM/TIP、当前用户启用残留、测试设置已删除；专用词库与临时日志目录已删除。`output/LocalTest/purge-result.json` 记录 `registrationRemoved=true`、`settingsRemoved=true`、`dailyBaselineUnchanged=true`。
- 测试后端、模型服务及启动器已停止。暂停前进程快查仅见原版 WeaselServer PID 24436，路径 `C:\Program Files\Rime\weasel-0.17.0\WeaselServer.exe`；`local-model/ready.json` 不存在。最终 CIM 全量进程查询被环境拒绝，未据此声称完成新一轮系统全审计。
- **尚余 `C:\Program Files\WeaseLC-LocalTest\weaselx64.dll`**。此前模块枚举确认被 ChatGPT 5344、Explorer 6236、Notepad 6504 占用；暂停快查仍见该文件。卸载注册不会卸载已载入 DLL。没有强杀这些应用，也没有安排重启删除。
- 建议用户方便时重启 Windows，释放所有宿主；下次在管理员 PowerShell 执行 `test/LocalDeployment/Purge.ps1` 并核查结果，仅删除独立测试身份及固定目录，不操作原版/搜狗。
- 本地模型权重与便携运行时 `E:\WeaseLC\local-model` 保留为开发资产；工作区诊断构建/日志暂留便于复现，未声称本地无实验构建产物。
- 本次原版安装文件/注册基线比较未变；不能据此保证已加载测试 DLL 的进程完全不受暂态影响。若仍有输入异常，先切回日常输入法并重启。

## 恢复入口

先阅读本文件，再查看 `test/ProbeActivation/CompositionTests.h::TestInterruptedInput` 与上述源文件；从 19 项/9 失败基线继续，勿重做本机模型演示或把当前改动误认为故障修复。
