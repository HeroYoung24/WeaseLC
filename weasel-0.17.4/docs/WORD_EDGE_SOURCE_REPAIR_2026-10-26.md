# Word 无提示与 Edge Tab 替换：源码修复与离线验证

日期：2026-10-26。基于本地 main 的 be354ac。用户离开电脑期间，只修改工作区源码、构建和运行自有离线宿主；不更新、卸载、重新注册或激活已安装测试输入法，不操作实际 Word / Edge 文档，不调用真实模型。完成后停止，等待用户确认。

## 结论与证据边界

| 问题 | 已定位到的代码问题 | 本轮修复 | 仍须现场确认 |
| --- | --- | --- | --- |
| Word 无提示 | 上文校验在输入范围门控退出；现有兜底只接受原生纯文本框，Word 文档不符合该能力验证 | 对缺失范围属性的特定失败，独立验证当前 Word 文档的可编辑、无保护和无权限限制状态；仍只允许跟踪的本输入法上屏文字 | 本机 Word 是否提供这些元数据；通过门控后的上文累计、候选旁显示及 Tab |
| Edge 提示出现、Tab 原文不变 | 同一个编辑锁中扩大合成范围、写前文、缩回拼音；合成宿主复现前文丢失，后续回读识别为未确认 | 将 TSF 合成暂结束、前文替换、原位置恢复拼音分成三个同步编辑会话；不重放 Rime 输入 | 实际 Edge A/B/C 的页面持久化、DOM 事件、选词、撤销和视觉表现 |

**源码修复与离线验证完成，不等于真实 Word / Edge 验收通过。** 没有用自动断言替代用户观察，也没有通过换模型、降低门槛或放松私密保护来制造提示。

## 一、Word：为什么读不到可用于提示的上文

### 完整调用链

正常上屏经 `_ApplyCompositionResponse` 写入本次提交文本、保留其 TSF 范围、结束该次组合，然后调用 `AutomaticCorrection::Capture`。Capture 只累计同一上下文、同一相邻插入点上由本服务刚提交的文字；不扫描文档全文或任意历史。旧范围必须回读一致，累计上限仍为 1024 个 UTF-16 单元。

Capture 随即进入 Validate。它按顺序核对焦点、设置版本、可写状态、输入范围、范围原文及选区/拼音位置。只有通过后才建立可用捕获、启动检查计时和模型请求，之后才进入建议显示。

前轮固定响应现场的**新鲜 LastAutomatic 是 ScopeUnavailable**，所以不能归因于模型速度、模型没有返回或 Tab 窗口坐标。最近范围详情是 GetValueFailed / E_FAIL，但该 20 字节详情本身没有时戳，不能把它升级成具有独立时戳的本轮完整接口对照。也不能从 E_FAIL 一项推断 Word 内部究竟是哪段实现返回错误。

### 原兼容分支的明确缺口

`InspectInputScope` 获取 `GUID_PROP_INPUTSCOPE` 的应用属性，再对目标范围 GetValue。遇到 `E_FAIL + VT_EMPTY` 时，原来只有 `NativeTextRangeVerified` 能提供另一条正向能力证明：同进程同线程、与 TSF 视图关联、已启用、无密码、可编辑的多行 Edit 或纯文本 RichEdit。Word 的 `_WwG` 文档窗口不是这种控件；因此即使中文基本输入正常，也会被拒绝自动纠错。

这里拒绝的是“无法确认输入范围允许纠错”，并非已经证明 Word 的 GetText 无法读取字符。直接跳过范围检查会把未知或受保护字段混进模型输入，不能作为修复。

### 新的受限 Word 能力验证

新增 `include/CorrectionDocumentScope.h`。只在 LocalCommit 策略下，且应用属性恰好返回 `E_FAIL + VT_EMPTY` 时尝试：

1. 目标范围确属当前 TSF 上下文，当前焦点是该视图中的同进程、同线程 `_WwG` 文档窗口；进程主映像名必须为 WINWORD.EXE。仅识别进程名本身不授予访问权限。
2. 通过该**具体窗口**的 `OBJID_NATIVEOM` 获取已经存在的 Word Window 对象，不创建 Office 实例，不使用全局 ActiveDocument。
3. 仅查询封闭元数据：Window.Document、Document.ReadOnly、Document.ProtectionType、Document.Permission.Enabled。要求 ReadOnly 为严格布尔 false、ProtectionType 为 wdNoProtection(-1)、权限限制 Enabled 为严格布尔 false。任何缺项、错误、未知类型或限制都拒绝。
4. 查询后重新核对原焦点。重入保护阻止 Office getter 消息泵递归进入同一检查；原有修订代数、焦点和原文校验继续防止重入后使用过期范围。

不会读取 Document.Content/Text、名称、路径、用户词库或选区正文，也不会调用 setters、宏、保存或 Office 启动接口。这些元数据是宿主能力证明，不扩大允许读取的文字范围；文档正文仍只经原有 TSF 跟踪范围回读。

明确的 IS_PRIVATE、密码、PIN、URL 等范围不进入此分支。TSF 只读、键盘禁用、空上下文仍提前拒绝。NearbyText 的任意上文探测不会使用该能力。没有因“Word 是熟悉的应用”默认放行。

### 避免下一轮只得到同一个 E_FAIL

范围诊断追加 stage 20（WordDocument）和 21（WordMetadataUnavailable），保持 20 字节协议不变。仅这两种 stage 的 scope 字段表示封闭证据位，不是 InputScope 枚举；监测脚本已明确解码为 wordEvidence。

| 位 | 含义 |
| --- | --- |
| 1 | 范围上下文身份符合 |
| 2 | 有效 TSF 视图 |
| 4 | Word 文档窗格和进程/线程条件符合 |
| 8 | 取得该窗格原生对象 |
| 16 | 取得 Document 元数据对象 |
| 32 | ReadOnly 确认为 false |
| 64 | ProtectionType 确认为无保护 |
| 128 | Permission.Enabled 确认为 false |
| 256 | getter 完成后焦点仍符合 |

只有全部为 511 才通过。失败时仍阻止提示，但可以区分“未识别为文档”“对象不可取得”“只读/保护/权限状态未通过”，无需盲目反复输入。LastScope 原协议仍无时戳，现场必须结合新的 LastCapture / LastAutomatic 及本轮监测时间，不单独相信遗留记录。

## 二、Edge：为什么有提示却没有替换

### 接口成功与页面同步不同

旧代码必须让 SetText 范围被本服务的活动 TSF composition 覆盖，所以先把 ni 的 composition 起点临时移动到纠错词前面，替换前文，再在**同一个写锁**退出前恢复起点到 ni。

这种做法在简单内存文档和记事本曾通过，但浏览器维护 TSF 缓冲与页面编辑器两个状态。Chromium 的 RequestLock 在 OnLockGranted 返回后才综合最终合成范围，把文字同步到 TextInputClient；仍活动且起点未变时，更新的是合成部分。中途扩大又恢复的范围，不能被当作页面必然收到前文改动的保证。

独立 CompositionProjection 行为模型已复现：有 ni 时旧路径前文修改丢失；无 ni 对照成功。该模型不是完整 Chromium 实现。参考的是官方 Chromium 源码，不是对用户当前 Edge 构建的逐行匹配，因而仍保留实际应用验收边界。

### 本轮生产路径

有活动拼音时，Tab 进入一次局部事务：

1. 第一把同步写锁：重新校验提示、原文、范围、焦点和拼音；退休建议，结束**本服务这一条 TSF composition**，保留宿主中的 ni 和 Rime 输入缓冲，不清空、不学习、不重放输入。
2. 第一锁完全返回后，第二把同步写锁：重新校验源范围全文、纠错词、拼音、选区、保护状态、设置和代数；用 TSF 默认临时纠错 composition 替换前文，并恢复跟踪的选区。宿主此时看到独立的前文修改。
3. 第二锁返回后，第三把同步写锁：再次校验同一批证据，在**现有 ni 的范围**上 StartComposition，恢复显示属性和光标。不重新插入 ni，不从键串重建 Rime 会话。
4. 后续独立只读锁：检查替换是否保留、拼音 composition 是否确实恢复在原跟踪范围、拼音正文是否一致。只有这一步通过才报告 TabApplied。

没有活动拼音时，保留原先单次写入加独立回读的路径。没有发送退格/模拟键盘、修改剪贴板或调用浏览器脚本。

### 中途失败策略

- 所有请求限定 TF_ES_SYNC；不排队迟到写入。每步返回后显式退休其可调用体，即使宿主异常保留 edit-session 对象，晚到回调也不能使用已释放的栈变量或再次写入。
- 第一步成功改变合成状态后，Tab 只消费一次；重复查询/自动重复不再写入，抬起后释放快捷键。未发生动作时仍交还正常 Tab。
- 第二步被一次性拒绝时，尝试在新同步锁中恢复未改过的原拼音；不会重试纠错。
- 宿主返回 S_OK 却未改文时，不报告成功；原文仍一致才允许恢复原拼音。
- 已写入但锁退出后被宿主撤回，报告持久化未确认。恢复锁被拒绝则报告恢复未确认，不冒充完整 Tab 成功。
- 失焦、改选区、改原文、切入私密/只读、代数变化时，不继续旧替换，不去其他位置恢复拼音。
- 若无法恢复且仍是同一代本服务的悬空后端拼音，只清理这一后端状态和候选 UI，保留宿主现有文本。失败时残留的拼音可能成为普通文字；非行内模式也可能保留原占位字符。宁可保留用户文字，不擅自删除或覆盖新内容；下一键不能卡死。较好的失败提示仍需后续 UI 验收。

## 三、验证结果

双架构 x64 / Win32：

| 检查 | 各架构结果 |
| --- | --- |
| 自动纠错 / 阶段同步 / 故障边界 | 432 项通过，包含既有正常回归；固定建议及模拟传输，不是真实模型 |
| 离线响应及上屏链路 | 23 项通过，2 次模型调用为夹具响应 |
| 按键查询/实际回调、中断 | 104 项通过，Word 重复派发修复保持 |
| 焦点/诊断 | 26 项通过 |
| 范围策略 | 57 项通过 |
| Word 封闭元数据 | 14 项通过，包含只读、所有文档保护模式、权限限制、缺项/类型错误和引用平衡 |
| 原生纯文本控件能力 | 15 项通过 |
| DLL 引用与卸载 | 通过 |
| 独立测试 TSF / Server / Deployer 构建 | 双架构成功，未安装 |

新增同步回归包括两种允许范围、有/无拼音、替换变短/等长/变长、重复 Tab 和下一次选词；故障覆盖第二/第三把锁拒绝、一次性拒绝后的拼音恢复、失焦、私密/只读切换、选区/源文变化、代数失效，以及宿主异常保留会话后的晚到调用。

初轮失败日志保留：未改文与恢复未确认的诊断归类经过修正；第一次双架构同时运行时，自有协调器争用同名管道导致 Win32 启动失败，顺序重跑通过。最终协调器测试一律顺序执行，不把这次争用记成产品根因。

证据位于 `output/LocalTest/word-edge-*20261026.log`；最终同步使用 `word-edge-handoff-staging-<架构>-20261026.log`，离线链路 `word-edge-handoff-offline-*`，生命周期 `word-edge-verified-composition-*`，原生控件 `word-edge-verified-native-*`。构建只保留既有 localtime / 测试变量遮蔽警告。

尚未覆盖：实际 Edge/Word 宿主接收上述同步阶段后的最终可视结果、DOM composition/input 事件、宿主重写、撤销/重做、真实 Rime 及真实模型在该新路径的现场对照。旧真实模型记事本通过记录不能外推成本轮新路径已验收。

## 四、安装状态与恢复入口

本轮没有安装、卸载、重新注册、启用纠错或启动已安装后端/模型。只读核对安装清单 200 个文件全部匹配，原输入法基线相同；安装仍 61c3560，纠错 mode=0、automatic=false。未触碰 Sogou 或原版 Weasel 设置、用户词库和频率数据。

机器可读记录：`output/LocalTest/word-edge-source-repair-state-20261026.json`。新构建只存在工作区，不能把它称为当前已运行程序。

## 五、用户回来后的验证顺序

1. 用户确认后再检查 DLL 占用，准备配套更新及回退；本轮不预先启动部署。
2. Off：Word / 记事本数字选词、句号、半截拼音后鼠标中断、退格、正常 Tab、搜狗 Shift，先防基本输入回退。
3. Word Fixed：先看新鲜 LastCapture 是否累计本轮上屏字符，以及 Word capability 或明确范围是否通过。未通过先看证据位，不重复要求用户测试提示；通过后才看 Shown、提示位置和 Tab。再测受保护/只读文档应跳过。
4. 记事本 Fixed：提示、Tab、ni 保留、下一次选词、中断；验证此次分阶段路径没有损坏原来通过的功能。
5. Edge A/B/C Fixed：分别测无拼音直接提示和保留 ni 的提示；按一次 Tab 后核对页面正文、ni、光标、后续选词，再测重复 Tab、中断和各阶段状态。加撤销/重做及 input/composition 事件观察。
6. 上述通过后再用已冻结的 4B 配置做真实模型对照；不同时引入调参变量。

停止条件：基本输入重复/吞键、光标跳错、改写非目标范围、旧建议复活、受保护字段进入检查，立即关闭纠错并停止该应用测试，保存元数据；不反复向同一文档尝试写入。

## 参考依据

- [Microsoft：TSF compositions 生命周期](https://learn.microsoft.com/en-us/windows/win32/tsf/compositions)。
- [Microsoft：ITfRange::SetText、默认临时 composition 和活动范围限制](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfrange-settext)。
- [Chromium 官方 TSFTextStore 源码](https://raw.githubusercontent.com/chromium/chromium/main/ui/base/ime/win/tsf_text_store.cc)，重点 RequestLock 的锁后同步及合成起点判断；不是用户 Edge 精确构建版本。
- [Microsoft：通过 `_WwG` 的 OBJID_NATIVEOM 取得 Word Window](https://learn.microsoft.com/en-us/windows/win32/api/oleacc/nf-oleacc-accessibleobjectfromwindow)。
- [Word Window.Document](https://learn.microsoft.com/en-us/office/vba/api/word.window.document)、[Document.ReadOnly](https://learn.microsoft.com/en-us/office/vba/api/word.document.readonly)、[Document.ProtectionType](https://learn.microsoft.com/en-us/office/vba/api/word.document.protectiontype)。ReadOnly 指原文件保存限制，本轮将其作为额外保守拒绝条件，并不独立等同于全部编辑权限；还必须通过 TSF 状态与保护/权限检查。
- [Office Permission.Enabled](https://learn.microsoft.com/en-us/office/vba/api/office.permission.enabled)，本轮只调用 getter，不设置或移除权限。
