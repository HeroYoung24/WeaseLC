# 小狼毫本地模型测试环境

已下载并校验：官方 Qwen3 4B Q4_K_M（默认测试基线，约 2.50GB）、Qwen3 1.7B Q8_0（对照，约 1.83GB），以及 llama.cpp b11640 的 Windows x64 Vulkan 便携运行时。没有安装服务或输入法。源地址、固定提交、字节大小、SHA256 在 `deployment.json`，许可证文本在本目录。

## 手动启动

在 PowerShell 中运行，保持终端开启，按 Ctrl+C 停止：

```powershell
& E:\WeaseLC\local-model\Start-LocalModel.ps1 -Variant 4b
```

等待显示 `Ready`，脚本先完成合成请求预热。默认只监听 `127.0.0.1:18963`，占用时退出。停止脚本会结束其子进程。也可使用 `-Variant 1.7b` 做对照；两者默认不能同时启动。

正式手动检查界面：

```powershell
& E:\WeaseLC\weasel-0.17.4\output\WeaselDeployer.exe /correction
```

选择“本地”，填写：

- 接口：`http://127.0.0.1:18963/v1/chat/completions`
- 模型：`weasel-qwen3-4b`
- 建议保留默认提示阈值；只有主动点击“检查文本”才发送本页文字。

这里的兼容接口由本机 llama.cpp 提供；使用本地模型不需要 OpenAI API 密钥或 Codex CLI。

## 当前结论

2026-10-21调参搜索收尾但**未达标**，见[完整报告](../MODEL_TUNING_REPORT_2026-10-21.md)。14组配置、980次有效开发请求：原4B修对9/20；7B虽修对18/20、中位406ms/P95 938ms，却有5/50对照误报、2错改。短思考、Schema和新提示均未形成可采用组合，默认4B/原生产提示/0.90门槛不变，系统纠错关闭。

新增对照资产：Unsloth量化的Qwen3-4B-Instruct-2507 Q4_K_M，以及Qwen官方Qwen2.5-7B-Instruct Q4_K_M两片，均已校验SHA并保存Apache-2.0许可证。可显式使用`Download-Model.ps1 -Variant 4b-instruct`或`-Variant 7b`下载，`Start-LocalModel.ps1`同名参数启动；对应别名是`weasel-qwen3-4b-instruct`、`weasel-qwen25-7b`。`-ThinkingBudget 32|64`仅用于原Qwen3思考对照，默认0，不适用于这两个纯非思考模型。这些都是被拒绝的实验候选，不是推荐启用的配置。新权重约7.18GB保留，模型与下载进程已退出。

2026-10-20新增速度对照：42条同语料中，4B确定性与低温采样的请求中位均312ms、P95均672ms、修对7/14错句；1.7B中位188ms但仅修对1/14。保留4B、temperature=0及既有门槛；另16条验证修对2/6、10对照无误报，仍有明显漏报。见[提示框审查和速度报告](../TAB_DISPLAY_REVIEW_2026-10-20.md)。这是合成请求计时，不含默认250ms去抖或下次输入等待；没有修改系统设置或默认启动模型。

4B 模型在 18 条合成语料中，默认阈值下 4/6 错句显示预期修正、12 条对照未显示错误建议。热身后的请求中位约 266ms，不含 250ms 去抖；不代表真实准确率。另两条候选分别包含一个正确和一个错误的 0.85 分建议，不能靠降低阈值解决。日常自动纠错仍未开放。

详细结果、失败原因、复现及后续任务见 [本地测试记录](../LOCAL_MODEL_TESTS.md)。`final-*.log`、`threshold-analysis-4b.log` 与早期对照日志均为内置合成语料。

本目录是用户授权保留的模型部署资料；本轮模型与测试进程已退出，权重及工作区构建保留，系统纠错关闭。如缺少下载文件，可执行 `Download-Model.ps1 -Variant 4b`，下载脚本使用已有系统代理并校验 SHA256。
