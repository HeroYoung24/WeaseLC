# IPC 安全与部署路径检查

Release x64 / Win32，每架构 29 项。只建立带当前进程号的 `WeaseLC.Security.*` 临时管道和自有消息窗口，不使用日常输入法的管道名，不注册/激活输入法。

覆盖：安装/开发路径区分、非法路径拒绝、有效描述符、登录身份、ACL/包创建实例权限、匿名拒绝、模拟身份等级、身份恢复、繁忙连接期限、16 个并发客户端×20 请求的缓冲区隔离、正文终止符/标量清空、慢回复不占全局 API 锁、监听/空闲/回复阻塞回收、同步窗口回调完成和句柄释放。

```powershell
$env:VCTargetsPath = 'E:\Microsoft Visual Studio\MSBuild\Microsoft\VC\v170\'
Remove-Item Env:PATH -ErrorAction SilentlyContinue
& 'E:\Microsoft Visual Studio\MSBuild\Current\Bin\MSBuild.exe' test\SecurityChecks\SecurityChecks.vcxproj /p:Configuration=Release /p:Platform=x64 /m:1
./output/SecurityChecks/x64/SecurityChecks.exe
```

真实跨用户登录、受限包/浏览器宿主兼容和服务 UI 的长时间并发行为仍需虚拟机验收。基础客户端已连接后的同步事务仍依赖服务响应；本轮没有把整个 Rime 通道改成异步协议，Rime 引擎本身长期卡死时的降级仍需专项验证。
