# WeaseLC 第三方组件

WeaseLC 基于 Weasel 0.17.4，原版作者为 RIME Developers，来源为 https://github.com/rime/weasel。
保留原作者版权声明，主程序许可证见 LICENSE.txt。
本版本增加本地模型纠错、提示与队列、设置界面，以及独立安装身份和启动程序。
本分发改造日期为 2026-11-09，修改者为 WeaseLC contributors。

| 组件 | 用途 | 许可文本 |
| --- | --- | --- |
| librime | 输入法引擎 | licenses/librime.txt |
| Boost 1.84.0 | C++ 基础库 | licenses/boost.txt |
| glog、LevelDB | 日志及词典存储 | licenses/glog.txt、licenses/leveldb.txt |
| yaml-cpp、marisa-trie | 配置及词典 | licenses/yaml-cpp.txt、licenses/marisa-trie.txt |
| OpenCC | 字形转换 | licenses/opencc.txt |
| WTL | 原生窗口界面 | licenses/WTL-MS-PL.txt |
| NSIS | 安装程序及插件 | licenses/NSIS-COPYING.txt |
| Rime 方案、字典和语言数据 | 基础输入方案 | licenses/data-*.txt，原 YAML 中的来源说明同时保留 |

源码位置和本地提交记录参见仓库 docs/BUILD_BASELINE.md，子模块位置见 .gitmodules。
distribution-manifest.json 列出本包文件和哈希，并标识源码提交及是否存在未提交修改。

对应源码发布包需要覆盖实际使用的引擎、静态链接组件和方案数据，
并附上 packaging/windows/Build-Distribution.ps1 及安装脚本。
公开发布前须将准确的对应源码下载地址写入发布页。

本包不包含模型权重。用户另行取得的模型、推理运行时和依赖以各自许可为准。

WTL 版权所有 Microsoft Corporation、WTL Team。随附 MS-PL 原文取自
https://raw.githubusercontent.com/spdx/license-list-data/main/text/MS-PL.txt。
