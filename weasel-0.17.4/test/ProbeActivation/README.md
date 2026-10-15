# 本地 TSF DLL 诊断

这个程序在独立进程中直接加载指定 DLL。默认检查不注册 COM 类、TSF 配置或 Windows 服务；常规配置模式仅注册进程内配置，不改变用户输入法列表。另有需要管理员权限的临时类别、隐藏注册表配置对照模式，边界见下文。当前工程配置 Release x64、Win32。

默认检查 `DllGetClassObject`、`ITfTextInputProcessorEx` 实例创建、三个语言版本的探测菜单资源和中英文图标资源。对于带有探测诊断导出的新测试 DLL，还用模拟线程管理器检查激活失败后的清理。旧系统 DLL 没有这些导出时跳过失败注入。退出码 0 表示执行的检查通过，**不表示系统能激活输入法或能正常输入中文**。

## 构建与运行

在项目根目录、能使用 v143 的环境中运行。以下路径是本项目当前机器的工具链位置：

```powershell
$env:VCTargetsPath = 'E:\Microsoft Visual Studio\MSBuild\Microsoft\VC\v170\'
Remove-Item Env:PATH -ErrorAction SilentlyContinue
& 'E:\Microsoft Visual Studio\MSBuild\Current\Bin\MSBuild.exe' weasel.sln /t:WeaselTSF /p:Configuration=Release /p:Platform=x64 /p:WEASEL_CORRECTION_PROBE_BUILD=1 /m:1 /nologo /verbosity:minimal
& 'E:\Microsoft Visual Studio\MSBuild\Current\Bin\MSBuild.exe' test\ProbeActivation\ProbeActivation.vcxproj /p:Configuration=Release /p:Platform=x64 /m:1 /nologo /verbosity:minimal
& '.\output\ProbeDiagnostics\x64\ProbeActivation.exe' "$PWD\output\Probe\v3\x64\weaselProbe.dll" '{B749B45E-7DCE-4496-8414-9EE10970260A}'
```

可加 `--activate` 进行额外的手动激活诊断。程序创建并激活本线程的 TSF 线程管理器，通过 `ITfClientId` 获取服务身份，然后调用 DLL 的 `ActivateEx`。成功时还检查语言栏对象所属服务、标准模式项 GUID 和图标；最后配对调用 `Deactivate`。这条路径没有系统语言配置文件选择过程、目标应用输入框和真实按键，不能代替完整输入验收。

测试版导出当前进程内的阶段编号和按键注册阶段原始 HRESULT；不会记录输入正文，也不会把这些诊断写入注册表。阶段 1–6 分别是线程管理器事件、按键事件、保留按键、语言栏、状态 compartment、线程焦点事件；7 是服务连接；100 为初始化完成且服务已连接，101 为初始化完成但服务未连接。其他阶段的原始结果可能为 `E_PENDING`，不能解释为具体操作错误。

## 类工厂与实际模块卸载（2026-10-09）

```powershell
& '.\output\ProbeDiagnostics\x64\ProbeActivation.exe' "$PWD\output\ProbeDiagnostics\x64\weaselProbe.dll" '{B749B45E-7DCE-4496-8414-9EE10970260A}' --class-factory
```

Win32 使用对应架构的程序与 DLL。普通构建使用普通 DLL 和原版 CLSID。此入口不初始化 COM/TSF、不激活输入服务、不调用注册入口，不读取文本或打开窗口。构造服务对象会执行其构造代码，但不建立输入会话。

先验证错误 CLSID 和不支持的工厂接口；若不符合预期则停止，避免对旧 DLL 调用可能崩溃的空指针路径。随后检查空输出、接口身份、聚合拒绝、对象引用、多个工厂及模块锁；探测版额外要求服务创建/销毁平衡且没有激活。普通版 26 项、探测版 27 项。

最后要求 `DllCanUnloadNow` 成功，释放诊断程序的加载器引用，并按原模块地址查询，确认模块消失且错误为 `ERROR_MOD_NOT_FOUND`。COM 引用归零与操作系统实际卸载是两个独立检查。失败返回 1。

## 组合范围与显示属性引用（2026-10-09）

```powershell
& '.\output\ProbeDiagnostics\x64\ProbeActivation.exe' "$PWD\output\ProbeDiagnostics\x64\weaselProbe.dll" '{B749B45E-7DCE-4496-8414-9EE10970260A}' --composition-checks
```

仅支持探测 DLL；Win32 使用匹配架构。通过测试导出执行生产结束组合、文本定位及显示属性代码，使用诊断自己拥有的 COM 对象，不初始化 COM/TSF、不激活服务、不读用户文本、不调用注册入口。模拟范围每次返回均增加引用；调用后要求回到初始引用数。18 个场景各重复 64 次，检查成功/拒绝、无组合、空选区、成功但空范围的保护、位置回退、自取显示属性范围与实际 DLL 卸载。

模拟视图只返回零坐标，避免 IPC/UI 位置更新；不提供显示属性 property，因此只验证引用与安全失败，不验证渲染成功。诊断自身关闭系统崩溃弹窗，将访问异常打印到输出；故障退出仍为失败，不生成转储，也不改变其他进程设置。

同日后续增加 23 个状态场景：11 个开始组合成功/拒绝/错误；5 个结束失败及首错/状态保留；6 个请求和编辑会话双结果；1 个旧结束请求排队后新组合已建立的场景。共享生产 `RequestCompositionEditSession`，请求失败时 mock 故意不写会话输出，延迟方式持有编辑对象直到手动排空。检查旧通知、旧范围清理和旧结束都不影响新组合，依赖对象在清理后回到初始引用。

这些场景不经过真实 Windows 异步调度，不读取服务器响应；尚未验证主提交流程、跨文档和停用后的旧请求，也未覆盖当前组合终止回调的重入。`TF_S_ASYNC` 只代表请求获准排队，不能据此报告文字编辑完成。

同日进一步增加 13 个绑定/取消/清理场景：同文档请求可继续执行，跨文档及 A→B→A 旧请求被拒绝，显式失效、`Deactivate`（无激活的独立对象）、重复停用和焦点为空均取消旧请求；开始/结束子请求取消后不调用宿主；执行体错误保留，旧的未绑定服务 `ITfEditSession` 入口拒绝执行。另检查旧文档延迟清理、清理回调建立新组合的重入、清理请求被拒绝。生产主请求对象、捕获 context/cookie 和生命周期检查均复用；探测专属观察回调代替有效请求的 IPC/UI 执行体，只计数模拟调用，不读取或发送文本。清理请求失败场景仅证明脱离服务当前状态，不能证明宿主旧组合已经清除。

每轮需释放排队对象并调用失效入口，验证诊断 context 的引用归位。取消不会强制释放 Windows 持有的会话；对象在宿主释放后销毁。该测试没有真实再激活，不验证同文档排队响应快照或旧组合的实际文本清理，实际中文上屏另用 `--input` 验收。

## 注册失败与撤销模拟（2026-10-09）

```powershell
& '.\output\ProbeDiagnostics\x64\ProbeActivation.exe' --registration-checks
& '.\output\ProbeDiagnostics\Win32\ProbeActivation.exe' --registration-checks
```

这个入口不需要 DLL 参数，不加载输入法 DLL，不初始化 COM/TSF，也不写注册表。使用与生产入口共享的 `RegistrationWorkflow.h`，注入模拟注册、清理和注册表删除回调。两种架构各 38 项检查通过：三个注册失败位置、部分写入后回退、注册/回退同时失败、重试、3/5/16 项清理中的每个失败位置、注册表错误分类与空删除目标拒绝。

`DllRegisterServer` 返回原始注册失败 HRESULT，调试输出同时记录失败步骤和回退 HRESULT；步骤 0/1/2 分别为 COM、语言配置、类别。`DllUnregisterServer` 尽量执行所有清理并返回第一个失败。只有 Win32 注册表键不存在被当作已清理，不把 TSF 的通用 `E_FAIL` 当作“不存在”。实际权限、视图、缓存及系统注册残留未在这个入口测试，仍需隔离环境。回退沿用删除本服务注册的策略，不是保存/恢复已有注册的事务。

## 宿主异常处理边界（2026-10-09）

`--load-only` 只在本诊断进程中加载/卸载 DLL，不初始化 COM 或 TSF、不调用注册入口，也不触发崩溃。先测试宿主没有自定义异常处理函数，再测试宿主有自己的处理函数；两种状态都要求加载后、卸载后保持原样。退出前恢复诊断进程原有设置。例：

```powershell
& '.\output\ProbeDiagnostics\x64\ProbeActivation.exe' "$PWD\output\ProbeDiagnostics\x64\weaselProbe.dll" '{B749B45E-7DCE-4496-8414-9EE10970260A}' --load-only
```

DLL 参数也可指向工作区普通构建，使用其原版 CLSID 参数。Win32 使用对应架构的程序与 DLL；不能混用。加载失败或处理函数被改动均返回 1。

修正前两种探测 DLL 均复现覆盖宿主设置；已有宿主处理函数时，加载/卸载后均未保留。修正后移除 `DllMain` 中全进程崩溃处理注册及宿主转储代码，普通与探测的 x64/Win32 检查均通过。双架构 `--documents` 回归通过；未证明此问题是旧系统安装故障的根因。

## 2026-10-08 结果

最新验证：系统调用的进程内激活、语言栏/菜单及中文上屏通过，用户看到“你好”并关闭窗口；普通文本、未知类型、密码类型的探测分别通过。详见 [`LOCAL_TSF_TEST_RESULTS.md`](../../docs/LOCAL_TSF_TEST_RESULTS.md)。

附加模式：`--profile` 用独立 manifest 与进程内配置验证系统激活及菜单；`--input` 显示隔离窗口，需要用户点击获得焦点后自动测试按键上屏；`--scope`、`--scope-unknown`、`--scope-password` 分别使用新的诊断进程测试普通、未知、密码类型。后三者只捕获内存结果。运行这些模式时 DLL 参数应指向与诊断程序同目录的 `weaselProbe.dll`，该 DLL、符号和 manifest 会在诊断构建时复制到输出目录。

新增 `--documents`：同一激活期中依次切换普通、未知、密码合成文档，并检查启动探测后离开文档时取消读取。此模式通过。`--cycles`：每个文档前完整停用/激活，要求实际激活次数增加、语言栏可用及边界结果正确；仍间歇在语言栏阶段 4 返回 `E_FAIL`，不作为已通过验收。

后续已捕获一次直接冲突：管理器查询和前次移除成功，但 `AddItem=E_FAIL` 时已有标准模式项属于本机搜狗服务。源码增加注册成功及移除前的 COM 对象身份确认，并保存同一注册管理器，避免移除其他服务按钮。默认检查新增五种模拟情况：正常注册、添加失败、添加被忽略、移除前被替换、移除失败；均通过。最终一次 `--cycles` 和桌面中文回归通过，不能据此宣布完整共存稳定。

探测 DLL 的 `WeaselProbeCopyLanguageBarTrace` 导出最多 128 条内存记录；诊断失败时打印。操作编号 1–5 分别为查询管理器、添加、移除、移除前身份检查、添加后身份检查；所有者编号 0–4 分别为未知、无项、本对象、另一个探测对象、其他服务。包含 HRESULT、服务指针数值身份及已有服务 CLSID，不包含文本，不写注册表。仅在错误路径额外查询已有项以获取失败来源，正常路径保留必要的身份保护。

测试文档/窗口保留到所有场景结束，避免将关闭宿主混入切换；恢复焦点使用原测试文档。进程内配置没有系统类别注册，测试配置的类别为 `GUID_NULL`，不能把此环境等同于完整键盘 TIP 安装。旧 `--activate` 是手动调用服务的诊断方式，仍不适合作为系统激活验收。

探测 DLL 导出生命周期计数和仅供同一 DLL 实例使用的候选保留测试入口。测试结束必须创建/销毁、激活/停用计数相等且 `DllCanUnloadNow=S_OK`，否则退出码为 1。默认检查还验证宿主保留候选时服务可销毁、候选访问安全及描述 BSTR 可重复分配/释放。

`--input` 应在真实用户桌面启动；工具沙盒可能创建用户不可见的窗口，即使 `IsWindowVisible` 返回真。等待焦点超时仅表示未执行测试。不要靠结束用户应用或切换原版输入法来解决窗口问题。

- x64 DLL 与诊断程序构建成功；默认资源及失败清理检查退出码 0。
- 失败注入覆盖线程事件注册失败、按键管理接口不可用、按键监听注册失败。每种情况重复激活两次，并在每次失败后重复退出；确认仅取消成功注册的线程监听、不取消未注册的按键监听、释放线程管理器引用。另验证未激活时退出和无效激活参数。这些测试不创建系统 TSF 管理器、不连接输入服务。
- 源码补上按键监听注册状态、线程焦点监听的取消调用和 cookie 重置，以及重复退出与激活参数保护。普通 x64/Win32 TSF 回归编译通过。成功激活后的焦点监听清理尚无运行测试。
- 手动激活停在阶段 2，`ActivateEx` 为 `0x80004005`，`AdviseKeyEventSink` 原始结果为 `0x80070057`（`E_INVALIDARG`）。
- 原有正常使用的系统版 `weasel.dll` 在同类手动激活中也返回 `0x80004005`。因此，未把手动激活失败等同于测试版的真实故障根因，也未宣布真实输入已修复。
- 修正了测试版错误的语言栏项 GUID，恢复标准 `GUID_LBI_INPUTMODE`。Microsoft 的 [语言栏项结构文档](https://learn.microsoft.com/en-us/windows/win32/api/ctfutb/ns-ctfutb-tf_langbariteminfo)要求 Windows 8 起的模式项使用该 GUID。这个修正有明确依据，但仍需真实 TSF 应用验证。

运行结束后，应删除本轮生成的 `output/Probe/`、`msbuild/Probe/`、`output/ProbeDiagnostics/` 和 `msbuild/ProbeDiagnostics/`。递归删除前须确认解析后的绝对路径在当前工作区内且不是链接目录；只保留本目录源码及排查记录。

## Win32 与类别对照（2026-10-09）

Win32 构建将上述两个 MSBuild 命令的平台改为 `/p:Platform=Win32`，诊断及同目录 DLL 位于 `output\ProbeDiagnostics\Win32\`。项目按平台复制 amd64/x86 manifest。只在探测 DLL 的 x86 构建中为八个诊断导出增加不带修饰的别名；针对固定测试 CLSID 的诊断要求全部存在，否则退出失败，不能把缺失当作旧 DLL 而跳过测试。

Win32 `--documents`、一次 `--cycles` 和用户确认的桌面 `--input` 通过；x64 `--documents` 回归通过。运行边界及对象计数见验证记录，完整系统安装仍未验证。

类别对照请通过 `run-keyboard-category-test.ps1`，例如管理员 PowerShell 中：

```powershell
& .\test\ProbeActivation\run-keyboard-category-test.ps1 -Mode cycles -Count 1 -Transcript
```

脚本当前调用 x64 诊断。`-Mode input` 为一次交互测试，`-Count` 仅适用于重复 cycles，`-Transcript` 在实验输出目录保存可清理记录。对应 `--keyboard-cycles` / `--keyboard-input` 会通过 `RegisterCategory` 临时写入固定测试 CLSID 的键盘类别，没有 `LOCALPROCESS` 限定；这些模式不能直接按常规配置模式的隔离边界理解。

脚本要求原先无该测试 CLSID 的 32/64 位 HKLM/HKCU COM/TIP 键，通过互斥防止重复启动，在子进程退出后仅清理该测试类别根键，遇到意外 COM/配置或其他数据则拒绝删除；每轮核对原版 COM 路径及用户输入法列表不变。不要直接运行类别模式而绕过脚本的异常退出清理。

此前同一进程的非管理员注册失败；用户授权 UAC 后四轮注册和清理成功，但类别/当前键盘身份联合检查全部失败，未执行连续切换，不能宣布对照成立。后续新进程的分项结果见下节。该入口用于后续诊断，不注册 Windows 服务；隐藏配置模式的新增注册边界见下文。

### 类别可见性与隐藏配置对照

后续类别模式改为父进程注册、全新子进程验证，子进程日志保存为同平台输出目录的 `FreshCategoryChild.log`。本机实测父进程刚注册后的类别查询不匹配，新子进程则能匹配键盘类别；但子进程的 `LOCALPROCESS` 配置类别仍为 `GUID_NULL`，当前键盘配置仍非探测服务。不能把这个环境当作正常键盘 TIP，也不能只用父进程的类别查询判断注册失败。

需要明确授权时才执行临时隐藏配置对照：

```powershell
& .\test\ProbeActivation\run-keyboard-category-test.ps1 -Mode cycles -Count 3 -HiddenProfile -InputAfterCycles -Transcript
```

`-HiddenProfile` 由父进程先注册 `GUID_TFCAT_CATEGORY_OF_TIP`、`GUID_TFCAT_TIP_KEYBOARD`，并通过 `RegisterProfile(TF_RP_HIDDENINSETTINGUI)` 写入固定测试 CLSID 的配置记录，**不是进程内注册**。父进程保留配置直到全新子进程退出；子进程只读取和尝试激活，不重复注册。没有系统 COM 注册或用户输入法列表写入，激活仅用 `FORPROCESS`，禁止采用 `FORSESSION`、`ENABLEPROFILE` 或添加日常列表。`-InputAfterCycles` 只在自动检查全部通过后运行一次桌面输入测试。每轮移除旧子进程日志，再保留本轮日志；随后撤销配置/类别并核对残留。脚本只能清理固定测试 CLSID、固定简体中文语言和固定 profile GUID 的临时数据，遇到其他身份则拒绝删除。

用户确认后已在本机执行隐藏模式，仍未通过。父进程注册/读取成功；新子进程的旧接口能读取启用状态及描述，注册表记录存在，但类型化 `GetProfile` 和后续 `ActivateProfile(FORPROCESS)` 返回 `E_FAIL`，没有新增的 DLL 服务激活。不能把之前模拟测试残留的阶段编号解释成本次初始化失败位置。未进入连续切换或中文输入。

初次实际激活计数从 `TestLocalProfile` 开始记录，覆盖注册、线程管理器激活和设置焦点；仅包围 `ActivateProfile` 会漏掉此前已经完成的合法激活。必须新增激活且初始化结果成功，才能使用该阶段输出作为真实激活证据；类别模式还必须通过配置类别和当前键盘服务身份检查。修正后的常规 x64 `--documents` 通过，x64/Win32 诊断构建通过。完整注册测试计划见 [`ISOLATED_INSTALLATION_PLAN.md`](../../docs/ISOLATED_INSTALLATION_PLAN.md)。

注销使用独立的 `TF_URP_LOCALPROCESS` 或全局单配置的 `0`；不能复用注册标志。SDK 中 `TF_RP_HIDDENINSETTINGUI` 与 `TF_URP_ALLPROFILES` 恰好同为 `0x2`，复用会意外扩大注销范围。修正后的常规 x64 合成文档回归通过。
