# WeaseLC 更新日志

## 未发布

### 修复

- 「管理本地模型」支持 Windows 自带的 PowerShell 5.1。已有有效的 PowerShell 路径会继续使用。首次配置时优先查找 PowerShell 7，找不到就使用系统的 5.1。
- 修复 PowerShell 5.1 的脚本编码、绝对路径校验、模块加载和模型子进程启动问题。以前下载的推荐模型可以继续使用。

详见 [PowerShell 5.1 适配记录](weasel-0.17.4/docs/POWERSHELL_51_COMPATIBILITY.md)。
