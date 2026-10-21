# 纠错协调器定向测试

2026-10-21：默认接口组扩至35项，加入无文本/无推理的Health查询、错误与成功恢复、较早慢请求不能覆盖较新状态、连续24次无睡眠开关配置的版本失效。`--supervisor`为19项，增加额度耗尽后的显式恢复、重复恢复合并与停止后拒绝恢复。`--service-control`为16项，通过PID专属管道验证新管理接口身份、版本、操作、断连、恢复和挂起客户端停止。正式后端构建、隐藏界面及302项自动回归见[报告](../../docs/SERVICE_STATUS_REVIEW_2026-10-21.md)。以下保留早期检查说明，数量以本段为准。

2026-10-20：`BrokerCheck.exe --supervisor` 新增16项自有子进程监护测试，覆盖异步启动、崩溃恢复、连续失败耗尽、缺失程序、退避取消、稳定运行恢复额度、启动后立即退出与Job清理。无需配置模型或注册输入法。

`BrokerCheck.exe --supervisor-process <独立版WeaselDeployer.exe绝对路径>` 有5项真实子进程检查：认证设置端点可用、结束本测试自己的子进程后自动恢复、次数与退出码对应、父管道EOF退出及端点移除。不调用模型、不修改用户设置。不能与占用同一纠错管道的其他测试或已启动专用后端并行运行。

不注册、激活输入法，不读取用户文档，固定响应仍经过正式解析与保留词过滤。

## 构建与运行

用 MSBuild Release x64/Win32 构建 `BrokerCheck.vcxproj`；运行对应 `output/BrokerTests/<架构>/BrokerCheck.exe`。

每架构 26 项覆盖：设置、范围诊断元数据、固定响应、程序身份、版本/长度/协议、错误/无建议/过滤、关闭、异步取消、配置变更、并发上限、忙时设置读取、11 秒慢响应、退出和设置等待超时。每次只创建 PID 专用新注册表根，结束后删除。需在正常用户环境运行，不能与其他 BrokerCheck 或已启动的独立测试协调器争用同一会话管道。

`BrokerCheck.exe --listener-lifecycle` 另有 12 项检查：实际 Windows 管道的接受前断连（空连接/单向元数据）、17 个连接触及 16 工作线程边界、100 次快速断连、随后模型返回、退出、致命监听失败可观察与重启。接受前暂停/抛异常钩子仅在本测试项目的 `WEASELC_BROKER_TEST_HOOKS` 构建中存在，正式程序没有此接口。

`BrokerCheck.exe --process-on <独立版 WeaselDeployer.exe 绝对路径>` 在已开启固定响应的专用设置下，验证实际子进程启动、固定推理、父管道 EOF 正常退出及端点消失；不修改设置，不激活输入法，不读取文档。未部署设置时使用下述 `--process`。

`BrokerCheck.exe --process <独立版 WeaselDeployer.exe 绝对路径>` 只在独立纠错设置缺失时验证实际协调子进程启动、默认关闭、stdin EOF 正常退出及端点清理；不创建设置或调用模型。被测 Deployer 目录需有对应架构 rime.dll。

## 记事本包对照

`Run-PackageMatrix.ps1 -Pythonw <Pythonw.exe 绝对路径>` 启动专用无界面合成后端，并使用微软 Invoke-CommandInDesktopPackage 运行客户端，覆盖普通/MSIX、x64/x86、开启/关闭共八项。

通过条件：同一阶段四种客户端读取相同版本；包内直接读取临时设置失败，但管道成功；开启返回固定建议，关闭明确拒绝；总共四次模型夹具调用；控制视图收到元数据；临时设置和测试进程正常清理。输出在新的 `output/BrokerTests/matrix-<随机值>/` 中，不含正文或凭据。

`RunClient.py` 仅启动工作区 BrokerTests 内的编译客户端，关闭控制台显示；没有通用命令或文档读取能力。

实际 TSF 新链路还由 CorrectionIntegration `--automatic` 测试：新增模式从真实协调器读取设置、禁止回复注入，再验证提示与 Tab。焦点/候选锚点仍为模拟，真实记事本需要另行验收。
