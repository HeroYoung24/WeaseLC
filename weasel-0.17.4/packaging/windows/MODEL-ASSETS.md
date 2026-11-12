# 推荐本地模型的来源和许可

WeaseLC 的安装包提供下载和启动脚本。权重与推理运行时由用户在「管理本地模型」确认后分别从上游下载，下载脚本校验固定的 SHA-256。

| 文件 | 固定来源 | 许可原文 |
| --- | --- | --- |
| Qwen3-4B-Q4_K_M.gguf | [Qwen/Qwen3-4B-GGUF，bc64014](https://huggingface.co/Qwen/Qwen3-4B-GGUF/tree/bc640142c66e1fdd12af0bd68f40445458f3869b) | Apache-2.0，见 `Qwen-4B-LICENSE.txt` |
| llama-b11640-bin-win-vulkan-x64.zip | [llama.cpp b11640](https://github.com/ggml-org/llama.cpp/releases/tag/b11640) | MIT，见 `llama-LICENSE.txt`。压缩包内的 LLVM OpenMP 许可随运行时保留 |

PowerShell 7 由用户单独安装或选择已有版本。它是运行脚本的外部依赖。 [Microsoft 安装说明](https://learn.microsoft.com/powershell/scripting/install/installing-powershell-on-windows)
