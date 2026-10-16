# WeaseLC

基于 Weasel 0.17.4 的中文上下文纠错实验项目。仓库从已有修改建立初始快照并持续迭代，不包含 Weasel 上游历史，也不能回退到建立本仓库之前的中间状态。

- [源码、原项目说明和许可证](weasel-0.17.4/README.md)
- [开发进度](weasel-0.17.4/PAUSED_PROGRESS.md)
- [任务清单](weasel-0.17.4/docs/CORRECTION_TASKS.md)
- [本地模型测试报告](weasel-0.17.4/docs/LOCAL_MODEL_TESTS.md)
- [纠错操作与用户习惯](weasel-0.17.4/docs/CORRECTION_REVIEW_AND_LEARNING.md)
- [自动提示、Tab 纠错与布局设置](weasel-0.17.4/docs/AUTOMATIC_CORRECTION.md)
- [纠错鲁棒性与交互检查](weasel-0.17.4/docs/CORRECTION_ROBUSTNESS.md)
- [模型启动和下载说明](local-model/README.md)
- [Git/GitHub 使用说明](weasel-0.17.4/docs/GIT_GITHUB_SETUP.md)

## 仓库布局

仓库根目录是 `E:\WeaseLC`，源码位于 `weasel-0.17.4`。模型下载/启动脚本、版本摘要和许可证文本在 `local-model` 中；权重、运行时、日志及构建输出不纳入版本控制。源码自带的安装脚本、资源和必要的上游随附二进制予以保留。

`librime`、`plum` 为上游子模块，主仓库记录其精确提交，不复制其历史或本机构建目录。有效的子模块配置是仓库根的 `.gitmodules`；源码内的原始同名文件保留作为上游源码的一部分。重新克隆后，在仓库根运行：

```powershell
git submodule update --init --recursive
```

子模块中的未跟踪构建文件不显示为主仓库修改；对已跟踪源码或依赖提交的改动仍会显示。构建依赖安装与本机配置见 [构建基线](weasel-0.17.4/docs/BUILD_BASELINE.md)。
