# Linux与PL接口速查

下列地址对应本包冻结的BD和设备树。UIO次设备号按探测顺序分配，不应硬编码 `/dev/uio0`；应用按名称和物理地址确认身份后打开对应设备。

| 物理基址 | 设备 / 用途 |
| --- | --- |
| 0x40000000 | add_image，声学/相机叠加 |
| 0x40010000 | color_mapping，色阶参数 |
| 0x40030000 | acoustic_capture，单通道2048点冻结快照 |
| 0x40040000 | video_preprocess，图像模式、ROI及统计 |
| 0x43000000 | 声学VDMA |
| 0x43c10000 | VTC |
| 0x44a00000 | 摄像头VDMA |
| 0x44a20000 | acoustic_ctrl，热点和控制状态 |
| 0x44a30000 | Video Mixer，RGBA UI图层 |

完整寄存器语义以对应RTL和应用头文件为准；表格不授权另一个程序同时接管正在运行的硬件。视频初始化、显示策略、UI和看门狗有各自的所有权与锁。

`pl_control_daemon`读取声学控制窗口中的0x04状态、0x08/0x0c热点横纵坐标、0x10峰值及0x14历史命名为confidence的字段。当前读取逻辑要求后两者相同；它们不能直接解释为百分比置信度或dB SPL。软件还融合门控文件的新鲜度和识别开关，输出实时/稳定定位。坐标转换与有效性见 `src/ps/control/pl_tracking.h`；寄存器不是有帧号锁存的原子快照。

DDR的0x01000000～0x03ffffff为设备树预留no-map视频区；UI两槽分别从0x03000000、0x03400000开始。不要让普通Linux内存分配器或其他DMA用户占用这些区域。

用户态主要接口：

| 路径 | 内容 |
| --- | --- |
| /run/pl-control.sock | 声学状态与定位请求 |
| /run/acoustic-display.state | 静音门控、色阶和识别开关确认状态 |
| /run/acoustic-frequency.state | CH0 FFT频率状态 |
| /run/video-pipeline.state | 视频启动阶段与错误 |
| /run/robot-control.sock | 独立机械臂任务服务 |

UART1使用9600、8N1，参数在 `src/ps/video/video_robot_service.c` 的串口初始化中设置。`robot-regions.conf`只配置左/中/右映射与任务超时，不设置波特率。F16为RX、F17为TX，连接前须核对开发板实际引出与控制器逻辑电平。协议解析见 `video_robot_protocol.h`；串口打开、收到配置和动作成功是三种不同状态。
