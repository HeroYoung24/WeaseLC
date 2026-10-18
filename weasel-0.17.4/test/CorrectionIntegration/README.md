# 真实 TSF 文本写入与模型回路

2026-10-18：`--automatic` 扩展至每架构 139 项，包括有/无提示时鼠标移出预编辑或拖选跨边界，枚举确认宿主旧组合结束，并验证新位置可继续输入与提交；仍不激活 TIP。未激活测试服务绑定自有宿主客户端 ID，确保延迟清理实际执行。详见 [吞键修复与测试](../../docs/INTERRUPTED_INPUT_FIX_2026-10-18.md)。

自动提示新增 `--automatic`（双架构各 46 项）和 `--automatic-local <WeaselDeployer.exe 绝对路径>`（双架构各 16 项）模式：在独立宿主验证提示、真实键盘事件接收器的 Tab 接受、保留预编辑、拒绝/过期写回和独立模型进程。具体行为、限制及命令见 [自动提示说明](../../docs/AUTOMATIC_CORRECTION.md)。这些模式也不激活或注册 TIP，不采集实际输入。

`CorrectionIntegration.vcxproj` 支持 Release x64/Win32。只加载匹配的探测 DLL，以 `TF_TMAE_NOACTIVATETIP` 创建自己的 TSF 宿主；不注册、激活或安装任何输入法，不与现有 WeaselServer 建立会话。

默认模式：快照所有权自检、真实上下文/编辑锁/组合写入、普通字段提交、未知/密码字段跳过、固定响应校验和结果失效，19 项通过。普通/拼音按键与 Rime 响应未包含在该模式。

`--live-codex <codex.exe> <schema> <working-directory>`：需要用户授权，会启动本机 CLI 做真实推理。只允许内置两条样例 `你好。`、`这里有错子。`，拒绝其他文本、邻近文本及非提交来源。适配器只存在于本测试工程，未链接 Deployer 或普通输入法。不得用该开关采集真实用户输入。

本轮真实模式仅在 x64 执行一次，2 次模型请求、19 项通过；Win32 完成模拟回路。数据流、完整复现与剩余边界见 [`CORRECTION_INTEGRATION_TESTS.md`](../../docs/CORRECTION_INTEGRATION_TESTS.md)。

后续扩展：现在默认模拟模式 23 项，捕获事件通过 `RequestQueue` 的 250ms 去抖、单执行/最新待执行槽与 10 秒展示截止时间。

`--local-http <完整回环chat/completions地址> <模型名>`：对内置 10 条合成语料进行真实 HTTP 推理，包含 3 个错句和 7 个对照句；不接受外部正文。最后可追加 `--holdout`，改用 8 条另一组样例。未知/密码字段均不提交。检查严格区分结果格式、归属/时效和纠错质量，质量失败会使程序返回非零；完整输出保留模型耗时及合成建议，不把联网成功称为质量达标。文档见 [`LOCAL_MODEL_TESTS.md`](../../docs/LOCAL_MODEL_TESTS.md)。
