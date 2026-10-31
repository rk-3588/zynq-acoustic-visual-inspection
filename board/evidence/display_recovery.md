# 相册详情期间整屏竖纹：现场核查与恢复

日期：2026-10-30，Asia/Singapore。新热图翻转BOOT部署后的同一次启动，boot_id为 `192d56d0-5bc9-44b9-97e2-cc9c75d03a74`。

结论：故障范围已收窄到启用UI叠加时的公共显示通路，根因仍待底层观测。相册原图读取期间的突发DDR访问触发输出欠流或同步异常，是当前优先验证的假设；本轮没有直接读到Video Out欠流/锁定信号，不能将该假设写成已证实根因。

## 现场证据

- 用户确认整屏竖纹，顶部、底栏及侧栏文字一并损坏。
- BOOT、摄像头初始化器、UI v29、相册v2的板端SHA256均匹配本轮发布版本。摄像头保持降频及水平翻转配置。
- 摄像头与声学VDMA的MM2S/S2MM错误掩码均为0；摄像头写指针继续轮换；抓拍预检为ready，错误掩码为0。
- 相册列表和连续四次详情均返回 `GALLERY_RESULT kind=2 ok=1`。最后一次打开列表第四项（action=13）后，后续日志开始持续显示VPP接受SOF约80/s；此前为约60/s。
- 四次详情均通过原图读取、CRC校验、640×360缩放与固定长度IPC校验。最后一次构建约228ms、管道交付约206ms，`read_us=121104 crc_us=41298 reduce_us=56665 bytes=1161672`。
- UI进程仍刷新、双缓冲仍切换，无FLIP_TIMEOUT、FREE-buffer invariant、初始读回失败或退出记录。可用内存约912MiB；温度约62～64°C。

VPP计数是在摄像头MM2S后的视频处理入口处统计成功握手的SOF，**不是传感器帧率，也不是HDMI物理刷新率**。它在此用于比较公共流水线的节奏变化。

## 对照与恢复

| 阶段 | Mixer layers | VPP接受SOF | 采集错误 | 物理画面 |
| --- | --- | --- | --- | --- |
| 故障，UI继续刷新 | 3 | 约80/s | 0 | 用户确认整屏竖纹 |
| SIGSTOP暂停UI进程约2.5秒，保持图层与缓冲不变 | 3 | 仍约80/s | 0 | 未另作物理确认，异常计数未恢复 |
| SIGCONT后正常停止UI服务 | 1 | 约60/s | 0 | 用户确认竖纹消失、摄像头连续更新 |
| 正常启动相同哈希的UI v29 | 3 | 约60/s | 0 | 用户确认完整画面恢复 |

只对UI服务做上述暂停、恢复与正常stop/start；没有重启Linux、摄像头、VDMA、VTC或PL，没有清除错误位，没有替换程序或启动包。机械臂自动模式关闭、无活动事务，UART1 TX/RX始终为0。

暂停CPU刷新仍维持异常节奏，说明故障发生后已持续存在于显示流水线；这排除了“只要停止当前绘制就立即恢复”的解释，不能据此独立排除先前的缓冲数据或事务问题。

## 代码和硬件核查

相册后端 `petalinux-video/camera_album.c` / `camera_album_core.h` 读取SD文件、校验、缩放并输出管道包，不打开/dev/mem或UIO，也不直接写Mixer/VDMA/VTC。UI使用固定尺寸预览，并对目标显示区域进行边界检查。现有静态核查未发现相册路径新增的显示寄存器写入或明显图像复制越界；这不是形式化的内存安全证明。

UI两缓冲为 `0x03000000`、`0x03400000`，各使用1280×720×4字节、槽间距4MiB，位于预留no-map DDR内，和摄像头/声学三缓冲不重叠。Linux堆与相册包在常规内存中。双缓冲按两个新的ap_done事件退役，正常刷新只更新缓冲指针。

已核对本次bit独立构建输入的BD/XCI，配置归档在 `hardware-observability.json`：

- 两路VDMA共四个DDR读写主接口与Mixer UI内存读接口，经同一AXI互连、协议转换器进入PS HP0；HP0为64位、150MHz。
- UI为整屏RGBA，即使软件刷新降到5Hz，Mixer在约60帧/s时仍需读取约221.184MB/s的UI像素。该数字是流量估算，不是已测总带宽或饱和证据。
- Video Out为异步时钟、1像素/周期，`C_ADDR_WIDTH=10`，FIFO深度1024像素；视频时钟74.25MHz、AXI时钟150MHz。
- Video Out的locked、underflow、overflow、status、fifo_read_level没有接到可读接口，也未连接到本轮BD的观测网络，故当前软件无法读取这些指标。

AMD说明Video Mixer的输入/输出支持反压，数据源不足会在内部缓冲耗尽后停止有效输出；Video Out的FIFO容量需覆盖上游停顿，不能仅凭平均时钟更高判断无欠流。见 [PG243 Throughput](https://docs.amd.com/r/en-US/pg243-v-mix/Throughput)、[PG044 Buffer Requirements](https://docs.amd.com/r/en-US/pg044_v_axis_vid_out/Buffer-Requirements)、[PG044 AXI4-Stream排查](https://docs.amd.com/r/en-US/pg044_v_axis_vid_out/AXI4-Stream-Interfaces)。

这些资料与现场现象支持优先排查DDR访问停顿和视频输出同步，仍不能证明一次具体欠流已经发生，也不能证明扩大FIFO就一定修复。

## 下一步

1. 根因验证优先取得Mixer输出到Video Out的TVALID/TREADY/TUSER/TLAST、Video Out的locked/underflow/fifo_read_level/status与VTC同步信号。区分数据供应停顿、边界错误和同步状态异常，再决定FIFO或AXI配置修改。
2. 软件缓解可评估为每张截图保存640×360预览缓存，减少每次详情页重新读取2.76MB原图、CRC及缩放；另可评估UI写DDR的分批提交以减少突发负载。这些措施需要单独A/B验证，本轮未实施，不能代替根因证据。
3. 不应仅删除ready保护或屏蔽VDMA错误；本次采集原本无错。启动时重开一次UI也不能覆盖本次运行期间触发的异常。

## 日志位置

板端完整故障日志备份：`/var/lib/video-pipeline-backups/gallery-stripes-20261030.sSwegNA5/`。

本地：`fault.log`（故障状态、相册时间线、哈希、VDMA、内存、内核日志）、`ui-freeze-stop.log`（暂停与关闭对照）、`ui-restore.log`（同版UI恢复）、`restored-observe.log`（恢复后再次观察）、`hardware-observability.json`、`summary.json`。
