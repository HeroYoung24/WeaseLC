# Word 上文读取与纠错缓存丢失：重新定位

日期：2026-10-26。基于 46f481f 重新审查。此次不修改生产纠错逻辑或已安装程序，不启用纠错，不操作用户 Word 文档、不调用模型。新增仅探测构建可用的缓存长度读数及自有内存宿主分析入口。

## 结论

**没有证据表明 Word 对本项目实施了“统一禁止读取上文”。现场反而已有两次严格回读成功。当前可以确认的机制是：程序拿到了上屏文字，但遇到范围/权限元数据不可确认，或推理请求失败时，主动清空同一份短期纠错缓存。**

这不等于 Word 的所有范围都可读取。真正受保护的文档/字段、不可用的输入范围及旧文档历史仍须分别验证；本项目当前也没有实现任意全文或历史读取。

## 1. 现有实测到底证明了什么

上一轮已核对 Word 加载的是安装修复版 d7a3fbe。固定阶段现场记录中，存在如下不同事实：

- UTC 13:32:01、13:32:10 有 5 个 UTF-16 单元的成功上屏交付记录，交付阶段到达 LastCommit=7。这证明后端提交文字已经进入前端代码，不能说 Word 把输入文字挡在输入法之外。日志不保存正文，因此不能独立断言每个5单元记录的具体内容。
- UTC 13:32:19、13:32:20 有新鲜 LastCapture，长度分别为2和3，retainedUnits均为0。LastCapture 只在 Validate 成功后写出；Validate 中必有 ITfRange::GetText 的 S_OK、长度及正文完全一致检查。因此**这两段本输入法刚上屏的文字确实已在 Word 范围中回读成功**，但没有累计旧片段。
- 同一轮也有新鲜 ScopeUnavailable。Word 文档能力位383说明除了 Permission 检查之外，范围身份、视图、Word窗格、Document、ReadOnly=false、ProtectionType=-1和最终焦点检查均符合。
- 对该轮已知 Word 测试窗格的独立只读元数据检查确认：Document.Permission getter 本身返回 0x80004005，而非返回 Enabled=true。它没有确认“禁止读取”，也没有确认“没有限制”，只是接口调用失败。
- 两次新鲜协调器请求只有2/3单元，返回 InvalidRequest(5)；固定语料只接受预设测试句及其前缀，故碎片可能无法匹配。不是模型HTTP超时，也不是已有建议被UI藏住。

这些证据支持“上屏交付正常、允许回读与校验失败并存、上下文累计中断”，不能推出一个全局读取禁令。

证据文件：`word-fixed-failure-evidence-20261026.json`、`word-native-metadata-20261026-2134.log`及`pipeline-20261026-133136-e614b31d95234879964b39fe57a4640a.jsonl`，均在 `output/LocalTest/`。

## 2. 缓存在哪里消失

### 2.1 上屏文字最初来自输入法自己

`Composition.cpp` 取得 `response.commit`，完成文档写入后，把文字和已提交范围传给 `AutomaticCorrection::Capture`。这一步不是先从 Word 全文“发现”刚才打了什么；本服务本来就知道自己提交的文字。

Word 的接口会影响“是否允许核验这段宿主文字、与旧片段拼接、提交纠错或替换”，却不能让输入法进程从未收到自己计算出的提交字符串。当前丢失发生在后续缓存策略。

### 2.2 累计旧文字前的校验失败，会只留下新片段

`AutomaticCorrection.cpp:192` 起，只有同一上下文、有效设置、10秒内、总长度不超过1024，并且旧范围类型允许、旧范围末端与新范围起点相邻、旧文字回读一致，才复制旧缓存到 accumulated。

**无论累计检查是否成功，随后都会调用 Cancel（约202行）清理原状态。** 若旧范围验证失败，accumulated为空；新文字若独立验证通过，本次只缓存新片段。没有自动向前扩大范围找回丢失的旧文字。

### 2.3 当前片段校验或后续观察失败，整份缓存会清空

Capture 临时设置 `text_` 后立即 Validate；不通过便 Cancel。后续 OnEndEdit→Observe、普通按键校验、120ms周期验证等，也会在 Validate 失败时调用 Cancel。

Cancel（约110行）不仅隐藏提示、停止请求，还释放 context/range/selection，清零 `text_` 的内存并清空字符串。因此“这次不能确认范围”与“丢弃此前全部上下文”目前被绑定成同一动作。

Capture 已经 Cancel 后，OnEndEdit 的 Observe 只会验证仍存在的 context；context为空便无事可做。即使宿主属性稍后恢复，也没有补捕获、恢复旧快照或重读历史的机制。

### 2.4 推理失败也会清空缓存，形成第二次断链

Poll（约855行）只接受 Ok + 当前设置版本 + Suggestions/NoSuggestion/Filtered。固定语料的 InvalidRequest、通信错误、模型失败等会走 WorkerFailed→Cancel，同样清掉文字追踪。

所以现场存在一条有证据支持的连锁路径：

1. 范围属性失败/Word兼容验证失败，完整上屏片段不能保留或旧范围不能累计。
2. 后续偶尔成功时只剩2/3单元片段。
3. 固定语料拒绝不符合样例的片段。
4. 该拒绝又被当作需要清掉全部缓存的失败，下一次仍从新片段开始。

现场日志没有为每一次 Cancel 记录具体调用点，不能据此声称已还原每个片段的精确丢失回调。以上是源码可达、离线可复现、与现场结果相符的机制。

## 3. 为什么 Permission 失败不能叫作 Word 禁读

当前 Word 兼容分支在 GUID_PROP_INPUTSCOPE 的 GetValue 返回 E_FAIL+VT_EMPTY 时，通过特定文档窗格的对象模型核验 Document.ReadOnly、ProtectionType 和 Permission.Enabled。只有全部证据位为511才允许继续，现场为383。

这个额外门槛是**本项目选择的兼容验证策略**，并不是 Word 要求所有输入法先获得的通用读取许可。Permission 是 Office 文档权限设置对象；取得对象失败不等于“Word明确禁止输入法记住输入文字”。

同时，E_FAIL 是未具体分类的错误；不能把它等同 E_ACCESSDENIED、IS_PRIVATE 或密码字段。微软文档中，多值/未覆盖范围的 GetValue 通常应返回 S_FALSE+VT_EMPTY，不能仅凭 E_FAIL 认定是多值范围。

本项目应承担这条兼容分支过度依赖一个现场不可用接口的限制，不能将其描述成已证实的 Word 安全政策。也不能为了提示出现就把未知权限直接当成允许。

## 4. 本次离线因果复现

新增入口：

```powershell
.\output\CorrectionIntegration\x64\CorrectionIntegration.exe <Probe DLL绝对路径> --word-memory-review
```

用真实 Windows TSF 锁访问自有内存文档，不激活TIP、不访问当前Word、不连接已安装协调器；只有模拟失败进程收到编译的合成文本，不调用模型。

| 场景 | 文档与缓存结果 |
| --- | --- |
| 正常提交“这里有”，返回合法NoChange，再提交“错子。” | 文档6单元，缓存6单元：正常无错结果不会让程序忘记上文 |
| 缓存3单元后，模拟输入范围属性失败 | 直接GetText仍能读取自有文档，但程序因ScopeUnavailable将缓存清为0；文档3单元不变 |
| 属性恢复，再提交新3单元 | 文档仍是完整6单元，缓存只有新3单元；属性恢复不会重建旧缓存 |
| 范围一直有效，但模拟推理进程失败 | 文档不变，缓存仍被清空；下次提交重新从新片段起步 |

x64 / Win32 各20项通过，DLL实际卸载通过。日志为 `output/LocalTest/word-memory-analysis-<架构>-20261026.log`。这证明当前程序的缓存清理机制，不宣称是 Word 全部内部行为的完整模拟，也不表示已修复实机问题。

新增缓存读数只导出长度，不导出正文，且只编译进探测DLL。正式/独立安装的生产逻辑没有修改。

## 5. “记忆”还必须分清三种

- **纠错上下文**：本项目当前的 text_，短期、易失、有范围及生命周期限制。自动路径只累计本输入法刚上屏文字，不补取原文档历史或粘贴文字。
- **Rime高频词/用户词典学习**：由Rime自己的commit_notifier、Memory::OnCommit及用户词典事务处理。本项目没有新建重复高频缓存；取消自动纠错缓存不会因此回滚Rime词典。这一轮没有检查个人词库内容，也不宣称某句话必定被Rime学习。
- **模型上下文**：每次请求显式构造system+user消息，发送这次的target/before/after；不是Word自动给模型提供历史，也没有将输入自动训练进模型权重。若前端只保留了3单元，模型收到的便只有该快照，8192-token窗口再大也无法补回未发送的文字。

本轮短句远低于1024单元和模型窗口限制，缺少整句不能优先归因于模型容量。

## 6. 下一步修复设计，应先补证据再改策略

1. 为“旧范围累计”“本次Capture验证”“RecordInput的拼音范围”“OnEndEdit”“新只读锁”分别记录属性HRESULT、VARIANT类型、输入类型、代数、单元数及清理原因。仍不记录正文、文档名或句柄。
2. 区分“明确私密/密码/只读/失焦/文字变化”与“属性暂不可用/推理失败”。前者立即丢弃；后者不能继续展示、发送或替换，但应研究能否保留仍安全有效的追踪快照，或仅短暂暂存本服务刚产生的提交，待新锁下获得明确许可再使用。未知状态不能默认放行或无限重试。
3. 将清理建议/停止推理与丢弃文本追踪拆开。对权限和原文仍可靠的上下文，普通网络/模型故障不必自动丢掉它；同时防止空闲重试循环，不自动重放过期写入。
4. 若证明Word在稍后的编辑阶段稳定提供明确IS_TEXT/IS_DEFAULT，再设计受控捕获时序；单凭现有Trace里的S_OK不够，因为该结构没有记录VARIANT类型和真实输入类型，且子阶段没有独立时戳。
5. 不先改模型、不盲目扩大读取范围、不跳过私密保护，也不再只围绕提示坐标调试。先在自有宿主验证完整累计与取消边界，再计划一次Word定向实测。

本轮仅定位与复现，**未实施上述生产修复、未更新安装、未开启纠错或模型**。

## 参考

- [Microsoft：ITfReadOnlyProperty::GetValue](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfreadonlyproperty-getvalue)：属性值及E_FAIL/S_FALSE含义。
- [Microsoft：ITfRange::GetText](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfrange-gettext)：读取范围文字需要有效读锁，与属性查询是不同调用。
- [Word Document.Permission](https://learn.microsoft.com/en-us/office/vba/api/word.document.permission)、[Office Permission.Enabled](https://learn.microsoft.com/en-us/office/vba/api/office.permission.enabled)：文档权限对象，不是本项目缓存的系统授权开关。
