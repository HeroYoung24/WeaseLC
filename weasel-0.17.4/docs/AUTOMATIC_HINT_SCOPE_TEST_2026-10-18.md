# 自动提示实机复测：范围检查阻断（2026-10-18）

## 实测结论

重启清理完成后，从 `1e50d3f` 源码重建独立 LocalTest，准备 200 文件哈希清单，经 UAC 安装及注册回退演练、跨进程启用验证。未更改默认输入法，未替换原版 DLL。

用户确认中文选词、半截拼音后点击空白再输入及退格均正常。随后启用既有本地 Qwen3-4B Q4_K_M，自动提示开启、默认分数 0.90、下一次组合提示、Tab 开启。用户实际输入“这里有错子。”并开始 `ni`、保持前台等待后反馈：**没有 Tab 提示**。自动提示和 Tab 验收仍未通过。

本轮提前启动无正文元数据观察器，每 100ms 读取状态变化；在用户回到聊天前捕获三条：

| 本地时间 | 自动阶段 | 焦点阶段 | HRESULT |
| --- | --- | --- | --- |
| 17:15:23.821 | ScopeUnavailable（4） | Ready（0） | 0x00000000 |
| 17:15:36.581 | ScopeUnavailable（4） | Ready（0） | 0x00000000 |
| 17:15:37.822 | ScopeUnavailable（4） | Ready（0） | 0x00000000 |

因此，**本轮在范围检查处停止，焦点检查已经通过；不是据切回聊天后的失焦记录推断原因**。验证发生在模型工作进程启动之前，不能归因为模型不给建议或候选 UI 没画出来。此处 HRESULT 是旧版总状态的值，不能当作范围属性 API 全部成功的证据。

尚不能断言记事本返回 `IS_DEFAULT`：原版粗粒度范围结果无法区分属性缺失、空值、接口失败、默认或特殊类型。微软对 [ITfInputScope 获取方式](https://learn.microsoft.com/en-us/windows/win32/api/inputscope/nn-inputscope-itfinputscope)及 [InputScope 枚举](https://learn.microsoft.com/en-us/windows/win32/api/inputscope/ne-inputscope-inputscope)分别有定义，不能把“没有范围信息”直接视为明确文本范围。

## 本轮追加的源码改进

- 提取共享 `CorrectionScope.h`，在不读取正文的情况下区分属性查询、缺失属性、读取失败、VARIANT 类型、接口、范围查询、空/过长列表、非白名单和保护字段。
- 仍只接受明确的 `IS_TEXT`；默认、缺失、URL、未知等保持拒绝。密码、私密及三类 PIN 明确标记为受保护；混合列表中的保护类型优先。没有为了显示提示绕开范围校验。
- 新增 `Correction\Diagnostics\LastScope`，固定 20 字节，仅版本、阶段、HRESULT、InputScope 枚举及 VARIANT 类型，无正文、应用名、窗口标题或指针。相同结果不重复写入，子键更新不改变设置根时间戳。现有 24 字节 `LastAutomatic` 不改格式。
- 管理窗口在范围阻断时补充具体阶段；合成自动测试不写实际产品诊断。

新增诊断版本**尚未重新部署**，本轮实机证据来自前一版。下一轮必须读取实际 `LastScope` 后决定兼容方案；不要将“新增诊断”表述为“自动提示已修好”。

## 回归结果

x64、Win32 每架构：

| 项目 | 结果 |
| --- | --- |
| 新范围检查（生产共享函数） | 28 项，0 失败 |
| 焦点及诊断格式、临时存储、设置时间戳 | 26 项，0 失败 |
| 真实 TSF 合成宿主自动提示/Tab | 160 项，0 失败 |
| 既有中断恢复 | 69 项，0 失败 |
| 组合引用/结果/绑定/文本 | 18×64、23、13、12，0 失败 |
| 三语言设置界面 | 189 项，0 失败 |
| 正式类工厂与实际卸载 | 26 项通过，实际卸载通过 |
| 正式及探测 TSF / Deployer 构建 | Release 双架构成功，保留既有警告 |

真实 TSF 合成测试新增默认、私密和三类 PIN 字段，确认不能展示建议、接管 Tab 或修改原合成文字；不激活/注册 TIP。该结果不代替实际记事本、Rime 按键或真实模型提示验收。

## 收尾与恢复

用户已切回搜狗并关闭记事本、管理窗口。关闭独立自动纠错，停止模型、启动器及专用后端，UAC 撤销测试 COM/TIP/设置，删除独立词库和日志。原版 DLL 哈希、COM 路径及机器配置与本轮开始一致；无测试进程或模型监听。

**物理清理仍有一个文件未完成**：`C:\Program Files\WeaseLC-LocalTest\weaselx64.dll` 被 ChatGPT、Explorer 和 Edge 加载。没有结束宿主或强制卸载；旧注册已移除，不原位覆盖这个文件。方便时重启 Windows，再执行精确 Purge，随后安装新诊断包。

下一轮：先复核基本输入，再开启本地自动提示；保持测试记事本前台，观察器同时读取 `LastAutomatic`（24 字节）和 `LastScope`（20 字节）。依据范围阶段、HRESULT、枚举值选择修正路径，再验证提示、Tab 保留拼音及鼠标中断。

本轮证据在 `output/LocalTest/`：`hint-acceptance-baseline.json`、`hint-tested-manifest.json`、`hint-user-result.json`、`hint-diagnostic-trace.jsonl`、`hint-model*`、`hint-test-final-state.json`、`purge-result.json`。新增离线回归见 `output/hint-scope-*`。启动预热 3549ms；终止模型产生的退出 -1 是本轮主动收尾，不是用户输入时模型崩溃。
