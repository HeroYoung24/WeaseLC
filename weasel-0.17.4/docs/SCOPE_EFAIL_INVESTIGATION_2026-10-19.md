# GetValue / E_FAIL 专项调查（2026-10-19）

## 已确认的阻断，不等同于已找到宿主内部根因

`fe82f97` 重新部署后，用户确认基本中文、候选、鼠标中断恢复、退格及搜狗 Shift 均正常；固定纠错仍无提示。

人工阶段从 `2026-10-19T14:10:55.846561Z` 开始。普通与实际记事本 MSIX 身份的接口预检均立即返回一条固定建议、设置版本相同；预检元数据清空后才开始人工输入。人工提交记录为 5 UTF-16 单元，设置 Ready，随后：

```text
Capture → Validate → _CorrectionSafeScope → InspectInputScope
GetAppProperty(GUID_PROP_INPUTSCOPE) = S_OK
ITfReadOnlyProperty::GetValue(cookie, committedRange) = E_FAIL
VARIANT = VT_EMPTY
→ ScopeUnavailable → Cancel
```

人工阶段 `LastCapture`、`LastBrokerCheck`、`LastWorker` 均为空。这里的 LastCapture 只在范围校验和正文回读通过之后写入。

**因此没有继续读取，是程序在属性校验失败后主动退出。尚未执行后面的 GetText 原文回读，不能把这次结果描述为“正文读取失败”，也不能宣称上文缓存完整。** 没有请求模型、没有 Ready/Shown，也没有进入创建纠错窗口的路径。

显示分支修复不能消除这个更早的范围门控。前几轮在尚未处理该门控时过早安排了完整提示复测；后续停止重复同一链路的人工输入，先做接口对照。

真实证据在忽略的工作区目录：

- `output/LocalTest/visual-user-fixed-2026-10-19.json`
- `output/LocalTest/pipeline-20261019-140733-31b7b8576f3a4cb4941ce3638694c6fd.jsonl`
- `output/LocalTest/visual-installed-on-2026-10-19.json`
- `output/LocalTest/visual-installed-on-msix-2026-10-19.json`

## E_FAIL 不能充当原因解释

微软将 [GetValue 的 E_FAIL](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfreadonlyproperty-getvalue) 定义为未指定错误；缺少锁有单独的 `TF_E_NOLOCK`，无覆盖/多属性值有 `S_FALSE`。不能根据 E_FAIL 直接断定权限策略、混合范围、无锁、宿主不支持或结束组合后的失效。

现有生产调用使用标准的 [ITfInputScope 获取方式](https://learn.microsoft.com/en-us/windows/win32/api/inputscope/nn-inputscope-itfinputscope)。但 GetAppProperty 成功不足以说明后续属性数据的所有请求都能成功；需要分别验证范围归属、位置及会话时机。

## 用真实 Windows TSF 重现相同外观的错误

新增 `CorrectionIntegration.exe --scope-host`，只创建自有合成文本存储，线程管理器明确禁止激活 TIP；不注册输入法、不发送按键、不调用模型，窗口保持隐藏。

宿主故障注入只存在于 `test/ProbeActivation/MemoryTextStore.h`。每个条件创建新上下文，防止 TSF 缓存串扰。比较整段、起点、终点、单字符四种范围，均在新获得的读写编辑 cookie 下执行。

| 宿主条件 | GetValue | VT | 枚举 | 结论 |
| --- | --- | --- | --- | --- |
| 正常提供属性 | S_OK | VT_UNKNOWN | S_OK | 标准调用可工作 |
| FindNextAttrTransition 返回 E_NOTIMPL | S_OK | VT_UNKNOWN | 本机返回 0x8007000E | 单独缺失范围枚举并不能重现 GetValue/E_FAIL；已排除之前的这一推测 |
| RequestAttrsAtPosition 返回 E_NOTIMPL | E_FAIL | VT_EMPTY | S_OK | 可重现实机外观，整段和点范围都失败 |
| RetrieveRequestedAttrs 返回 E_FAIL | E_FAIL | VT_EMPTY | S_OK | 同样可重现，单个外层错误码不能区分这两个原因 |

这是本机 Windows TSF 的实验结果，不代表其他版本必然返回相同错误。文档说明 [EnumRanges](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfreadonlyproperty-enumranges) 依赖宿主属性转换枚举；本机观察也说明它与 GetValue 不能混为同一个能力。

**不能据合成重现断言实际记事本就是某个具体方法未实现。** 目前还没有记事本内部属性方法的返回值，真实宿主对照必须补齐。

双架构各 81 项、0 失败，日志 `output/scope-host-x64.log` 和 `output/scope-host-Win32.log`。诊断函数不主动调用 GetText、没有正文缓冲区、不改写文档；实验同时观察到 Windows TSF 自身可能为范围操作读取自有测试存储，因此没有把“没有任何内部文本读取”作为结论。

## 已补充的真实宿主对照

`include/CorrectionScopeTrace.h` 定义固定 40 字节记录；只有独立本机测试构建会发送，不改变正式范围许可策略。

三处记录分别是：

1. `LastScopeBeforeEnd`：本输入法提交后、结束组合前。
2. `LastScopeAfterEnd`：结束组合并执行原 Capture 后。
3. `LastScopeReadEdit`：同一上下文下，下一次文本变更的 OnEndEdit 只读 cookie。每次提交最多消费一次；与上下文绑定，Cancel/生命周期清理会释放。

第一笔提交前尚未加载开启设置时，BeforeEnd 可以缺失；AfterEnd/ReadEdit 以及随后提交的 BeforeEnd 用于对照，不把缺失记录当成接口返回错误。

每条记录包含：

- 版本、阶段、范围/属性上下文身份是否一致及范围是否为空的标志；
- 范围 GetContext 返回值；
- 当前 cookie 下 GetSelection 返回值；
- 同一属性对象查询原范围、克隆后折叠起点、折叠终点、当前选区的返回值；
- EnumRanges 返回值。

均为 HRESULT 和标志，没有正文、指针、窗口句柄、应用名称或路径。查询这些替代范围仅用于诊断，不会因某个点成功而授权整个未知范围。记录通过已有验证身份的协调通道转发，40 字节仍在原 40 字节元数据上限内；控制端只允许三个固定阶段名，拒绝未知阶段。

`Watch-Diagnostics.ps1` 已同步采集三条记录；`Read-Diagnostics.py` 已同步支持。双架构协调器各 26 项、0 失败，包含新元数据传输与未知阶段拒绝；双架构独立正式目标构建通过。

### 根据下一次实际记录决策

| 实际对照 | 后续处理方向 |
| --- | --- |
| 范围 GetContext 成功但身份不同 | 修正范围/上下文来源，并在正确上下文取得匹配 cookie，禁止沿用原 cookie 跨上下文查询 |
| 同一上下文、GetSelection 失败 | 先处理锁或上下文生命周期；不能解释成属性类型不支持 |
| BeforeEnd 成功、AfterEnd 失败 | 检查结束组合对原范围/属性的影响及范围快照 |
| 写会话失败、ReadEdit 成功 | 将安全验证/累计转到宿主提交完成后的只读会话，再补取消、焦点与版本回归 |
| 整段失败，点范围成功 | 核对范围边界/属性覆盖；不能用一个成功点放行整段 |
| 原范围、点、选区及不同会话均失败，锁与身份正常 | 调查宿主输入范围属性能力或实现差异，设计明确的兼容策略；不再反复改提示布局或等待模型 |

这些外层对照仍不直接暴露记事本内部 RequestAttrsAtPosition/RetrieveRequestedAttrs 的返回值。如果所有对照相同，应继续针对该宿主接口能力调查，不能再次把 E_FAIL 当成已确认的底层原因。

## 左上角短暂窗口的核对

上一轮自动原生显示回归 `VisualChecks.inc` 会在屏幕中央、左上、右下，循环创建/显示/隐藏自有候选和纠错窗口；左上短闪符合这个测试行为。相关输出 UTC 时间为 13:35 和 13:37（北京时间 21:35、21:37）。本轮人工测试在北京时间 22:11 左右，记录只有范围阻断，未达到纠错窗口创建和显示阶段。

没有具体发生时间或窗口身份，不能认定用户看到的某个窗口就是回归夹具，也不能直接排除候选框自身瞬时定位问题。但本轮没有纠错窗口被系统策略拦截的证据；下一次显示问题应关联 Ready/Shown、实际矩形与隐藏原因，不能只凭闪窗判断。

## 当前系统状态

用户已切回搜狗并关闭记事本。本轮独立 COM/TIP 已通过 UAC 注销、测试输入项已移除、专用后端与协调器退出，原版文件/注册基线一致。普通用户测试设置保留且自动纠错关闭；旧程序文件保留，只有 ChatGPT 仍加载旧 DLL，未强制卸载或安排重启删除。

新接口对照构建尚未部署，E_FAIL 未宣称修复。恢复时使用包含 ScopeTrace 的新包，释放旧 DLL 后按原流程清理/安装，优先取得上述接口对照，不再把同一整句提示测试作为首个定位动作。
