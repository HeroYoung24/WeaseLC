# 记事本输入类型属性兼容修复（2026-10-19）

## 本轮实机对照的确定结果

接口对照版 `1af35b5` 已重新部署，200 文件校验通过；用户分别上屏中文并保留窗口。普通/MSIX 固定响应预检成功后已清空预检诊断，人工阶段从 `2026-10-19T14:41:52.3071670Z` 开始。

实际 `LastScopeBeforeEnd / LastScopeAfterEnd / LastScopeReadEdit`：

| 项目 | 结束组合前 | 结束组合后 | OnEndEdit 只读会话 |
| --- | --- | --- | --- |
| 范围 GetContext | S_OK，同一上下文 | S_OK，同一上下文 | S_OK，同一上下文 |
| 属性 GetContext | S_OK，同一上下文 | S_OK，同一上下文 | S_OK，同一上下文 |
| GetSelection（核对 cookie） | S_OK | S_OK | S_OK |
| 原范围 GetValue | E_FAIL | E_FAIL | E_FAIL |
| 折叠起点 GetValue | E_FAIL | E_FAIL | E_FAIL |
| 折叠终点 GetValue | E_FAIL | E_FAIL | E_FAIL |
| 当前选区 GetValue | E_FAIL | E_FAIL | E_FAIL |
| EnumRanges | S_OK | S_OK | S_OK |

前两阶段 flags=27（同一范围/属性上下文、范围非空）；只读阶段 flags=31（同一上下文、当前选区为空）。所有查询均针对已提交范围或当前选区，没有读取任意文档历史。

可以排除本次失败是范围跨上下文、没有取得选区读取锁、仅结束组合后失效、仅非空范围失败或枚举本身失败。**本机该记事本版本的输入类型属性值查询在这些条件下持续不可用，而项目将它设成硬性前提，导致主动取消。** 未取得记事本内部回调的错误码，不能继续声称具体是哪一个内部方法未实现，也没有系统策略拦截提示窗口的证据。

人工阶段未成功写入 LastCapture、未请求模型。证据位于：

- `output/LocalTest/scope-interface-user-2026-10-19.json`
- `output/LocalTest/pipeline-20261019-144152-8834cd978d2b423c8ef8f5d98763faa8.jsonl`

## 兼容修复

保留原输入类型白名单，增加一个独立的原生纯文本控件验证路径。不是把 E_FAIL 当作文本许可：以下条件必须全部满足。

1. 仅限本输入法跟踪的本地上屏片段（`ScopeUse::LocalCommit`）；任意附近/历史读取不适用。
2. 原属性查询恰好为 `PropertyValue / E_FAIL / VT_EMPTY`；S_FALSE、无锁、接口类型异常、部分返回接口均不适用。
3. 上下文状态、键盘许可、非只读等原检查通过，范围 GetContext 与目标上下文的 COM 身份一致。
4. 当前线程真实焦点窗口必须等于上下文视图窗口或是它的子窗口；视图/焦点均属于当前进程，焦点控件属于当前线程；检查前后焦点保持一致。
5. 精确匹配 Unicode 原生 Edit 或 Rich Edit 类；普通顶层窗口、浏览器、自定义/窗口无关控件不适用。
6. 控件启用、具有多行样式、无只读/密码样式，密码替代字符为 0。
7. Rich Edit 还必须明确返回纯文本模式、没有富文本标志；经典 Edit 自身是纯文本控件。

控件查询只使用类名、样式、线程/进程归属、密码字符和文本模式等元数据。不发送 WM_GETTEXT，不修改选区，不向未知或异线程/异进程控件发送消息。支持的原生 Rich Edit 类包括 `RichEdit20W`、`RICHEDIT50W`、`RichEditD2DPT`；列入类名本身不足以通过其余验证。

微软文档说明 [EM_GETPASSWORDCHAR](https://learn.microsoft.com/en-us/windows/win32/controls/em-getpasswordchar) 返回实际使用的密码替代字符，Rich Edit 的多行控件也支持密码模式；[EM_GETTEXTMODE](https://learn.microsoft.com/en-us/windows/win32/controls/em-gettextmode) 用于区分 Rich Edit 的纯文本/富文本模式。两项均核对，不只凭窗口名称或应用名称放行。

明确返回的密码、私密、PIN、URL 等不允许类型仍由原规则拒绝；原生控件路径不会覆盖这些值。即使通过新路径，焦点、原文逐字匹配、空选区、组合归属、版本、设置、时限、只读及 Tab 写锁规则仍继续执行。

新成功状态为 `ScopeStage::NativePlainText`，表示通过了第二种验证，不表示 GetValue 已恢复。原始 GetValue 返回值继续由 ScopeTrace 保存；其 flags 的第 8–15 位记录原生证据，全部满足为 255。若记事本未能提供可验证的原生焦点/纯文本控件，该路径仍拒绝；下一次可以直接从 nativeFlags 知道未通过哪一项。

## 测试与边界

- 双架构各 **50 项范围策略**通过：LocalCommit 专用、附近读取拒绝、S_FALSE/无锁/异常变体拒绝、私密类型优先、只读优先及引用释放。
- 双架构各 **15 项真实原生控件**通过：隐藏的自有 Edit/Rich Edit，核对直接/父视图、无关视图、未知根窗口、无焦点、单行、只读、禁用、异线程、富文本、显式纯文本、密码字符与前台不变。
- 双架构各 **81 项真实 TSF 属性接口对照**在增加兼容逻辑后仍通过；无法验证的自有通用宿主仍不因 E_FAIL 获得许可。
- 双架构独立正式 TSF/IPCServer/Server/Deployer 构建通过；保留原有弃用/测试局部变量遮蔽警告。

测试没有显示窗口、没有更改桌面焦点，没有激活系统输入法或启动真实模型。范围策略测试可以注入验证结果来覆盖分支；实际控件证据另由真实 Windows 控件检查。这些回归不能替代实际记事本的组合验证，不能据此宣布其提示/Tab 已通过。

复现：

```powershell
.\output\ProbeDiagnostics\x64\ProbeActivation.exe --scope-policy
.\output\CorrectionIntegration\x64\CorrectionIntegration.exe --native-scope
.\output\CorrectionIntegration\x64\CorrectionIntegration.exe --scope-host
```

Win32 只需替换路径架构。对应日志前缀 `output/native-scope-*`。

## 当前状态与恢复顺序

用户已切回搜狗并关闭测试记事本，自动纠错关闭；本轮诊断输入项/COM/TIP 已经 UAC 注销，专用后端/协调器退出，原版输入法基线一致。旧文件与普通测试设置保留。可枚举进程中确认 ChatGPT、Explorer 占用旧 DLL；部分系统/受限进程模块无法枚举，不能称为完整零占用扫描。没有强制卸载、结束宿主或安排延迟删除。

新兼容修复只在源码与工作区构建中，尚未部署。下一次先释放旧模块、清理并安装包含该修复的新包。先核对两次上屏后的 LastScope 是否为 NativePlainText、LastCapture 长度/保留量及 nativeFlags，再测试固定“错子→错字”提示/Tab。若原生证据不足，按未满足的证据项处理，不再次凭相同 E_FAIL 修改范围或等待模型。

## 2026-10-19 兼容版首次实机结果：上屏范围与缓存验证通过

重启后安装 a1a3826，200 文件哈希、原版基线、双架构后端及普通/MSIX 固定响应预检通过。用户在保留的测试记事本中分别输入“你好”“再见”，确认两次上屏正常。

人工阶段 15:18:55–15:19:00 UTC 的诊断显示：

- LastScope=NativePlainText(19)/S_OK；三个范围对照的 nativeFlags 均为 255，独立原生纯文本证据全部满足。原 GetValue 依然为 E_FAIL，成功来自兼容验证路径。
- LastCapture 记录先后为：总量2/新增2/保留0、1/1/0、2/2/0、3/1/2。Capture 仅在 Validate 的原文逐字回读成功后记录，故已证明真实上屏范围可读、缓存可保留已有2单元并追加1单元；没有保存正文，不能称为两句全部连续累计成功。
- LastBrokerCheck status5/InvalidRequest，LastAutomatic stage14/WorkerFailed。核对 FixedAutomaticVerdict 后确认：该夹具只接受“这里有错子”、其前缀和句号变体；“你好”“再见”不在合成语料内，按设计拒绝并取消缓存。此结果不证明真实模型失败或正常无错文本会被生产模式拒绝，也不作为模型/显示验收成功。
- 测试记事本仍保留，专用后端/协调器运行。下一阶段复用当前 DLL，按新行输入精确夹具句，再输入 ni，分别验证协调器成功响应、Ready/Shown、Tab 与鼠标中断。不再重复安装或范围失败测试。

证据：`output/LocalTest/native-compat-user-capture-2026-10-19.json`；`output/LocalTest/pipeline-20261019-151508-734a29cbc3294c38a1e68ba22408d236.jsonl`。记录均为阶段、长度、时间和错误码，无正文。
