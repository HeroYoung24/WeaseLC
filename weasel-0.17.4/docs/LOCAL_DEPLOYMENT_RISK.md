# 本机独立部署风险评估（2026-10-16）

**最新：已修复输入项不可见。用户确认测试项出现、中文输入与搜狗正常。自动纠错仍默认关闭，尚未完成真实应用纠错验收。下面保留首次部署与定位经过，最新修复见文末。**

## 决策与授权

用户授权：评估风险；若极大概率不会造成不可逆系统损伤，直接本机部署并测试。本次授权取代之前仅在虚拟机内安装的约束。虚拟机安装包的保护仍保留，不绕过它来覆盖日常版本。

判断：**独立身份、独立目录部署的永久性系统损伤风险较低，可以开展受控本机测试。暂时影响输入、宿主崩溃和未保存文本丢失仍有现实风险。** 不能给出有统计依据的“绝对安全”或概率保证，也不能将本次部署等同于产品验收。

| 风险 | 依据与处理 |
| --- | --- |
| Windows 无法启动、驱动或引导损坏 | 本次为用户态 TSF COM DLL 与普通后端，不安装驱动，不改引导、系统服务或登录输入设置；预期风险低 |
| 覆盖日常小狼毫 | 原安装器会替换两份系统 DLL，甚至重命名占用文件并安排重启删除，且无事务回退。本次不执行该安装器，系统 DLL 哈希及原版注册路径作为前后检查基线 |
| 搜狗中英切换失效 | 历史实验曾发生，重启后恢复。不能排除再次发生；新身份仍共享 Windows TSF 子系统，因此独立目录不能完全消除此风险 |
| 应用崩溃/文本损坏 | TSF 在宿主应用内运行；内存或编辑会话缺陷可能影响该应用。先在新测试文件用合成文字检验，不在重要文档中首测 |
| 词库/设置串用 | 新 CLSID/Profile、命名管道、窗口类、互斥量、HKCU/HKLM 设置根、用户词库及日志目录；后端不会连接/退出原版后端 |
| 隐私/模型调用 | 默认关闭纠错；本轮基本输入测试不启动模型，不读取实际文档、剪贴板或日常词库；后续自动检查仅按已有显式文本范围限制执行 |
| 注册 DLL 被普通用户替换 | 复制到继承 Program Files 访问控制的新目录；用户和应用包只有读取执行权。复制前后均校验清单，拒绝额外文件/重解析点 |
| 回退不完整 | 已演练注册→注销→核对新增根键消失、原版哈希/注册不变→重新注册。尚未证明从任意宿主崩溃状态恢复无需重启 |

## 本次部署范围

- 输入法：**WeaseLC 本机测试**。
- CLSID：`{893169EA-8480-4965-9F2F-695ED51E4B43}`。
- Profile：`{FAF832B8-6CDD-4F9A-993D-22B47672BA27}`。
- 安装：`C:\Program Files\WeaseLC-LocalTest`；x86 在 `Win32` 子目录。
- 用户数据：`%APPDATA%\WeaseLC-LocalTest`，新建简体拼音方案，不复制原用户数据。
- 设置：`Software\Rime\WeaseLC-LocalTest`，纠错为其 `Correction` 子键。
- 普通与探测构建仍使用各自既有身份；本机部署宏不包含探测导出，不注册 SecureMode 类别，不启动原版批处理，不注册应用重启。
- 仅添加当前用户的简体输入项；搜狗默认项保留。没有自启动，电脑重启后需手动运行独立后端。

## 已验证与待测

- Release x64/Win32 独立 TSF、Server、Deployer 构建成功。
- 随后普通 Release x64/Win32 的 TSF、Server、Deployer 回归构建成功；二进制检查确认普通构建保留旧管道名、不含独立测试管道，避免构建产物串用。
- 两架构各 26 项类工厂检查通过，DLL 实际卸载通过；宿主异常过滤器加载/卸载保持检查通过。
- 二进制架构、新身份及独立端点检查通过；链接记录确认使用独立构建的静态库。
- UAC 安装成功。注销演练中 x64 `regsvr32 /u` 返回 0；x86 返回 5（前一次已移除共用 Profile），随后核对两架构 COM/TIP 及机器设置根均消失，原版基线一致，才重新注册。不能只依据单个退出码判断清理。
- x64 与 x86 客户端均通过实际独立后端/Rime，将固定 `nihao ` 转换成 `你好`；这不等于真实应用 TSF 按键测试通过。
- 加入输入法列表后，原四个输入项保留，搜狗默认项相同。原版 WeaselServer PID 16964、搜狗 Broker PID 9732/启动时间保持不变；新后端 PID 10932。
- 用户两次确认：记事本可见，Shift 正常，但 Win+空格列表不显示本机测试项。**真实应用输入测试未通过，尚不能选择测试输入法。**
- 追加原生 `ProfileCheck`：受限开发令牌下 `GetProfile` 返回 E_FAIL、只枚举到键盘布局；在正常交互令牌下可以枚举全部原有输入法及新项。后续 TSF 原生配置诊断须在正常令牌执行，不能把受限工具中的失败直接归因于输入法。
- 初次正常令牌枚举新项 flags=0；`EnableLanguageProfile` 后同进程 GetProfile flags=2、通知设置更新成功，但后续新进程仍见 flags=0。HKCU Enable=1，用户列表包含新项，默认搜狗保持不变。已重试先启用再添加以及添加后启用；用户界面仍未显示，不能断言仅为缓存。**需重启后检查原生枚举和 Win+空格；若仍失败继续分析注册兼容性，不反复修改全局输入状态。**
- 自动纠错、Tab、鼠标打断、模型性能、长时间运行均未完成本机验收。模型未启动，纠错保持默认关闭。
- 收尾停止独立测试后端 PID 10932，并确认其已退出；仅保留独立注册/用户列表项，等待重启后检验。原版两份 DLL 哈希及 COM/机器设置基线未变，搜狗与原版后端仍保持原 PID/启动时间。重启后从 `test\LocalDeployment\Start.cmd` 手动启动，无开机项。

测试证据在忽略目录 `output\LocalTest`、`output\local-*.log`；程序日志在 `%TEMP%\rime.weaselc-localtest`。评估不等同于完整安全审计。

## 停止与回退

普通 PowerShell，位于源码目录：

```powershell
# 仅停止独立测试后端，不改变注册
powershell -NoProfile -ExecutionPolicy Bypass -File .\test\LocalDeployment\Start.ps1 -Stop
# 从当前用户列表移除、停止独立后端，并弹出 UAC 注销独立注册
powershell -NoProfile -ExecutionPolicy Bypass -File .\test\LocalDeployment\Rollback.ps1
```

确认 `output\LocalTest\elevated-Unregister.json` 的结果后，运行 `Install.ps1 -Action Status` 应没有机器 COM/TIP 根。脚本保留非活动程序文件与独立测试词库用于诊断，不安排重启删除，不对其他输入法执行卸载。已打开应用可能继续持有 DLL；若语言栏/切换异常，先停用测试项并保存工作，重启 Windows 清除会话状态。文件可在注销且所有宿主退出后另行清理。

官方接口依据：[TSF 注册](https://learn.microsoft.com/en-us/windows/win32/tsf/text-service-registration)、[InstallLayoutOrTip 的添加/删除与默认项标志](https://learn.microsoft.com/en-us/windows/win32/tsf/installlayoutortip)、[ActivateProfile 的进程/会话范围](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-activateprofile)。

## 重启后不可见修复与实际输入确认

1. 用户重启并运行 Start.cmd 后仍没有测试项；真实交互令牌的 GetProfile 仍为 flags=0，排除“重启即可解决”的假设。
2. 本机测试构建人为设置 RegisterProfile 的 `bEnabledByDefault=FALSE`；普通版本简体 Profile 为 TRUE。用户级 Enable=1、SortOrder 项完整、EnableLanguageProfile 成功，仍不足以让新进程看到启用状态。
3. 用管理员官方 `EnableLanguageProfileByDefault` 只调整新 CLSID 的简体 Profile 可用性。此调用影响该 Profile 对所有用户的默认可用属性，**不设置用户的默认输入法、不激活 TIP**；本用户的输入列表仍通过 InstallLayoutOrTip 单独添加。不能将它描述成仅写 HKCU。
4. 最初修复脚本报告失败：写入进程仍读到旧缓存并以退出码 8 报错。但后续两个独立进程均读到 flags=2，用户测试时读到 flags=3（启用且活动），切回搜狗后为 flags=2。这一错误日志保留，不据此声称 API 写入失败。
5. 辅助工具已改为按写入 API 的 HRESULT 返回，安装/修复流程另外启动只读进程验证持久化。新构建简体 Profile 改为可用；繁体及其他 Profile 仍禁用。现有安装通过 API 修复注册元数据，**没有替换已加载的 DLL**。
6. 用户确认：**测试项已出现、中文输入及搜狗均正常**。这完成本轮基本可选与中文输入验证，不代表自动纠错、Tab/鼠标边界或全部应用通过验收。
7. 原版 DLL 哈希/COM/机器配置基线一致，默认输入法仍为搜狗。重启后原版后端 PID 24436、搜狗 PID 19664 保持不变；独立后端 PID 21700 保留运行供继续测试，未增加自启动。

依据：[EnableLanguageProfileByDefault 的所有用户可用范围](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofiles-enablelanguageprofilebydefault)、[TF_INPUTPROCESSORPROFILE 的启用/活动标志](https://learn.microsoft.com/en-us/windows/win32/api/msctf/ns-msctf-tf_inputprocessorprofile)。
