# TSF 提交与模型衔接测试（2026-10-15）

**后续记录：** 本页保留首次 CLI 回路与修复记录。当天后续已完成请求调度、真实 HTTP 边界、本地 1.7B/4B 部署和双架构 GUI 联调，最新结果见 [本地模型与调度](LOCAL_MODEL_TESTS.md)。其中 4B 另一组语料有 2 项质量失败，未启用日常自动纠错。

## 已验证的链路

```text
测试自建的真实 TSF 文本框
 → 生产开始/插入/结束组合代码（合成响应）
 → 成功结束后检查普通文本输入范围
 → 探测构建专用提交事件（有界复制，不等待模型）
 → 会话/版本过滤 → 后台 IModelClient
 → 独立 Codex CLI 子进程 → 纠错 JSON 校验
 → 结果归属检查；失效后不再使用
```

使用真实 Windows `ITfContext`、编辑锁、组合和自有 `ITextStoreACP`。宿主线程管理器以 `TF_TMAE_NOACTIVATETIP` 启动，没有调用输入法 `ActivateEx`、语言配置激活或注册函数；生产组合代码通过探测导出直接执行，注入合成的服务响应。这**不是从键盘拼音经 Rime 再上屏**的完整路径，也不是在记事本中安装后的输入验收。标志含义依据 Microsoft [`ITfThreadMgrEx::ActivateEx`](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfthreadmgrex-activateex)。

普通构建不含提交捕获注册入口、测试导出或 Codex 测试适配器。正式管理窗口的 CLI 选项仍未开放，日常输入法没有自动模型请求。探测捕获只在测试进程明确注册回调后工作；本轮没有读取用户文档、剪贴板或其他输入法内容。

## 本轮修复

1. **响应快照与单次消费。** `ResponseSlot` 记录实际收到的完整正文；标量结果、短包、奇数字节及新事务不会复用旧正文。TSF 在排队前解析为自有 `ServerResponse`，回调不再读取共享缓冲区。重复消费没有新编辑；普通按键释放路径也捕获自己的新响应。排队期间样式从最新已捕获样式继承，防止后包回退到尚未更新的旧样式。
2. **复用已授予的写锁。** 主响应的开始、插入、结束和内联预编辑直接使用当前写 cookie；不再把这些相互依赖的动作嵌套排队，并把 `TF_S_ASYNC` 当作已经写完。插入/折叠/选区失败及空范围保留实际错误。对失败立即停止后续步骤，不向模型提交失败或未正常结束的组合。
3. **预编辑边界。** `GetRange=S_OK` 但空输出不再解引用；写入/折叠/选区错误向上传递，光标偏移校验长度和实际移动量。开始组合遇到生命周期变化会尝试结束本次捕获对象，不写入新文档服务状态。
4. **异步生命周期。** 原重连代码的分离线程捕获裸 `this`，可能在对象销毁后访问服务。现由后续输入事件重试，不再安排该分离回调。本轮未运行服务启动脚本。
5. **纠错提交边界。** `CorrectionCapture.h` 只对探测构建公开固定容量事件；组合结束成功后检查输入范围，未知/密码/私密不提供正文。`SessionGate` 按生命周期及提交序号取消旧结果；捕获回调异常不传播到输入路径。模型执行不放在编辑锁回调中。

最终快照检查曾先失败：测试包误用 `context` 作为协议名，而真实协议是 `ctx`。修正测试包后，同一生产快照函数验证成功：甲/乙两包分别持有自己的提交与预编辑，原始缓冲区覆盖后仍保持不变，第二次消费被拒绝。这一失败属于测试数据错误。

## 真实 Codex 推理

用户本轮明确授权通过本机已登录的 Codex CLI 对合成文本做真实推理。没有继续沿用此前“仅固定响应”的限制到本轮真实测试，但该授权只用于受控测试，不使正式产品默认开放 CLI。

| 合成 TSF 提交 | 实际返回经验证 | 结果 |
| --- | --- | --- |
| `你好。` | `no_change`，建议数组为空 | 通过 |
| `这里有错子。` | 有建议，首条 `错子 → 错字`，通过原文与阈值校验 | 通过 |

x64 真实回路共 19 项检查、失败 0、实际模型调用 2 次。未知范围和密码范围中同样可以写入自有测试文字，但没有产生模型提交。每次结果核对会话/版本；调用真实的服务失效入口后旧结果不再可用，最后 TSF 宿主清理和 DLL 实际卸载通过。

CLI 继承本机模型配置（只读查询为 `gpt-6.1-sol`、`high`），没有自行调整模型或推理强度；这不是服务端模型版本指纹。进程使用 `--ephemeral --sandbox read-only --output-schema` 和 stdin，按照 [官方非交互接口](https://learn.chatgpt.com/docs/non-interactive-mode)。本适配器禁用 shell、子代理、应用、浏览器、计算机控制及 hooks，清空 MCP 配置，并要求仅返回 JSON；没有提取或复制认证文件。只读沙箱不等同于零文件读取权限隔离，因此适配器还硬性只接受上述两条合成正文、空邻近文本及提交来源，其他文本在进程启动前拒绝。

测试适配器只链接 `CorrectionIntegration`，实现已有 `IModelClient`，复用与 HTTP 模型相同的 `CorrectionInstructions`、`BuildCorrectionInput`、`ParseCorrectionAnswer`。每次调用独立进程、独立 Job；退出/取消/180 秒截止时清理该调用及子进程；stdout 有 64 KiB 上限，stderr 消费后丢弃。没有调用 HTTP 模型服务或使用用户实际输入。

本次日志创建于 14:12:50、结束于 14:16:55，两条样例整个回路约 245 秒，含宿主和 CLI 启动、等待及推理，没有拆分纯模型耗时。CLI 用于联调，不能据此作为逐键实时推理方案。仅两条样例也不能证明准确率、分数校准或性能达标。

真实推理通过后，最后增加/收紧快照自检、预编辑错误保护和捕获生命周期检查；最终源码重新编译，并重跑两架构模拟回路和回归，没有追加真实推理调用。

## 最终回归

| 项目 | x64 | Win32 |
| --- | --- | --- |
| TSF / 传统 IME / Server / Deployer Release 构建 | 成功 | 成功 |
| 普通 DLL 异常边界、类工厂 26 项及实际卸载 | 通过 | 通过 |
| 探测 DLL 异常边界、类工厂 27 项及实际卸载 | 通过 | 通过 |
| 组合引用 18 场景×64，状态 23，绑定/取消 13 | 通过 | 通过 |
| 新增插入/预编辑成功、空输出及错误传播 12 场景 | 通过 | 通过 |
| 模型接口、响应槽、结果归属离线 70 项 | 失败 0 | 失败 0 |
| 快照所有权及真实 TSF 宿主模拟回路 19 项 | 通过 | 通过 |
| 纠错管理窗口隐藏离线自检 | 通过 | 通过 |
| 真实 Codex 推理回路 | 19 项通过，2 次推理 | 未追加真实推理 |

构建仍有原有 `localtime`、整数/指针转换、未使用变量等警告，不是零警告构建。未构建 NSIS 安装包。

## 复现

源码根目录中，使用现有 `env.bat` 或等效 v143 环境；本机 MSBuild 见其他构建文档。

```powershell
$env:VCTargetsPath = 'E:\Microsoft Visual Studio\MSBuild\Microsoft\VC\v170\'
$build = 'E:\Microsoft Visual Studio\MSBuild\Current\Bin\MSBuild.exe'
& $build weasel.sln /t:WeaselTSF /p:Configuration=Release /p:Platform=x64 /p:WEASEL_CORRECTION_PROBE_BUILD=1 /m:1 /nologo /verbosity:minimal
& $build test\CorrectionIntegration\CorrectionIntegration.vcxproj /p:Configuration=Release /p:Platform=x64 /m:1 /nologo /verbosity:minimal

# 默认只模拟；要求 Windows COM/TSF 可用，不需要系统管理员或注册。
& '.\output\CorrectionIntegration\x64\CorrectionIntegration.exe' "$PWD\output\Probe\v3\x64\weaselProbe.dll"

# 仅在得到真实调用授权后运行；可能使用本机 Codex 对应额度。
$cli = (Get-Command codex).Source
& '.\output\CorrectionIntegration\x64\CorrectionIntegration.exe' "$PWD\output\Probe\v3\x64\weaselProbe.dll" --live-codex $cli "$PWD\test\CorrectionIntegration\response.schema.json" "$PWD\test\CorrectionIntegration"
```

开发工具的沙箱可能限制 Windows 集成；本轮采用桌面执行权限，进程没有取得 Windows 管理员令牌或 UAC。本测试文本框隐藏运行，无须用户点击，不会抢焦点。Win32 用匹配 DLL/程序运行模拟模式。收尾删除测试产物，复现前须重建。

## 剩余门槛

- 验证实际按键 → Rime IPC → 快照 → 候选/上屏的快速连打、切换方案和真实应用表现；本轮合成响应不能替代这些。
- 验证真实异步编辑调度和不同宿主；部分写入/开始失败后的组合回收、焦点改变时被取消的旧组合、真实停用/再激活仍需完善。
- 将事件交给正式独立纠错进程，补齐受控 IPC、设置开关、去抖、超时、关闭应用/失效结果和无焦点干扰的建议呈现。目前只有诊断桥，不是日常版自动纠错。
- 上屏文本以外的邻近文本读取、文档版本核对和安全替换尚未接入；没有收集高频词或长期正文。
- 真实 HTTP/TLS/重定向与服务故障验收、本地模型部署及纠错质量评测仍待完成。

本轮收尾已实测完成：八个测试输出/中间目录已删除，全项目探测 DLL 和 ProbeActivation/CorrectionClient/CorrectionIntegration EXE 为 0，临时界面检查日志已删除；探测 CLSID 的 HKLM/HKCU 32/64 位 COM/TIP 根键、输入法列表测试项、相关服务及诊断/演示进程均未发现。原版 COM 路径仍指向两份系统 weasel.dll；WeaselServer PID 21288、启动时间 2026-10-15 13:52:14，搜狗 Broker PID 10764、启动时间 13:52:07 不变。保留源码、文档与普通构建，未安装或替换日常输入法。
