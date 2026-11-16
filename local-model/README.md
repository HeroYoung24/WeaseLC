# WeaseLC 本地模型

## 从应用准备模型

1. 当前开发版会优先使用有效的已保存路径，或从系统注册、标准安装位置及 `PATH` 查找 PowerShell 7。找不到时，自动使用系统自带的 Windows PowerShell 5.1。`0.1.0-beta.1` 安装包需要 [PowerShell 7](https://learn.microsoft.com/powershell/scripting/install/installing-powershell-on-windows)。
2. 打开 WeaseLC 的「管理本地模型」，点「下载推荐模型」并确认。下载约 2.53 GB，建议预留至少 3 GB。校验完成后模型目录会预填为 `%LOCALAPPDATA%\WeaseLC\Models\Qwen3-4B`。
3. 点「启动本地模型」。推荐运行时需要可用的 Vulkan 设备。看到「已就绪」后，可关闭窗口让模型继续在后台运行，或用「停止并退出」停止模型。
4. 从语言栏开启本地自动纠错，或在「管理纠错」手动检查一段文字。

已有完整目录时，可以选择含 `Start-LocalModel.ps1`、`runtime\llama-server.exe` 和 `Qwen3-4B-Q4_K_M.gguf` 的目录。也可在「管理纠错」填写自行管理的本地接口和模型名。

## PowerShell 与数据

`pwsh.exe` 是 PowerShell 7，`powershell.exe` 是 Windows PowerShell 5.1。WeaseLC 在独立进程中执行 `Start-LocalModel.ps1` 和 `Download-Model.ps1`，跳过个人启动配置，遵循当前脚本执行策略。已选择的 PowerShell 与模型目录会记忆。使用 5.1 启动已有模型时，会调用随应用更新的启动脚本。卸载保留 PowerShell 和模型文件，取消保留偏好会清除路径记录。

推荐模型只监听 `127.0.0.1:18963`，模型名为 `weasel-qwen3-4b`。模型下载和预热不读取输入框正文，自动纠错可单独开启或关闭。

## 从仓库手动准备

在已克隆的仓库根目录运行：

```powershell
pwsh -NoProfile -File .\local-model\Download-Model.ps1 -Variant 4b
pwsh -NoProfile -File .\local-model\Start-LocalModel.ps1 -Variant 4b
```

使用 Windows PowerShell 5.1 时，把命令开头的 `pwsh` 换为 `powershell.exe`。第一条下载和校验资产，第二条启动模型。保持终端打开，按 Ctrl+C 停止。使用应用管理模型时，请从「管理本地模型」启动。

## 来源与许可

- Qwen3-4B-GGUF Q4_K_M，许可为 [Apache-2.0](Qwen-4B-LICENSE.txt)。 [固定上游版本](https://huggingface.co/Qwen/Qwen3-4B-GGUF/tree/bc640142c66e1fdd12af0bd68f40445458f3869b)
- llama.cpp b11640，许可为 [MIT](llama-LICENSE.txt)。Windows Vulkan 压缩包内的 LLVM OpenMP 许可随运行时保留。 [固定上游版本](https://github.com/ggml-org/llama.cpp/releases/tag/b11640)

[资产版本及 SHA-256](deployment.json) · [历史开发与调参](../weasel-0.17.4/docs/archive/LOCAL_MODEL_DEVELOPMENT.md)
