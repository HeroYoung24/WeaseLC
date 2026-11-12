# WeaseLC

**为 Windows 小狼毫加入本地模型纠错。** 用熟悉的 Rime 方案打字，在候选框旁查看纠错建议，按 Tab 采纳。

WeaseLC 是基于 [小狼毫 Weasel 0.17.4](https://github.com/rime/weasel/tree/0.17.4) 的独立社区项目。输入引擎来自 [Rime / librime](https://github.com/rime/librime)，输入方案和词库通过 [东风破 plum](https://github.com/rime/plum) 管理。感谢 RIME Developers、各词库作者及上游贡献者。

## 功能

- 保留小狼毫的输入方案、候选框配色和本地用户词典，可与原版小狼毫共存。
- 对支持的输入框中由 WeaseLC 新上屏的文字提供本地纠错。Tab 采纳时保留后续标点及当前拼音，Esc 可忽略当前建议。
- 可选连续提示，按顺序处理多处建议，可调整显示时长、位置及敏感度。
- 可按应用暂停，也可从语言栏只读检查当前输入框的兼容性。受保护或无法确认的输入框保留普通输入。

## 开始使用

1. 在 x64 Windows 10 或 11 上安装适用的 [WeaseLC Release](https://github.com/HeroYoung24/WeaseLC/releases)，选择输入方案和配色，再用 Win+空格切换到 WeaseLC。
2. 想使用纠错时，从开始菜单或语言栏打开「管理本地模型」。可以下载推荐模型，也可以选已有目录。下载约 2.53 GB，启动推荐模型需要 PowerShell 7 和可用的 Vulkan 设备。
3. 模型显示「已就绪」后，从语言栏开启本地自动纠错。也可在「管理纠错」先手动检查一段文字。自动纠错默认关闭。

## 默认模型

| 设置 | 默认值 |
| --- | --- |
| 模型 | Qwen3-4B-GGUF，Q4_K_M 量化 |
| 推理运行时 | llama.cpp b11640，Windows x64 Vulkan |
| 本地接口 | `http://127.0.0.1:18963/v1/chat/completions` |
| 模型名 | `weasel-qwen3-4b` |
| 推理与提示 | 温度 0、关闭思考，默认建议分数门槛 0.90 |

模型按需下载。日常普通输入使用 Rime，手动检查可连接用户配置的本地或在线服务。 [模型准备与排错](local-model/README.md) · [自动纠错说明](weasel-0.17.4/docs/AUTOMATIC_CORRECTION.md)

## 开发与贡献

源码在 `weasel-0.17.4/`。克隆后运行 `git submodule update --init --recursive`。可从[文档索引](weasel-0.17.4/docs/README.md)查阅[构建基线](weasel-0.17.4/docs/BUILD_BASELINE.md)与测试报告。欢迎提交可复现的合成测试样例。 [贡献指南](CONTRIBUTING.md)

## 许可证

WeaseLC 的主程序沿用 Weasel 的 [GNU GPL v3](LICENSE)。上游版权、组件许可和模型资产来源见 [第三方声明](weasel-0.17.4/packaging/windows/THIRD_PARTY_NOTICES.md)、[模型许可](local-model/README.md#来源与许可) 和 [发布材料清单](weasel-0.17.4/docs/RELEASE_SOURCES.md)。
