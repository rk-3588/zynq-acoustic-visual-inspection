# 2026-10-31 UI 自动恢复补丁

已部署最终v2（10秒冷却），同步本地和虚拟机PetaLinux源码。未重新生成或替换BOOT.BIN、image.ub、设备树、rootfs.ext4；未重启Linux。

## 最终规则

每秒独立、只读采样真实VPP SOF计数；UI进程有效且图层开启、摄像头MM2S/S2MM运行且错误掩码为0时，连续3个完整有效区间落在72～90次/秒，调用正常UI stop/start。每次尝试结束后冷却10秒，不设恢复次数上限，不额外归档故障日志。

计数复位、采样异常、UI更换、UI关闭和采集错误均打断累计。恢复与手动服务操作使用同一个服务锁，带UI和看门狗身份检查；停止失败不强杀、不继续启动。看门狗不读Mixer ap_ctrl/ISR，不干扰UI完成事件。

服务首次开机只启动UI一次，旧固定二次启动逻辑已删除。UI关闭同时停止看门狗，晚到恢复请求不能复活已经手动关闭的UI。

## 测试

- 策略原生及ASan/UBSan通过：3区间累计、72/90阈值、正常60、10秒冷却、超过3次仍可恢复、UI/相机错误保护、计数回卷/复位、漏采样。
- 模拟UIO的独立进程完整测试通过：摄像头有错不调用恢复，清错后持续80/s触发一次服务调用，之后冷却；重复监控进程被锁拒绝。
- 服务生命周期在dash和BusyBox ash各9项通过。冷却变更未改变服务脚本，相关测试未重复运行。
- ARM交叉编译通过，`--check` 在板端真实UIO通过。

## 板端实测

新看门狗首次安装时保留现有UI进程2372，未主动重开UI。用户随后浏览相册复现竖纹，并确认自动恢复；状态记录看门狗进程3041保持、`recovery_attempts=1 last_result=0`，UI变为3157，SOF约60/s、VDMA错误为0。初版30秒冷却实测倒计时为28985→23985→18985毫秒等。

收到用户“改10s”后，核心版本升为v2，重新执行受影响的策略与监控进程测试并交叉编译。仅停止、替换、启动看门狗，确认UI进程号不变；读回v2版本，UIO采样约60/s，摄像头和声学VDMA错误均为0。10秒的完整冷却行为在模拟进程测试验证；没有为实测10秒而人为制造板端竖纹。

部署过程中保留板端较新的UI v30、robot v7，未回退到此前v29/v6；UI SHA256 `209d9a1fb5e0e6537106afec7170bc02c3be8feceb6e3ae88575a5d4570b91a8`。机械臂自动模式关闭、无活动事务；UART1 TX/RX在本轮开始前已为8/8，全程保持8/8，没有发送机械臂指令。

## 产物和回退

- `/usr/bin/video_ui_watchdog` v2 SHA256：`1f6502ecd7747eb3e5efd3fcfc942b33ff210969a1ff2bdc07240207c5908259`。
- `/etc/init.d/video-status-panel` SHA256：`baaf7f80511f8e4d8b5490d3c50d3cf5f5719bafed788fc51fbf7c38ac4bce0d`。
- 板端旧服务脚本备份：`/var/lib/video-pipeline-backups/before-ui-watchdog-20261031.xpcYo26t/video-status-panel`；同目录保留初版30秒看门狗程序。仅备份程序，不复制故障日志。
- VM配方原文件备份：`<petalinux_project>/diagnostics/20261031/ui-watchdog-before.jFTMtrpJ`。核心头文件后续已同步到10秒版；源文件旁的`.before10s`保留30秒版头文件。
- 实际启动仍为热图翻转BOOT（SHA256 `afb669a7...`）和降频/翻转摄像头初始化器（`60577766...`）。

看门狗是恢复措施，不代表底层输出失步根因已修复。黑屏若不呈现规定SOF异常，或摄像头同时报错，则不会触发该补丁。

本地编译、测试与部署记录：`build-v1.log`、`build.log`、`monitor-test.log`、`deploy.log`、`observe.log`、`cooldown-update.log`、`deployed.json`。这些为开发验证记录，板端看门狗不保存此类历史日志。
