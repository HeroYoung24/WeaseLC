# 纠错模型接口与管理界面（2026-10-13）

> 2026-10-15 新增探测构建的 TSF 提交桥及诊断专用 Codex CLI 适配器，真实合成样例回路通过；正式管理窗口仍不开放 CLI。下文保留 10 月 13 日阶段记录，最新结果见 [衔接测试](CORRECTION_INTEGRATION_TESTS.md)。

本阶段完成模型适配接口和独立管理窗口。自动读取宿主正文、实时纠错提示及模型质量验收仍未完成。用户确认搜狗切换在重启后已恢复；本阶段没有激活、注册或替换输入法。

## 打开界面

在源码目录运行工作区程序，不使用已安装旧版本的菜单来判断新界面。

```powershell
# 推荐先看离线演示：固定模拟响应，不调用模型，也不保存设置。
Start-Process -FilePath 'E:\WeaseLC\weasel-0.17.4\output\WeaselDeployer.exe' -ArgumentList '/correction-demo' -WindowStyle Normal

# 实际配置页：只有点击“检查文本”才会调用用户配置的 HTTP 服务。
Start-Process -FilePath 'E:\WeaseLC\weasel-0.17.4\output\WeaselDeployer.exe' -ArgumentList '/correction' -WindowStyle Normal
```

Win32 程序在 `output/Win32/WeaselDeployer.exe`。本轮首次从沙箱启动演示，用户看不到窗口；改为允许桌面交互的执行方式后，用户确认界面可见、检查显示相应模拟建议，并退出。若从开发工具启动仍不显示，应使用桌面执行权限或从资源管理器/普通终端启动；不要把不可见窗口当作输入法故障。这个结果只证明本次纠错控制窗口呈现，不追溯证明旧 TSF 测试窗口的根因。

设置包含：关闭/本地/联网、接口地址、模型名、仅当前窗口有效的访问密钥、2–60 秒超时、JSON 输出兼容开关、保守/标准/积极敏感度、自定义最低分数、手动检查与取消、结果及状态。提示时机显示为预留且禁用；手动检查总是在返回后显示，不假装有自动纠错。

本地、联网分别保存配置；修改地址或模型名时关闭自定义阈值，避免旧模型分数静默沿用。自定义分数及默认 90/80/65 都是实验性过滤参数，**不是正确概率，尚未校准**。关闭、忽略或无法解析的返回不得称为“全文无错”。提示皮肤、候选框跟随和宿主原文替换留待后续。

实际设置使用当前用户 `Software\Rime\Weasel\Correction` 的 `SettingsV2`，一次写入有版本的配置对象。访问密钥、联网确认、原文和建议不写入配置，也不打印日志。联网确认每次打开/切换配置后需重新选择。离线演示的“关闭演示”只关闭，不持久化；本轮自检和演示结束后该设置注册键仍不存在。

## 架构与接口

```text
管理界面（独立 WeaselDeployer 进程）
  └─ 单个后台检查任务 + 版本/文本快照校验
      └─ IModelClient::Check(config, request, cancellation)
          ├─ Chat Completions 适配器
          │   └─ IModelTransport::Post → WinHTTP / 离线模拟传输
          └─ CodexCliReserved（预留，调用即拒绝；不启动 CLI）
      └─ provider-independent JSON 校验 → 建议或明确失败状态
```

公共契约见 `include/CorrectionModel.h`。`CheckRequest` 分别持有目标、前后邻近文本，标记来源、会话、文本版本和安全许可；目标最多 1024 UTF-16 单元、每侧最多 256。超限直接拒绝，不偷偷截断。界面目前只提交本页手动文本，邻近文本为空；未来宿主接入不能省略安全范围判断。

关闭、已取消、未知/受保护和非法输入在发送前拒绝。单窗口最多一个在途任务，重复点击不排队；编辑文本/配置、切换模式、取消或关闭清除旧结果。后台线程不持有 HWND；UI 轮询完成状态并核对会话、版本和原文快照。取消后立即不再显示该结果，但当前同步 WinHTTP 调用可能等待该阶段超时才返回；不是立即中断网络请求，也不能撤回已发送的数据。超时按阶段设置，阶段间检查总预算；尚不是严格的端到端实时截止机制。

本地模式只允许 `127.0.0.1`、`::1` 或 `localhost`，后者规范为数字回环；不使用代理。联网模式必须 HTTPS 且本次确认。所有模式拒绝 URL 用户名/密码、查询串/片段，禁止自动重定向、Cookie 和自动认证，不跳过 TLS 证书校验。响应最多 64 KiB，JSON 深度最多 16；拒绝截断、拒绝内容、格式不符、超量、分数越界、无匹配/多次匹配的原文及重叠建议。只输出供用户参考的结果，不自动替换宿主文字。

### HTTP 请求

地址需为完整 `/chat/completions` 端点，初始本地地址 `http://127.0.0.1:1234/v1/chat/completions` 只是可修改的占位地址；模型名初始为空，不代表本机已运行服务。

请求包含 `model`、`messages`、`stream:false`、`temperature:0`、`max_tokens:768`，可选择 `response_format:{type:"json_object"}`。系统提示要求 JSON；用户正文先编码为独立 JSON 数据。检查响应的 `choices[0].message.content` 和 `finish_reason:"stop"`。本机兼容服务必须支持这些字段；API 名称兼容并不保证模型行为或纠错输出合格。依据 [Chat Completions 官方接口](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)；不是绑定 OpenAI 官方服务器或强制使用 API Key。

### 与传输无关的纠错结果 v1

```json
{
  "version": 1,
  "decision": "suggestions",
  "suggestions": [
    {"original": "错子", "replacement": "错字", "reason": "同音字", "score": 0.95}
  ]
}
```

没有建议返回 `decision:"no_change", suggestions:[]`。最多 8 条；原文必须在目标中唯一出现。客户端自己推导 UTF-16 位置，不信任模型输出坐标。

`CorrectionInstructions()`、`BuildCorrectionInput()`、`ParseCorrectionAnswer()` 不依赖 Chat Completions 信封。未来 Codex CLI 适配器实现 `IModelClient`，可复用输入、结果和 UI，不必伪造 HTTP 响应。CLI 启动、认证、进程取消、超时和结构化输出验收均未实现。`Provider::CodexCliReserved` 没有可选 UI 开关，也没有读取登录凭据的代码。

本轮只由 Codex 编写固定测试响应，**没有执行 `codex exec` 推理，也没有调用真实 HTTP 模型**。曾只读核对 CLI 已存在且登录状态为 ChatGPT；这不证明推理可用或有剩余额度。[官方非交互文档](https://learn.chatgpt.com/docs/non-interactive-mode)说明未来可复用 CLI 登录并使用结构化输出。

## 验证结果与限制

| 检查 | x64 | Win32 |
| --- | --- | --- |
| TSF 宿主异常处理边界 | 通过 | 通过 |
| 类工厂 27 项、引用及实际 DLL 卸载 | 通过 | 通过 |
| 18 个范围引用场景×64、23 个状态、13 个绑定/取消/清理场景 | 通过 | 通过 |
| 模型接口离线 56 项 | 失败 0 | 失败 0 |
| 简体/繁体/英文资源窗口、后台模拟结果、过期取消、恢复默认、关闭 | 通过 | 通过 |
| 普通 WeaselDeployer 构建 | 成功 | 成功 |
| 用户桌面离线演示 | 可见、显示模拟提示并退出 | 未单独人工验收 |

没有 Windows TSF 激活/注册测试、实际中文上屏、本地模型、外部 API 请求或模型质量评测。WinHTTP 网络实现仅编译，未做真实网络/服务集成验收；离线注入验证协议、边界、错误和取消状态，不能证明实际超时、证书与重定向行为。设置编解码完成离线验证；本轮没有写入用户设置做注册表持久化实测。

原主输入路径的共享 IPC 响应缓冲区、部分子编辑错误传播、真实停用/再激活和宿主组合残留仍待验证。不要将本阶段接口/GUI 完成写成原系统安装故障或真实应用长期共存已经修复。

## 复现离线检查

```powershell
$env:VCTargetsPath = 'E:\Microsoft Visual Studio\MSBuild\Microsoft\VC\v170\'
& 'E:\Microsoft Visual Studio\MSBuild\Current\Bin\MSBuild.exe' test\CorrectionClient\CorrectionClient.vcxproj /p:Configuration=Release /p:Platform=x64 /m:1 /nologo /verbosity:minimal
& '.\output\CorrectionTests\x64\CorrectionClient.exe'
& 'E:\Microsoft Visual Studio\MSBuild\Current\Bin\MSBuild.exe' weasel.sln /t:WeaselDeployer /p:Configuration=Release /p:Platform=x64 /m:1 /nologo /verbosity:minimal
Start-Process -FilePath "$PWD\output\WeaselDeployer.exe" -ArgumentList '/correction-selftest' -Wait -PassThru -WindowStyle Hidden
```

本机若工具链发现环境不正确，可只在构建子进程中移除 PATH，按 `test/ProbeActivation/README.md` 的既有方式运行。Win32 换平台参数。GUI 自检不显示窗口、不发网络、不保存设置；只创建自己的隐藏控件，日志只含检查名、布尔值及资源语言，不含正文。

本轮收尾后测试 EXE、探测 DLL 及六个测试输出/中间目录清理；保留源码、文档与普通控制程序。上述内容为 10 月 13 日记录；10 月 15 日已补齐响应快照并执行授权的 CLI 与本地 HTTP 实测，详见 [TSF 衔接记录](CORRECTION_INTEGRATION_TESTS.md) 和 [本地模型/调度记录](LOCAL_MODEL_TESTS.md)。正式 GUI 中 Codex CLI 入口仍未开放。
