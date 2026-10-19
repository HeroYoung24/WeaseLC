# 自动提示显示专项检查（2026-10-19）

## 当前结论

发现并修复了一个确定的正式版本显示错误：组合输入时，即使候选窗口可见且坐标有效，旧代码仍会无条件隐藏纠错提示。独立测试使用探测编译分支，之前没有覆盖这个正式版本差异。

同时，本轮真实记事本仍有另一个更早的阻断：输入范围属性 `GetValue` 返回 `E_FAIL`。不能据字符提交记录宣称整句上文已经成功缓存，不能宣称真实应用提示/Tab 已验收通过。

## 三条链路的证据

| 环节 | 本轮观察 | 能确认与不能确认的内容 |
| --- | --- | --- |
| 输入与上文 | 用户确认中文、候选、半截拼音鼠标中断后继续输入/退格、搜狗 Shift 正常。实际记事本有 LastKey、LastCommit、LastGate，设置来源 Ready；人工阶段出现 5 单元提交，但随后 ScopeUnavailable | 提交入口与共享设置已工作；不能确认目标错句累计、回读、校验均完成 |
| 模型/协调器 | 已安装协调器在普通与实际记事本 MSIX 身份预检均立即返回一条固定建议、设置版本一致。预检记录已清空，人工阶段没有 LastBrokerCheck | 预检连通性通过；人工输入在模型前停止，不能归因于模型响应过慢。本轮没有启动真实模型 |
| 提示窗口/Tab | 修复正式版本条件分支；固定建议独立驱动实际原生候选窗口、提示绘制、定位和 Tab 路径 | 原生显示定向检查通过；人工记事本尚未安装新显示修复，也未通过整链路验收 |

人工阶段从 `2026-10-19T13:24:54.4536320Z` 开始。观察器先前的合成预检不能混入人工链路结论。诊断只保存阶段、错误码、长度、时间等元数据，没有读取或保存测试记事本正文。

本地证据（忽略的输出目录）：

- `output/LocalTest/broker-fixed-user-diagnostics-2026-10-19.json`
- `output/LocalTest/pipeline-20261019-132120-8aa0e1e8e3344f738b9aa890531dd31f.jsonl`
- `output/LocalTest/broker-acceptance-active-2026-10-19.json`

`LastScope` 为 `PropertyValue / 0x80004005 / VT_EMPTY`。稍后的 `LastCapture` 只有另一段 2 单元提交、保留长度为 0，不能作为目标整句已缓存的证据。`E_FAIL` 本身也不能证明是混合属性值；[微软 GetValue 文档](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfreadonlyproperty-getvalue)对多值/无覆盖使用的是 `S_FALSE`，不可混同。

## 正式版本显示错误

位置：`WeaselTSF/AutomaticCorrection.cpp::Refresh`。

旧代码把 `else if (!testing_)` 放进探测构建的条件编译中，隐藏语句块却留在条件编译之外。正式版本预处理后相当于：

```cpp
if (candidateAvailable) anchor = candidate;
{ Report(CandidateUnavailable); Hide(); return; }
```

因此在正在组合输入、应当在候选框旁显示提示的时刻，必然隐藏并返回。这是源代码层面的确定错误，不依赖于范围属性失败，也不依赖于模型是否有回复。

修复将 `else` 保留在所有构建中，探测构建仅决定“是否允许缺失候选框”的测试选项。正式版本仍要求候选框确实可见、属于本输入法；隐藏或由宿主接管候选界面时，不凭任意坐标显示、也不消费 Tab。

## 显示接口与行为检查

当前方案使用本输入法自己的 Win32 顶层提示窗口，候选锚点来自实际 `CCandidateList::WindowRect`，而非把纠错项混入 Rime 候选词。提示窗口通过 `SetWindowPos(HWND_TOPMOST, …, SWP_NOACTIVATE | SWP_SHOWWINDOW)` 定位并显示；`WM_PAINT`/`WM_PRINTCLIENT` 使用同一绘制函数。透明度为 255，窗口不激活、不抢前台，鼠标命中穿透。Tab 由输入法按键处理通道在建议有效时处理。

新增 `test/CorrectionIntegration/VisualChecks.inc`：

- 固定建议绕过模型与配置依赖，文档为自有合成 TSF 文本存储；候选窗口和提示窗口使用项目实际 UI 类。
- 显式启用严格候选要求，禁止旧模拟测试中的光标坐标回退，检查正在组合 `ni` 时的显示。
- 右侧、左侧、下方 × 中央、右下边缘、左上边缘共 9 组实际窗口坐标检查。
- 校验可见、无候选重叠、处于显示器工作区、边缘回退、首选位置、前台不变、不激活、置顶、不透明，以及提示文字/Tab 说明。
- 候选隐藏或由宿主接管时隐藏提示且放行 Tab；候选重新可见后恢复提示；Tab 查询不写入，实际接受后撤去提示。
- 回归包含既有组合、中断、过期、原文变化和清理场景，并验证窗口类注销与 DLL 实际卸载。

结果：x64 与 Win32 各 **287 项、0 失败**。显示器实测 DPI 为 144。示例物理坐标：

| 布局 | 候选矩形 | 提示矩形 |
| --- | --- | --- |
| 中央右侧 | 1280,794,1366,1003 | 1378,794,1858,969 |
| 中央左侧 | 1280,794,1366,1003 | 788,794,1268,969 |
| 中央下方 | 1280,794,1366,1003 | 1280,1015,1760,1190 |
| 右下边缘，右侧布局回退 | 2474,1287,2560,1496 | 1982,1287,2462,1462 |

提示自身的实际绘制结果已查看，文字与操作说明清晰，无裁切。下图来自自有提示窗口，不是用户桌面截图；位置关系另由上述矩形断言验证。

![实际绘制的纠错提示](images/CorrectionHintStrictVisual.png)

双架构独立正式 TSF 构建也通过，未启用探测宏；保留现有 `localtime` 弃用警告。本轮没有覆盖混合 DPI 多显示器之间的实际拖动、其他宿主接管候选窗口或高对比模式的人工验收。

复现（项目根目录，需已构建探测 DLL 与集成测试程序）：

```powershell
.\output\CorrectionIntegration\x64\CorrectionIntegration.exe E:\WeaseLC\weasel-0.17.4\output\Probe\v3\x64\weaselProbe.dll --automatic
.\output\CorrectionIntegration\Win32\CorrectionIntegration.exe E:\WeaseLC\weasel-0.17.4\output\Probe\v3\Win32\weaselProbe.dll --automatic
```

测试不注册/激活系统输入法、不发送系统按键，仅产生自有窗口并还原临时测试配置。日志：`output/strict-visual-x64.log`、`output/strict-visual-Win32.log`。

## 保留问题与下一步

1. 重点调查 `EndComposition → Capture → InspectInputScope` 的范围生命周期与属性读取时机。下一次应使用无正文的元数据区分原提交范围、重新取得的等价范围、属性枚举与编辑会话时机；没有确定安全范围之前，不把 `E_FAIL`、缺失属性或密码范围放行。
2. 范围读取通过后，先确认实际目标提交的 added/retained/total 长度及原文回读，再确认人工请求编号、固定建议返回、CandidateUnavailable/Shown、窗口矩形，最后验证 Tab 替换及 `ni` 保留。
3. 固定响应整链路通过之后，才验证真实小模型延迟与质量。不再重复同一已安装失败版本的相同人工用例。

当前系统安装的仍是 `e3284c5`，本次显示修复仅在源码与工作区构建中。测试版自动纠错已关闭；独立后端/协调器仍保留，用户切回搜狗、测试记事本保留。未覆盖已加载 DLL，未修改原版小狼毫或搜狗，未增加系统服务。后续替换安装前先关闭测试窗口、核对 DLL 占用并按既有流程回退，不强制卸载占用模块。
