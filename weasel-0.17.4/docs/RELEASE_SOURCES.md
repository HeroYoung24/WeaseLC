# 发布物、许可证与对应源码

## 发布页材料

- 安装包、SHA-256、版本说明和适用系统。安装包内的 `distribution-manifest.json` 记录源码提交及每个文件的哈希。
- 该包对应的源码下载，包含 WeaseLC 改动、构建和安装脚本、实际使用的引擎、链接依赖及输入方案数据。发布页在安装包旁列出对应源码的链接。GitHub 自动生成的源码压缩包需要补齐子模块和构建时另取的源码。 [GPL v3 第 1、5、6 节](https://www.gnu.org/licenses/gpl-3.0.html)
- [主许可证](../../LICENSE)、[第三方组件清单](../packaging/windows/THIRD_PARTY_NOTICES.md)、[模型来源和许可](../../local-model/README.md#来源与许可)。源代码和发布物保留原作者声明。

## 构建来源

主仓库的 `.gitmodules` 固定 librime、plum。引擎依赖版本见 [构建基线](BUILD_BASELINE.md)。对应源码还应包含由 plum 取得、实际编译进该包的方案和词库，以及 WeaseLC 的本地补丁。发布前在干净目录重建并对照清单，确认资产与源码一一对应。

| 方案与词库 | 当前构建使用的提交 |
| --- | --- |
| [rime-bopomofo](https://github.com/rime/rime-bopomofo) | `6085c9a38a4a728047862b33d67eee18aa86f3b9` |
| [rime-cangjie](https://github.com/rime/rime-cangjie) | `52d90a1b1312e74042b38c1cbc8142defbc53171` |
| [rime-essay](https://github.com/rime/rime-essay) | `054920de4f54c9e5994276a96a4fc2a35cb51aa3` |
| [rime-luna-pinyin](https://github.com/rime/rime-luna-pinyin) | `56b934b099dfbeab842320f13aa8b461a6ab3e42` |
| [rime-prelude](https://github.com/rime/rime-prelude) | `082425ea0684bca36474415d4a0e8db9b016487e` |
| [rime-stroke](https://github.com/rime/rime-stroke) | `1e8fff9b9494ddec23b0cbc526bcfd8171a6fd48` |
| [rime-terra-pinyin](https://github.com/rime/rime-terra-pinyin) | `723e51bc266cf9464530c1ddedb856aa18e3da34` |

仓库的历史构建工具包括 [7-Zip 24.05](https://github.com/ip7z/7zip/tree/24.05)、[curl 8.7.1](https://github.com/curl/curl/tree/curl-8_7_1) 和 WinSparkle 两个版本。分发包按当前清单只装应用文件与模型准备脚本。历史工具的许可另列于第三方声明。
