# Weasel 0.17.4 本地构建基线

记录日期：2026-10-08。本文件记录当前工作区的可核对事实，供纠错功能开发和后续发布复查；不是完整的软件物料清单，也不表示已经完成安装或真实应用验证。

## 1. 工作区与工具链

| 项目 | 本机记录 |
| --- | --- |
| 源码目录 | `E:\WeaseLC\weasel-0.17.4`；顶层是源码拷贝，没有 `.git`，故无法从本目录确定上游提交号 |
| 构建系统 | Visual Studio 2026 MSBuild 18.10.1；MSVC v143 14.44.35207；ATL、MFC 已安装 |
| Windows SDK | 10.0.26100.0 |
| 操作系统 | Windows 11 家庭版，版本 10.0.26100 |
| CPU / 内存 | Intel Core i7-14650HX，16 核 24 线程；约 31.7 GiB 总可见物理内存 |
| 显卡 | NVIDIA GeForce RTX 4060 Laptop GPU；Intel UHD Graphics。显存、驱动版本尚未测量 |
| Boost | 1.84.0，静态库已构建 x64 与 Win32；本地压缩包 SHA-256：`A5800F405508F5DF8114558CA9855D2640A2DE8F0445F051FA1C7C3383045724` |
| 构建配置 | `Release`，`x64` 与 `Win32`；`weasel.props` 指向本地 Boost，`env.bat` 使用 `v143` |

`deps/boost_1_84_0.tar.gz` 的文件名和解压目录表明版本为 1.84.0，以上哈希可用于核对本地包；文件本身未保留可验证的下载来源元数据。顶层 `.gitmodules` 声明了依赖仓库地址，但当前实际使用的六个 C++ 依赖放在 `librime/deps/`，没有启用远端；下表的提交号来自各目录的本地 Git 元数据。`librime/`、`plum/` 的远端也已解绑。

## 2. 源码依赖与许可证

| 组件 | 本地提交或版本 | 声明的上游 / 本地许可证 |
| --- | --- | --- |
| Weasel | 源码包标称 0.17.4，顶层无 Git 提交号 | [`LICENSE.txt`](../LICENSE.txt)，GPL-3.0 |
| librime | `1c23358157934bd6e6d6981f0c0164f05393b497` | [rime/librime](https://github.com/rime/librime)，[`librime/LICENSE`](../librime/LICENSE)，BSD 3-Clause |
| plum | `cab9ed35289f2022b6888503750d2dfb1851dbe0` | [rime/plum](https://github.com/rime/plum)，[`plum/LICENSE`](../plum/LICENSE)，LGPL-3.0 |
| glog | `7b134a5c82c0c0b5698bb6bf7a835b230c5638e4` | [google/glog](https://github.com/google/glog)，[`COPYING`](../librime/deps/glog/COPYING)，BSD 3-Clause |
| LevelDB | `99b3c03b3284f5886f9ef9a4ef703d57373e61be` | [google/leveldb](https://github.com/google/leveldb)，[`LICENSE`](../librime/deps/leveldb/LICENSE)，BSD 3-Clause |
| yaml-cpp | `f7320141120f720aecc4c32be25586e7da9eb978` | [jbeder/yaml-cpp](https://github.com/jbeder/yaml-cpp)，[`LICENSE`](../librime/deps/yaml-cpp/LICENSE)，MIT |
| googletest | `f8d7d77c06936315286eb55f8de22cd23c188571` | [google/googletest](https://github.com/google/googletest)，[`LICENSE`](../librime/deps/googletest/LICENSE)，BSD 3-Clause；构建/测试依赖 |
| marisa-trie | `0d4e8ab58eec355facf8f65ff11ef811b330e373` | [rime/marisa-trie](https://github.com/rime/marisa-trie)，[`COPYING.md`](../librime/deps/marisa-trie/COPYING.md)，BSD 2-Clause 或 LGPL，可按许可文本选用 |
| OpenCC | `e5d6c5f1b78e28a5797e7ad3ede3513314e544b7` | [BYVoid/OpenCC](https://github.com/BYVoid/OpenCC)，[`LICENSE`](../librime/deps/opencc/LICENSE)，Apache-2.0 |
| Boost | 1.84.0，本地包哈希见上 | [`LICENSE_1_0.txt`](../deps/boost_1_84_0/LICENSE_1_0.txt)，Boost Software License 1.0 |

仓库内还包含嵌套依赖、WTL 头文件及运行时工具。发布前须按最终打包内容重新生成完整许可证清单，保留各组件要求的版权与许可文本。GPL-3.0 的 Weasel 修改版发布时还须准备相应源码与修改说明；模型权重和推理运行时的许可需在选型后另行核对。此处仅列本地文件所载许可，不替代发布前法律审查。

## 3. 现成运行时二进制

| 文件 | 文件元数据版本 | SHA-256 / 许可记录 |
| --- | --- | --- |
| `output/WinSparkle.dll` | 0.8.1 | `F3C86101E041DB5476C4D491AB1175BFAC22A26CE41046551B283AD74DF27718`；上游 [WinSparkle COPYING](https://github.com/vslavik/winsparkle/blob/master/COPYING) 为 MIT，当前输出目录未见其单独许可副本 |
| `output/Win32/WinSparkle.dll` | 0.6.0 | `14D9ADA9FD17AD089D7DEA3A4B6E7117F132B23CD150323C60DF5FFDA5C72B6F`；与 x64 版本不一致，需在发布前确认来源及是否升级 |
| `output/curl.exe` | 8.7.1 | `C64A615BB61401006D79CFD2E169A04A8C67582C6F9F023604510C1D915E93FF`；[`COPYING-curl.txt`](../output/COPYING-curl.txt) |
| `output/7z.exe` | 24.05 | `F2E7726726C8CFC23E84DE198ABC04A5FE73249DCDF9EF9E5CA2B83E23643AE6`；[`7-zip-license.txt`](../output/7-zip-license.txt) |

这些版本来自文件元数据；没有二进制签名与上游发布包逐一比对记录。WinSparkle 两种架构的版本差异保留原样，不在纠错原型阶段顺便替换。

## 4. 构建与验证

MSBuild 的 v143 项目集成位于 `E:\Microsoft Visual Studio\MSBuild\Microsoft\VC\v170\`。在当前命令环境下，先设 `VCTargetsPath` 并移除进程内重复的 `PATH` 环境项，再调用 MSBuild：

```powershell
$env:VCTargetsPath = 'E:\Microsoft Visual Studio\MSBuild\Microsoft\VC\v170\'
Remove-Item Env:PATH -ErrorAction SilentlyContinue
& 'E:\Microsoft Visual Studio\MSBuild\Current\Bin\MSBuild.exe' weasel.sln /t:Build /p:Configuration=Release /p:Platform=x64 /m:1 /nologo /verbosity:minimal
& 'E:\Microsoft Visual Studio\MSBuild\Current\Bin\MSBuild.exe' weasel.sln /t:Build /p:Configuration=Release /p:Platform=Win32 /m:1 /nologo /verbosity:minimal
```

当前源码（含纠错设置与手动 TSF 探测）曾通过 x64 和 Win32 编译；实验构建日志已按清理要求删除。NSIS 未安装，也未生成安装包。

**运行验证边界：** 当前系统仍运行 `C:\Program Files\Rime\weasel-0.17.0\WeaselServer.exe`；原 TSF CLSID 的 64 位与 32 位注册路径均指向系统目录的 `weasel.dll`。直接注册普通新 DLL 会替换这份注册，不是隔离测试。因此尚未执行真实应用中的 TSF 探测、输入稳定性比较或 DPI/应用矩阵。

### 独立 TSF 探测版

最新进展：2026-10-08 已通过 `TF_RP_LOCALPROCESS` 配置的系统激活、用户确认的中文上屏、语言栏/菜单检查及三种输入类型的上下文探测。本次修复引用泄漏，验证候选对象保留、DLL 可卸载、文档切换及离开焦点后取消读取；普通 x64/Win32 TSF 编译通过。完整停用/激活仍间歇在语言栏阶段失败，未安装系统级测试输入法。详见 [`LOCAL_TSF_TEST_RESULTS.md`](LOCAL_TSF_TEST_RESULTS.md)。以下段落保留此前构建及安装故障记录。

2026-10-08 继续修正初始化失败后的清理：按键监听注册标记、线程焦点监听取消、激活参数检查和重复退出保护。x64 独立失败注入与资源检查退出码 0；普通 x64/Win32 TSF 编译通过。测试覆盖激活早期的三个失败阶段，成功激活后的清理和真实中文输入仍待验证。源码与诊断保留，实验二进制和中间文件在测试结束后清理。

已为 Release x64/Win32 增加条件构建 `WEASEL_CORRECTION_PROBE_BUILD=1`。当前源码输出到 `output/Probe/v3/<平台>/weaselProbe.dll`，中间文件也隔离；CLSID 为 `{B749B45E-7DCE-4496-8414-9EE10970260A}`，语言配置文件 GUID 为 `{A2F702D2-DD84-4C9E-9C18-66FEBA75C4E3}`。v2 在真实使用中不能输入中文，且语言栏与菜单缺失；详细事实、回撤和未证实的原因见 [`PROBE_ACTIVATION_INCIDENT.md`](PROBE_ACTIVATION_INCIDENT.md)。v3 调整了测试版注册方式和模式按钮 GUID，当前 x64 的进程内 DLL、类创建与资源检查通过，普通 x64/Win32 TSF 也通过回归编译。**没有安装或真实输入验证**；普通构建仍使用原 CLSID 和输出文件。诊断入口见 [`test/ProbeActivation/README.md`](../test/ProbeActivation/README.md)。实验输出在验证后清理；以下命令会重新生成它们：

```powershell
$env:VCTargetsPath = 'E:\Microsoft Visual Studio\MSBuild\Microsoft\VC\v170\'
Remove-Item Env:PATH -ErrorAction SilentlyContinue
& 'E:\Microsoft Visual Studio\MSBuild\Current\Bin\MSBuild.exe' weasel.sln /t:WeaselTSF /p:Configuration=Release /p:Platform=x64 /p:WEASEL_CORRECTION_PROBE_BUILD=1 /m:1 /nologo /verbosity:minimal
& 'E:\Microsoft Visual Studio\MSBuild\Current\Bin\MSBuild.exe' weasel.sln /t:WeaselTSF /p:Configuration=Release /p:Platform=Win32 /p:WEASEL_CORRECTION_PROBE_BUILD=1 /m:1 /nologo /verbosity:minimal
```

2026-10-08 曾在同一账户的提升权限进程中注册 v2，并通过 `InstallLayoutOrTip` 将其加入当前用户输入法列表。用户实际选择后报告：不能输入中文，中英文切换与探测菜单缺失，图标仍是上一个输入法。进程检查未发现目标应用加载 v2 DLL。现已撤销 v2 的系统注册和输入法列表项，原版小狼毫的 x64/Win32 注册路径保持原样，用户确认原版功能正常。**当前没有已安装的测试输入法，也没有成功的真实输入或正文读取验证。**

[`register-correction-probe.ps1`](../register-correction-probe.ps1) 目前仅用于状态核对和 v2 卸载清理；`Register` 已禁止，不能用它安装 v3。`Status` 会核对 32/64 位合并 COM 视图。没有测试注册时，`Unregister` 安全退出，无需 DLL 文件或提升权限；仍有注册时需同一 Windows 用户的提升权限终端。调用示例：

```powershell
& 'E:\WeaseLC\weasel-0.17.4\register-correction-probe.ps1' -Action Status
# 仅在需要清理旧版测试注册且已提升权限时运行：
& 'E:\WeaseLC\weasel-0.17.4\register-correction-probe.ps1' -Action Unregister
```

不要对原版 `weasel.dll` 执行卸载。待以后在隔离环境完成输入功能验证，才能使用 [`read-correction-probe.ps1`](../read-correction-probe.ps1) 测量光标两侧可读字符数。`RegisterProfile` 的 `cchDesc` 已按 Microsoft API 要求改为字符数，相关说明见 [官方文档](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-registerprofile)。
