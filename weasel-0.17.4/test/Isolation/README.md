# WeaseLC 首次隔离部署测试包

目标：x64 Windows 11 的**一次性虚拟机**。当前状态：本机已完成独立宿主回归；尚未进行虚拟机安装和真实应用验收。不能在日常电脑安装本包。

## 包含与边界

- 正式双架构 TSF DLL、x64 服务/管理程序、x86 Setup，配套 Rime DLL、方案与 OpenCC 数据；Win32 服务/管理程序用于后续架构对照。
- 独立诊断程序、固定响应的本地 HTTP 夹具、逐文件 SHA256 与 PE 架构清单、当前 Git 源码快照及子模块提交记录。
- 不包含模型权重、用户词库/正文、密钥、NSIS、旧 curl/7-Zip/WinSparkle 或上游更新安装器。开发版在线更新已关闭。
- `source.zip` 内的子模块须按 `SUBMODULES.txt` 和源码 `.gitmodules` 获取；这是私人测试材料，尚未完成对外发行所需的完整依赖源码、签名与许可证物料验收。
- 哈希用于检查传输损坏，不提供发行者签名。正式构建没有探测导出；探测 DLL 只用于独立诊断，不能注册为输入法。

开发机先按仓库说明重建普通目标和诊断工程、提交已审查源码，再运行 `test/Isolation/Build-Package.ps1 -Git <git.exe 路径>`。输出在 `output/IsolationTest/`，不推送二进制到 Git。重复构建同一提交可指定不同的 `-Label`。源 ZIP 对两份旧工具的排除清单见 `SOURCE_OMISSIONS.txt`。

## 1. 准备环境

在支持 Windows 11 的虚拟机软件中建立客体。建议分配 4 个虚拟 CPU、8 GB 内存及 64 GB 以上虚拟磁盘；使用有权使用的 Windows 镜像。宿主和客体关闭共享剪贴板、拖放、USB/宿主磁盘透传，不登录个人账号。

安装完成后创建“干净系统”快照。首次使用仅有系统自带输入法的新测试账户；不要安装已有 Weasel。将 ZIP 通过只读共享传入，复制到客体本地再解压，断开共享。记录系统、显示缩放、应用版本和快照名称。初次固定响应测试可断网进行。

本机 Windows 家庭版不支持 Windows Sandbox；不修改宿主系统组件。虚拟机软件与镜像的取得见仓库内的隔离测试方案。

## 2. 注册前回归（普通 PowerShell）

在解压目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\SmokeTest.ps1
```

脚本只创建自有文档/管道，不激活或注册 TIP、不连接日常服务；成功不等同于输入法安装成功。任何失败都停止部署。日志位于新建的临时报告目录。

## 3. 首次安装（虚拟机内管理员 PowerShell）

确认快照可恢复后：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Install-InDisposableVM.ps1 -SnapshotConfirmed
```

脚本先核对包哈希，拒绝构建电脑、非识别虚拟机、已有 Weasel 状态和非 x64 Windows 11。随后复制到客体 `C:\WeaseLCTest`，以正式 CLSID 注册，使用客体 `C:\WeaseLCTest-User`。不添加 Windows 服务、开机启动项或计划任务。它会修改**客体**系统 DLL、COM/TIP 和当前用户输入法列表。

安装不是事务；某一步失败可能留下部分客体状态。失败时保存返回值，然后恢复快照，不在残留状态上反复安装。已有 Weasel/搜狗共存属于第二阶段，另建快照测试。

## 4. 首次应用验收（普通 PowerShell）

关闭管理员终端，用同一测试账户的普通 PowerShell 运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Start-TestApps.ps1
```

先在记事本中切换到小狼毫，完成候选、简体上屏、中英文切换、菜单和设置检查。若无法正常输入，停止纠错测试；记录图标、实际 DLL/服务路径、系统错误和返回码。必要时注销客体账户再登录，不能在宿主重启输入服务来代替验证。

基本输入通过后，在客体另开一个普通 PowerShell：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Start-SyntheticModel.ps1
```

“管理纠错”选择本地 HTTP，地址 `http://127.0.0.1:18964/v1/chat/completions`，模型名 `weasel-synthetic`；开启自动提示，保存。输入固定句“这里有错子。”，上屏后开始下一次拼音组合。应出现“错子 → 错字”；Tab 接受且保留正在输入的拼音。Esc 忽略。其他文字不产生固定建议。

这一步只验证安装后的完整连线，**没有模型推理**。普通字段若未提供明确 `IS_TEXT`，程序会保守跳过；须记录宿主能力，不能为了出现提示而关闭密码/未知字段保护。

然后按 `APPLICATION_MATRIX.md` 检查鼠标、粘贴、切换和关闭等情况。首个应用全部通过后再扩展浏览器、Office/编辑器、Win32 程序和输入法共存。真实小模型推理另需客体本地部署并校验；客体没有 GPU 直通时可采用 CPU 推理，不能把宿主回环地址当作客体服务。

## 5. 收尾与记录

退出固定响应服务，关闭测试应用。只导出合成测试记录、返回值和崩溃元数据；截图先确认不含个人内容。恢复“干净系统”快照并核对输入法列表及原有输入恢复。不用客体卸载成功返回码替代快照恢复。

将矩阵中的“待测”改为通过/失败/跳过，附应用版本、架构、输入范围、时间和复现步骤。首次部署结果未记录前，不在日常系统安装。
