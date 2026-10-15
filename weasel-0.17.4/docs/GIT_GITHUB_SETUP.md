# 本地基线与 GitHub 关联

当前状态：用户已推送初始提交 `5ebfad0`，`main` 跟踪 `origin/main`，主仓库地址为 `https://github.com/HeroYoung24/WeaseLC.git`。以下“未关联”的文字描述创建初始基线时的状态；后续按用户指示在 `main` 继续提交。

## 本地仓库

本仓库以 2026-10-15 当前文件状态建立新的 `main` 初始提交，仓库根为 `E:\WeaseLC`。此前的开发已经包含在这个快照中，没有此前逐次修改的提交历史。未设置主仓库远程地址，未创建或推送 GitHub 仓库。

跟踪源码、测试源码、文档、原有资源和模型部署脚本/摘要/许可证。模型权重、便携运行时、下载缓存、日志、机器专用 `env.bat`/`weasel.props`、下载依赖和本机编译结果被忽略，仍保留在磁盘上。部分上游随附小型二进制是发行所需原始资源，保留在基线中。

`librime` 固定为 `1c23358157934bd6e6d6981f0c0164f05393b497`，`plum` 固定为 `cab9ed35289f2022b6888503750d2dfb1851dbe0`。递归依赖的已跟踪源码没有本地修改；现有脏状态来自未跟踪构建目录，以子模块 `ignore=untracked` 忽略，不隐藏已跟踪文件修改。

## 在 GitHub 创建空仓库

1. 登录后打开 [新建仓库](https://github.com/new)，填写名称，例如 `WeaseLC`。
2. 按需要选择 Public 或 Private。
3. 不勾选初始化 README、`.gitignore` 或许可证，本地已有这些内容。
4. 点击 Create repository，然后复制 HTTPS 地址。

这套已有代码导入流程依据 [GitHub 新建仓库说明](https://docs.github.com/en/repositories/creating-and-managing-repositories/creating-a-new-repository) 和 [本地仓库推送说明](https://docs.github.com/en/migrations/importing-source-code/using-the-command-line-to-import-source-code/adding-locally-hosted-code-to-github)。

## 关联与首次推送

在 PowerShell 中运行，把地址替换成自己的仓库地址：

```powershell
Set-Location E:\WeaseLC
git remote add origin https://github.com/YOUR-NAME/WeaseLC.git
git remote -v
git push -u origin main
```

首次推送按 Git 凭据管理器的登录提示完成认证。如果已经配置了 SSH，也可以使用 GitHub 提供的 `git@github.com:YOUR-NAME/WeaseLC.git` 地址。仅添加 remote 不会上传文件，`git push` 才会上传。

如果 `git` 未在 PATH 中，本机现有可执行文件为：

```powershell
$gitExe = 'C:\Users\lenovo\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\git\cmd\git.exe'
& $gitExe -C E:\WeaseLC status
# 后续同理，将 git 命令改为 & $gitExe ...
```

## 后续本地保存与克隆

```powershell
git status
git add .
git commit -m "描述本次修改"
git push

# 新机器克隆时带上依赖：
git clone --recurse-submodules https://github.com/YOUR-NAME/WeaseLC.git
```

提交只保存本地历史，推送才同步到 GitHub。主仓库不会替代模型和大型依赖的备份；模型可按固定版本和 SHA256 重新下载。子模块需要分别管理其源码变更，不能仅提交主仓库就认为子模块内未提交的修改也已保存。
