# 本机独立测试

适用本次用户明确授权的日常电脑测试。先读 [风险与验收范围](../../docs/LOCAL_DEPLOYMENT_RISK.md)。虚拟机包仍用于干净客体，不直接用于本机覆盖安装。

1. MSBuild Release x64 与 Win32：`weasel.sln /t:WeaselTSF,WeaselServer,WeaselDeployer /p:WEASELC_LOCAL_TEST_BUILD=1 /p:WEASEL_CORRECTION_PROBE_BUILD=0`；另构建 Release x64 `test\LocalDeployment\ProfileCheck.vcxproj`。两个宏不可同时启用。默认普通构建未改变。
2. `Prepare.ps1` 将独立输出、对应 rime.dll 与公开 data 复制到新的 `output\LocalTest\Deployment`，生成逐文件哈希。不会重新使用已有暂存目录。
3. UAC 下执行 `Install.ps1 -Action Install -ValidateRoundTrip`；或用 `Run-Elevated.ps1` 记录结果。只写入新的 Program Files 目录、新的两架构 COM/TIP 和机器设置根。简体 Profile 的机器默认可用属性为启用，不设为默认输入法；独立进程核验启用状态。遇到已有本机测试状态即退出，避免原位更新已加载 DLL。早期已安装但不可见的版本可用 `Install.ps1 -Action EnableProfile` 修复其注册元数据，无需替换 DLL。
4. 普通用户执行 `Start.ps1` 或双击 `Start.cmd`：新建独立用户词库并部署简体拼音，启动独立后端。单独验证后可用 `Start.ps1 -EnableInputMethod` 添加当前用户输入项；此选项会重启自己的后端，并要求先构建 x64 `ProfileCheck.vcxproj`。辅助工具只启用本用户新 Profile、通知设置变化，不激活 TIP 或更改默认项。配置查询成功不代表切换界面可见。
5. 在新记事本中选择“WeaseLC 本机测试”，检验 `nihao`、选词、Shift、空格、Enter、Esc、切换回搜狗。纠错仍默认关闭；基本输入通过后再单独启用本地模型和自动纠错。请勿在未验收阶段编辑重要文档。
6. `Rollback.ps1` 先移除当前用户输入项并停止自己的后端，再请求 UAC 注销新身份。结果见 `output\LocalTest\elevated-Unregister.json`。程序文件和独立词库保留；不杀死宿主进程、不安排重启删除。

`BackendCheck.vcxproj` 的两架构只向独立管道发送固定合成 `nihao ` 并比对提交 `你好`；不激活 TSF、不注入键盘。类工厂检查使用原 `ProbeActivation --class-factory/--load-only`，DLL 为独立正式代码构建，且无探测导出。

当前状态：已安装及演练注册回退，后端合成转换通过；修复 Profile 机器可用属性后，用户确认列表可见、中文输入和搜狗正常。自动纠错/多应用/Tab 与鼠标边界仍待本机验收。以后重启用 `Start.cmd` 启动独立后端，无自启动。
