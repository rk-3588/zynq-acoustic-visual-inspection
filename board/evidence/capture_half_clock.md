# 2026-10-30 抓拍、相册与摄像头降频对照

结论：本轮降频后采集错误为零，命令行与触摸各成功抓拍一次，触摸相册列表、照片详情及详细数据实测正常。首次启动仍黑屏，正常重开 UI 后恢复；采集异常与公共显示链路异常应分别处理。用户选择暂时保留降频配置。

## 当前板端配置

- 启动 ID：`09e09f6a-184f-4c80-a164-e4660e5e5291`。
- 摄像头接线仍为 30 cm，PCLK/GND 绞合；本轮未改接线。
- 临时初始化器：`20261030-video-init-half-clock-diag-v1`。
- 初始化表唯一改动：OV5640 `0x3035: 0x11 → 0x21`，已 SCCB 读回 `0x21`。
- 输出尺寸仍为 1280×720，PCLK 极性 `0x4740=0x20`，原驱动强度保持。VTC、HDMI 输出时序、FPGA、声学算法未改。
- 该分频预期使采集速率约减半；未用示波器实测本轮 PCLK/VSYNC，不能把下述 VPP 计数当作传感器 FPS。
- UI v29、snapshot v3、album v2、只读显示诊断已部署；常规版本哈希见 `../snapshot_release_v29/manifest.json`。
- 临时初始化器 SHA256：`8e67821c84ab494c7a77526c63a09bec7107b2380f5d837f9cf11b9a71890edd`。
- 原初始化器备份：`/var/lib/video-pipeline-backups/before-half-clock-20261030.yZ3xweU6/video_pipeline_init`。
- 原版 SHA256：`4b405cdb1da8de50fa022abd971d7cb04923b06dd154018177a27301821caa01`。
- 临时程序已安装至板端 `/usr/bin/video_pipeline_init`，下次重启继续生效。**VM 常规配方和常规发布包仍是原速初始化器**，重建/重写根文件系统会覆盖这项临时设置。

## 采集对照

原速、画面正常时仍出现相机 S2MM `0x15190`，本轮曾到 `0x1d190`。包含 Internal、SOF Early、EOL Early，后者还包含 EOL Late。独立、限次 W1C 对照中，清零后 0.5 秒内又出现新错误，故不能仅解释为历史粘滞位。snapshot v3 在这些状态拒绝 ready 是有效保护。

降频重启后启动状态记录 clean frames。约 17 秒到 314 秒的多次采样中，相机 S2MM 一直为 `0x11000`，错误掩码为 0，写槽继续轮换；未清除该次启动的错误位。最后状态 `capture_ready=true`、`capture_reason=ready`。

两次实际保存：

| 入口 | ID | 耗时 | 结果 |
| --- | --- | --- | --- |
| 命令行 | `shot-09e09f6a-76306-XXpxlvEa` | 779 ms | 保存，计数 4→5 |
| 触摸按钮 | `shot-09e09f6a-230238-XX8I0qqr` | 886 ms | 保存，计数 5→6 |

两遍像素比对、250 µs 观测间隔保护均未放宽。上述任务分别经历 1 次、4 次复制保护间隔拒绝后重试成功；回执中的 `max_gap_ns` 是所有尝试的最大值，不代表成功那一次接受了超限间隔。最终没有覆盖、像素不一致、几何变化或错误状态。

## 相册验证

- 后端列表扫描正常，首次新照片后 total=5、bad=0。
- 连续 3 次列表页读取成功；缩略图缓存命中，单次构建约 10～23 ms。
- 新拍照片原图读取、CRC 校验、640×360 缩放通过；后端详情构建约 128 ms。
- 用户随后实际触摸抓拍、打开列表、照片详情、详细数据、关闭相册，确认正常。
- UI 日志：`CAPTURE_RESULT ok=1`、`GALLERY_RESULT kind=1 ok=1`、`GALLERY_RESULT kind=2 ok=1`；详情构建约 126 ms，管道交付约 221 ms。

## 尚未解决的公共输出故障

降频首次启动仍有 HDMI 信号但整屏黑，包括 UI；当时采集完全无错且命令行抓拍成功。VPP 上游接受 SOF 约 80/s。

只执行 UI 服务的正常 stop/start，没有重启相机、VDMA、VTC：

| 状态 | Mixer layers | VPP 接受 SOF | 相机采集错误 |
| --- | --- | --- | --- |
| 整屏黑时 | 3 | 约 80/s | 0 |
| 正常关闭 UI | 1 | 约 60/s | 0 |
| 同版 UI 重开 | 3 | 约 60/s | 0 |

用户确认画面恢复，之后抓拍和相册实测正常。最后约 314 秒状态 VPP 约 60/s、温度 63.6°C。板端没有可读的 Video Out locked/underflow，故这只是异常同步/反压的间接证据，不能把 VPP 当 HDMI 物理帧率，也还不能断言 Mixer IP 本体有 bug。

## 判断边界与下一步

降频改善结果支持采集时序/传输裕量不足，但同时降低了 DVP 速率与写 DDR 负载，尚不能仅凭此唯一归因于 30 cm 线缆。此前短线恢复、PCLK/GND 绞合改善是支持外部信号裕量问题的独立线索。

目前适合保持该临时配置做使用验证，单独继续排查 UI 首次启用及输出同步。不能将本次约 5 分钟测试等同于长期稳定保证；也未证明原速抓拍已经修复。本轮不继续改变 FPGA。

机械臂自动模式始终关闭，UART1 TX/RX 从重启后到本轮结束均为 0，没有发送机械臂指令。

## 回退

`restore_camera_clock.sh` 是交付脚本，**未执行**。以 root 在板端运行时，会先核对当前临时程序及备份的哈希，再原子恢复原初始化器；随后需正常重启才改变传感器时钟。它不会在线切换 PCLK，也不会自动重启。

本地 `video_init_half_clock.c/.arm` 和 `build_half_clock.py` 保留了临时程序源码、产物及构建方法；`board_state.json` 记录部署路径与选择。

主要原始日志：`manual-observe.log`、`capture-full-clock.log`、`ack-startup.log`、`reboot-v29-first.log`、`reboot.log`、`geometry.log`、`capture.log`、`album-load.log`、`ui-recover.log`、`observe.log`、`record.log`。后几项为本轮降频启动。
