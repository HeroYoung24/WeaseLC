# Word上文缓存：IRM失败定位与显式本机兼容

日期：2026-10-27。安装基线仍为f5f11f1；本报告涉及新的源码、离线验证和待部署产物，没有将其算作真实Word修复通过。

## 缓存为什么没有读入

上屏字符串由Rime的`response.commit`交给Capture，本来已存在输入法内存中。现场LastCommit=7证明这一步正常。后续验证要求Word范围身份、焦点、可编辑状态、原文回读等；当GUID_PROP_INPUTSCOPE.GetValue为空E_FAIL时，又要求Word对象模型给出全部511能力位。

实际缺失的是128：`Document.Permission.Enabled`必须明确为false。10-27四次输入的暂存均在约501–539ms后到期，没有新LastCapture/协调器请求。也就是说文字只进入临时追踪，未成为允许纠错使用的有效缓存，不是模型耗时或UI坐标问题。

本轮对仍保留的测试Word PID34356，使用原生窗格对象进行封闭只读元数据检查，未获取正文/名称/文件路径或改变选区：

| 查询 | 结果 |
| --- | --- |
| 原生对象、Document | 成功；Document为VT_DISPATCH |
| ReadOnly | 严格VT_BOOL false |
| ProtectionType | VT_I4，-1（无编辑保护） |
| Permission名称解析 | GetIDsOfNames=S_OK |
| Permission getter执行 | Invoke=0x80020009（DISP_E_EXCEPTION），EXCEPINFO.scode=0x80004005（E_FAIL），wCode=0，结果VT_EMPTY |

证据`output/LocalTest/word-metadata-detail-x64-20261027.log`。这次具体区分了“名称不存在”和“执行getter抛出异常”；旧动态调用只暴露内层E_FAIL。此独立检查不在Word的TSF编辑锁中，下一轮仍需核对输入法内部新诊断，不能假设两种调用时序完全相同。

Permission属于Office IRM设置对象，不是TSF通用缓存授权。微软说明相关客户端缺失也会让该接口报错。本机System32/SysWOW64的msdrm.dll文件均存在，因此不能直接断言“缺少DLL”就是此次异常原因，也不能由E_FAIL断言文档无IRM限制或Word统一禁止读取。

之前500ms修复针对的是暂时失败后恢复；当前该属性持续无法给出允许证据，等待不会修复组件可用性。真正需要处理的是可选IRM接口与本机自有提交纠错之间的兼容策略。

## 源码修改

### 保留严格模式，新增明确选择的兼容模式

新增`automatic.word_local_compatibility`，默认false；自动提示设置页提供三语言入口及说明。旧v2/v3配置不自动授权，v4要求严格布尔字段。取消子窗口不改变父设置，确认仅改变内存，主窗口保存才持久化。

兼容模式不是“已确认无版权限制”。它仅在用户明确选择、本地模式且自动提示已启用时使用：

- 仍要求精确Word进程/同线程窗格、范围所属上下文、原生Document、ReadOnly=false、ProtectionType=-1、查询后焦点稳定。
- 只认这次观察到的Permission getter异常组合：名称成功、Invoke=DISP_E_EXCEPTION、scode=E_FAIL、wCode=0、无延迟填充错误、VT_EMPTY。名称失败、直接未分类错误、E_ACCESSDENIED、Enabled失败或错误类型均不放行。
- 新能力位512表示这一特定“IRM状态不可用”；895表示其余所需Word能力齐备。诊断ScopeStage22明确标注“Word本机兼容”，不伪装成511/已确认无IRM。
- 仅ScopeUse::LocalCommit允许此路径，内容仍限定本输入法刚提交的相邻片段、受跟踪的拼音范围；不授予NearbyText/全文/历史读取能力。标准原文回读、选区、焦点、设置版本、长度/生命周期与Tab写前写后校验继续执行。
- 显式私密/密码/PIN/只读/键盘禁用/编辑保护/已知IRM限制仍拒绝；光标移动、宿主改文、失焦、设置关闭或版本变化仍取消。在线模式或自动提示关闭时，即便兼容选项为true也不生效。
- 协调器只接受已验证的本机回环模型地址，不启用在线自动纠错。

**边界：IRM状态不可用时无法自动证明文档没有版权限制。用户不应对受版权限制文档启用这一全局选项；需要完全保守处理时保持关闭。** 这是一条显式兼容选择，不是把所有未知失败静默视为允许。它没有修复Word的IRM组件，也没有证明其他版本Word必定适用。

### 具体错误诊断与配套版本

DocumentMetadata保留名称查询/Invoke HRESULT、EXCEPINFO数值、VARIANT类型及延迟填充错误；释放异常字符串，不记录描述/正文等。独立测试版通过40字节`LastWordMetadata`专用通道记录失败成员编号和时戳，每线程最多128次、最多每秒一次；持续不变的旧记录仍须结合时戳判断。观察脚本已支持新记录和Scope22。

设置格式升为v4，协调器协议和管道升为v3。旧协议明确拒绝，防止兼容授权在混用版本时静默丢失；必须成套更新双架构Server/Deployer/TSF。回退旧二进制前，应恢复升级前独立测试SettingsV2快照或显式还原v3配置；旧代码不支持v4时会关闭纠错，不能将六文件回退当成配置降级。

按键派发、Rime学习/高频词、模型参数及现有分阶段Tab替换算法没有改动。没有复制用户词库或安装Office/RMS组件。

## 验证

x64/Win32各：

| 检查 | 结果 |
| --- | --- |
| 缓存、限时暂存、兼容授权与撤销 | 120通过 |
| 提示/Tab/宿主同步及故障回归 | 442通过 |
| 范围策略/Word元数据 | 92/23通过 |
| 正常按键及中断/焦点诊断 | 104/26通过 |
| 设置迁移、模型契约等离线检查 | 145通过 |
| 协调器、兼容开关、数字诊断和旧协议拒绝 | 43通过 |
| 三语言界面确认/取消/默认关闭 | 219通过，图像检查布局无裁切 |
| 离线完整链路 | 23通过；2次模型调用为夹具 |
| DLL实际卸载 | 通过 |
| 独立Release TSF/Server/Deployer | 构建成功 |

自有TSF宿主只替换Word窗格能力证明，实际生产的策略、缓存、编辑锁、回读、选区和Tab流程仍执行。持续属性失败下，关闭兼容仍限时拒绝；开启后3+3累计为整句，Tab修正、ni保留及下一次候选上屏通过。私密/只读/光标移动/改文/能力缺失/在线/关闭自动均有拒绝检查。它不能替代真实Word验收。

日志`output/LocalTest/word-compat-*-20261027.log`；最终摘要`word-compat-final-test-summary-20261027.json`。界面产物`WordCompatUi-final-20261027-x64`及Win32对应目录。仅既有localtime和测试变量遮蔽警告，无构建错误。

## 下一轮实测

1. 核对旧DLL占用，由用户保存并关闭相关应用后成套安装；不强制结束Word/浏览器/桌面进程。保留升级前独立纠错设置快照，供配置回退。
2. Off先确认Word数字/空格选词、单句号和中断恢复。确认实际加载新DLL。
3. 仅在合成测试文档中明确开启Word本机兼容并保存；用Fixed先检查LastCapture是否累计整句、Scope22/895以及新鲜固定协调器结果。不再重复旧严格模式无提示的盲测。
4. 捕获通过再输入ni，验提示/Tab实际改文/拼音保留/后续候选与鼠标中断。失败则按LastWordMetadata具体数值或范围回读结果继续定位。
5. 再回归记事本/Edge既有成功路径及关闭兼容后的隔离；Fixed通过后才用冻结4B。

本轮源码修复完成后准备包，不直接改动已安装f5f11f1。当前测试纠错/固定响应Off，无真实模型、无专用后端运行，Word窗口由用户自行处理。

源码提交`f8f22e6dd0cbfa92734efe3a2829e5ccae138355`；六文件包`output/LocalTest/UpdatePatch-20261027-150517`已完整只读校验，未应用。升级前独立v3设置快照`output/LocalTest/word-irm-settings-before-20261027.bin`（SHA256 `A671E045D4CF08E26BB07740BD727C241233B8D2F37B639719A07797EC0EDDFD`）已保存，未更改实际设置。恢复状态`output/LocalTest/word-irm-compat-state-20261027.json`。收尾确认自有测试进程/专用运行时/18963监听/模型ready均无，原版基线一致，安装DLL仍为f5f11f1的E5C2BA47…。

## 依据

- [Office Permission对象](https://learn.microsoft.com/en-us/office/vba/api/office.permission)：IRM用途以及客户端缺失时可能报错。
- [Word Document.Permission](https://learn.microsoft.com/en-us/office/vba/api/word.document.permission)：返回权限设置对象。
- [TSF GetValue](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfreadonlyproperty-getvalue)：E_FAIL为未具体分类错误；多值/无覆盖范围通常是S_FALSE+VT_EMPTY。
