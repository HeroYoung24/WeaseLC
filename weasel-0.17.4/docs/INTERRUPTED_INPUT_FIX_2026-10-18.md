# 吞键修复与扩展离线测试（2026-10-18）

## 状态与边界

上次新增的 19 项中断回归从 **9 项失败变为全部通过**；代码修复和双架构离线回归已完成。本轮未安装、注册或激活测试输入法，没有注入系统按键/鼠标，也没有调用真实模型。**真实应用中的原故障尚未重新验收，不能据此宣布日用稳定。**

本记录接续 [10 月 16 日暂停交接](LOCAL_MODEL_TEST_HANDOFF_2026-10-16.md)。保留该记录作为失败基线，不覆盖历史事实。

## 修复内容

1. **按键配对**：用上下文、虚拟键、消息参数与消息时间匹配查询和实际事件，取代不区分事件的布尔锁存。重复查询仅复用匹配结果；缺失回调后，不同按键、退格、释放或另一文档的输入继续处理。中断时释放待配对状态和持有的上下文。
2. **组合退出**：失焦、停用、文档切换或光标移出组合时，立即脱离旧组合、失效旧写任务并清理后端状态和候选 UI。延迟任务仅结束捕获的旧宿主组合，独立于普通写任务的代数，不再因失焦失效而漏掉结束，也不能擦除正文或结束新组合。
3. **宿主终止与正常结束**：宿主已终止时不再递归请求结束。主动正常结束中的同步终止通知受保护，不取消当前提交响应；过期通知不影响新组合。
4. **失败恢复**：组合开始、写入、选择、结束及请求写锁失败时，清理当前代数的旧状态，允许后续输入重建组合；发生重入后不清理新代数。即使旧清理任务失败，也不继续复用其范围。
5. **显式取消**：候选取消只在立即取得同步写锁时清空；拒绝写锁时转为保留正文的退出，不排队进行迟到的删除。

### 中断时的文字策略

鼠标移出或失焦保留宿主当前已有文字，不强制替用户选词，避免延迟操作误删正文。这可能使宿主已有的未完成拼音作为文字保留；真实应用的候选提交/取消策略及撤销分组仍需以后验收。正常 Rime 响应中的取消仍走原有清空路径。

宿主持续拒绝写锁或结束组合时，前端会放弃旧状态；不能承诺替宿主恢复其写权限。本轮不启动后台无限重试。

## 扩展场景及结果

以下是 **x64 和 Win32 每架构**的结果，失败均为 0：

| 检查组 | 结果 |
| --- | --- |
| 新增中断与按键回归 | 69 项，包括上次的 19 项 |
| 既有组合引用检查 | 18 场景，各重复 64 次 |
| 组合结果 / 文档绑定 / 文本错误传播 | 23 / 13 / 12 项 |
| 自动提示、Tab 与独立 TSF 宿主 | 139 项，从既有 103 项扩展 |
| 原提交与模拟模型回路 | 23 项，真实模型调用为 0 |
| 正式 DLL 类工厂 | 26 项，实际卸载通过 |
| 正式与探测 Release TSF 构建 | 通过；正式 DLL 无 `WeaselProbe` 导出 |

新增覆盖：

| 场景 | 已验证行为 | 验证层次 |
| --- | --- | --- |
| 查询后无实际回调，下一次为退格/不同键/另一文档 | 不误用旧消费结果 | 真实按键 sink + 计数后端替身 |
| 重复查询、释放、重复键标记、仅实际回调 | 正确分派次数，释放待配对引用 | 同上，不连接日常 Rime |
| 失焦、重复停用、宿主终止 | 旧写入失效，新组合可开始，迟到清理不影响新组合 | 未激活服务 + COM 失败注入 |
| 鼠标移到组合之前/之后、拖选跨越边界、另一文档通知 | 仅退出对应组合；不删除已有正文 | COM 场景及生产 OnEndEdit |
| 正常结束内同步通知终止 | 不递归结束或取消正常响应 | 真实结束代码 + 同步回调替身 |
| 拒绝开始、写入/选择/结束失败 | 清理前端、后续可重建、旧引用释放 | 生产提交响应 + 故障注入 |
| 有提示/无提示 × 光标移出/跨边界拖选，共四组 | 正文保持，提示/Tab 释放，宿主旧组合确实结束，新位置可输入与提交 | Windows TSF 自有文本存储，不激活 TIP |

后面四组额外枚举宿主组合，确认不是只清空前端指针。扩展过程中修复了测试的两个缺陷：保留的正文会改变后续插入点，夹具必须显式恢复文末；未激活服务还必须绑定自有宿主的客户端 ID，才能实际执行延迟组合清理。该绑定仅在探测构建存在。

## 可重复运行

从 `E:\WeaseLC\weasel-0.17.4` 运行；使用本机 VS v143、Windows SDK 10.0.26100.0。构建前设置 `VCTargetsPath=E:\Microsoft Visual Studio\MSBuild\Microsoft\VC\v170\`，MSBuild 路径为 `E:\Microsoft Visual Studio\MSBuild\Current\Bin\MSBuild.exe`。

依次构建：

```powershell
& 'E:\Microsoft Visual Studio\MSBuild\Current\Bin\MSBuild.exe' weasel.sln /t:WeaselTSF /p:Configuration=Release /p:Platform=x64 /p:WEASEL_CORRECTION_PROBE_BUILD=1 /p:WEASELC_LOCAL_TEST_BUILD=0 /m:1
& 'E:\Microsoft Visual Studio\MSBuild\Current\Bin\MSBuild.exe' test\ProbeActivation\ProbeActivation.vcxproj /p:Configuration=Release /p:Platform=x64 /m:1
& 'E:\Microsoft Visual Studio\MSBuild\Current\Bin\MSBuild.exe' test\CorrectionIntegration\CorrectionIntegration.vcxproj /p:Configuration=Release /p:Platform=x64 /m:1

& .\output\ProbeDiagnostics\x64\ProbeActivation.exe .\output\Probe\v3\x64\weaselProbe.dll '{B749B45E-7DCE-4496-8414-9EE10970260A}' --composition-checks
& .\output\CorrectionIntegration\x64\CorrectionIntegration.exe .\output\Probe\v3\x64\weaselProbe.dll --automatic
& .\output\CorrectionIntegration\x64\CorrectionIntegration.exe .\output\Probe\v3\x64\weaselProbe.dll
```

Win32 替换所有平台/路径中的 `x64`。每一步检查退出码，失败先停下。**不要使用 `--activate`、`--input` 或注册命令。** 正式构建将 `WEASEL_CORRECTION_PROBE_BUILD=0`，只生成工作区 DLL，不复制到系统目录。

详细日志保存在被 Git 忽略的 `output/swallow-expanded-{x64,Win32}.log`、`swallow-automatic-*`、`swallow-commit-*`、`swallow-factory-*`、`swallow-formal-build-*`；源码与本报告入 Git，日志可按上述命令重建。

## 后续场景设计与待办

优先继续离线测试，不因之前的部署授权重新安装：

- [ ] 扩展宿主暂时无法提供选择/组合范围、恢复后输入的场景；将读取失败与真实鼠标中断分别验证。
- [ ] 增加多次快速文档切换、组合终止与子进程响应交错的长序列；检查持续失败下的对象数量和回收。
- [ ] 故障注入：纠错范围恢复被拒绝、IME 中英模式切换中断写入、候选 UI 重入回调；避免错误恢复影响后来输入。
- [ ] 自动提示缺失：增加不含正文的状态诊断，区分输入范围不可确认、关闭/过滤、模型无建议、布局不可用及结果过期；不放宽隐私门控。
- [ ] 补充不同应用策略的仿真：组合外点击提交/取消、只读状态动态变化、撤销/重做后的迟到结果。
- [ ] 真实应用及完整 Rime 按键、候选 UI、撤销、跨屏 DPI 验收保持后置，本轮未执行。

## 系统与开发资产

本轮结束快查：独立本机测试注册为空；无 ProbeActivation、CorrectionIntegration 或 llama-server 进程；模型 ready 标记不存在。未重新部署、启停日常输入法或修改默认输入法。

此前卸载剩余的 `C:\Program Files\WeaseLC-LocalTest\weaselx64.dll` **仍存在**，本轮未操作该已部署文件，也未声称清理完成；按先前交接，在释放占用后单独执行精确清理。模型权重、便携运行时、工作区构建及复现日志保留。Git 继续使用本地 `main`，不自动推送。
