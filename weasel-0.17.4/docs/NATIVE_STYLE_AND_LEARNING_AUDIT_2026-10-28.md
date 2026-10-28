# 原生配色与用户词典复用核查（2026-10-28）

## 结论与范围

候选框换色、用户词频学习均已存在，当前独立测试版的配置也保留了这两项能力，不新增皮肤引擎或高频词数据库。本轮只核查源码、读取独立测试版的配置和词库文件元信息、运行合成词库检查；没有更换安装文件、打开设置窗口、切换输入法、启动模型或读取用户词库正文。

| 功能 | 源码与安装证据 | 开发决定 |
| --- | --- | --- |
| 候选框皮肤/换色 | `UIStyleSettings` 枚举并选择配色，配置器保存自定义补丁并重新部署，`RimeWithWeasel` 加载颜色，`WeaselUI` 绘制。测试版编译后配置选中 `aqua`，含 36 套预设及对应 36 张安装预览图 | 复用原有设置和渲染，不重做 |
| 常用词、习惯短语及调频 | Rime 提交通知更新本地用户词典；测试版 `luna_pinyin_simp` 使用 `script_translator`、`dictionary: luna_pinyin`，没有关闭 `enable_user_dict`，引擎默认值为 true；存在独立 `luna_pinyin.userdb` | 沿用 Rime 学习，不增加全文记录或第二套词频库 |
| 用户词典管理 | 原托盘菜单连接 `WeaselDeployer /dict`，已有备份、恢复、文本导入/导出；`/sync` 为原用户数据同步入口 | 沿用原入口；不能把这些操作称为已经提供“一键清空全部学习” |
| 自定义固定短语 | 方案中已有 `table_translator@custom_phrase`，其 `user_dict: custom_phrase`、`db_class: stabledb`；独立测试用户目录本轮未发现自定义短语文件 | 已有配置扩展点，不等同于已导入用户短语，也不影响主拼音词频学习 |
| 纠错提示框的主题 | `CorrectionHintView.h::PaintHint` 使用系统窗口色和蓝色强调，支持高对比度分支；由 `AutomaticCorrection` 独立绘制，没有读取原候选框的 `UIStyle` 配色 | 尚未跟随候选皮肤；未来只补主题衔接及回归，不建立另一套预设库 |
| 明确保留某表达 | 已有 `CorrectionLexicon`，由用户明确操作保存并在本地过滤建议；不更新 Rime 候选词频 | 保持独立语义，复用现有实现 |

“配置启用”与“当前进程已打开词库”不同：本轮没有启动专用后端来探测实时数据库状态。词库文件存在也不能证明某一句话的学习次数。皮肤切换窗口本轮未人工操作，不能把源码和配置核查写成最新交互验收。

## 实现依据

- `WeaselDeployer/UIStyleSettings.cpp`：读取 `preset_color_schemes` 和 `style/color_scheme`，通过自定义设置 API 保存所选配色；预览优先取用户目录，其次共享数据目录。
- `WeaselDeployer/UIStyleSettingsDialog.cpp`、`Configurator.cpp`：配色列表、预览、确认保存及工作区重新部署。原托盘“输入法设定”进入这套流程，“用户词典管理”进入原词典管理界面。
- `RimeWithWeasel/RimeWithWeasel.cpp::_UpdateUIStyleColor` 与 `WeaselUI/WeaselPanel.cpp`：把配置颜色装入候选框样式并绘制。已有字体、横竖布局、边框、圆角等配置，不应再次复制一套候选框样式系统。
- `librime/src/rime/dict/user_dictionary.cc::UserDictionaryComponent::Create`：`enable_user_dict` 缺省 true；未另设名字时从主词典名得到用户词典名。
- `librime/src/rime/gear/memory.cc::OnCommit`：由确认提交的候选产生学习；`script_translator.cc::Memorize` 更新整段拼音短语，`table_translator.cc` 亦有原生学习路径。
- `user_dictionary.cc::UpdateEntry` 保存提交次数与动态信息，`CreateDictEntry` 根据这些信息计算候选权重。它是输入候选学习，不是任意文档全文缓存，也不是模型权重训练。
- `include/CorrectionLexicon.h`、`WeaselDeployer/CorrectionWorker.cpp`：明确保留表达走既有本地建议过滤，不把整张保留词表发给模型，不参与 Rime 调频。

安装核查位置为独立测试目录 `C:\Program Files\WeaseLC-LocalTest\data`、`C:\Users\lenovo\AppData\Roaming\WeaseLC-LocalTest\build`；没有修改日常 `Rime` 用户目录或搜狗。词库仅检查目录和文件信息，没有导出或搜索测试用户实际输入记录。

## 本轮验证

使用已有 `output/LocalTest/x64/WeaselDeployer.exe` 和 `output/LocalTest/Win32/WeaselDeployer.exe` 的 `/rime-learning-selftest`。该分支在普通配置器初始化之前执行，分别使用全新 `output/RimeLearningTests/<架构>/<进程号>/` 合成配置和词库，不连接系统输入会话。

每架构的 `table_translator`、`script_translator` 各 12 项，共 **24 项通过、0 项失败，退出码 0**。验证静态初始排序、5 次明确选择提交、引擎结束后从磁盘重载、习惯词升为首选及导出计数确为 5。两种 translator 都验证，不能据此承诺任意第三方方案均启用学习。

日志：`output/FeatureAudit-20261028/learning-x64.log`、`learning-Win32.log`。微型方案没有配置 ASCII 绑定，标准错误输出有 `Missing ascii bindings`，不影响上述学习断言；没有据此更改生产方案。

## 后续任务边界

1. 候选框换色和基础高频词学习视为复用完成；不新增数据库，不重写排序，不重复统计每次上屏。
2. 如推进纠错框皮肤，复用现有配色来源，分别验证浅色、深色、高对比度、预览与实际显示一致，并保持不抢焦点和现有 Tab 校验逻辑。该衔接本轮尚未实现。
3. 保留表达、临时纠错上下文和 Rime 用户词典是三类数据。清理界面须明确删除对象，不把“清空纠错保留词”称作“清空输入习惯”，不自动删除日常词库。
4. 高频不代表正确：反复输入“这里有错子”可能被 Rime 学成候选，但不能因此自动列入纠错白名单。历史现象与原生学习机制一致；未读取旧词库逐项记录，不推断具体次数或来源条目。
5. 本轮无二进制修改，无需重装或释放 DLL。Word 继续仅普通输入；上一轮测试纠错与模型已关闭的状态不因本次核查改变。
