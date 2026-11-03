# 上板与当前发布组合

硬件：XC7Z020-CLG400-2、OV5640、16路ICS-43434双环（25/50 mm）、HDMI触摸屏。机械臂使用独立控制器。本工程不是PYNQ Overlay，使用PetaLinux＋UIO，不能套用PYNQ加载命令。

取得配套预编译系统文件后，可按 [SD最小启动流程](sd_quickstart.md) 直接写卡运行。完整运行镜像待原样机SD卡离线导出、核验后另行提供；当前源码仓库不包含完整SD恢复镜像。下面保留完整启动配置和构建说明。

## 启动与根文件系统

SD第一分区为FAT，放BOOT.BIN、image.ub、boot.scr、system.dtb；第二分区为可写EXT4根文件系统，内核命令行使用：

```text
console=ttyPS0,115200 earlycon root=/dev/mmcblk0p2 rw rootwait uio_pdrv_genirq.of_id=generic-uio
```

U-Boot固化 `scriptaddr=0x08000000`。FSBL在交接前执行MIO8 USB PHY复位（低20ms、释放后等待700ms）。这些配置已收录在 `src/petalinux/recipes-bsp/`。不要在运行中用USB PHY复位、重绑设备替代正确启动流程。

BOOT包含FSBL、PL、U-Boot和内嵌DTB四个载荷。`boot.bif` 中DTB加载到0x00100000；正式打包时提供与当前硬件相匹配的FSBL/U-Boot/DTB和bit。不要省略第四个载荷。

```sh
petalinux-build
bootgen -arch zynq -image board/boot.bif -o work/boot.bin -w on
```

BIF从 `release_assets/` 读取载荷。它们未混入Git源码提交；冻结版本哈希在 `release_manifest.json`。本地另行保存的二进制包用于同一板型恢复，不应在没有核对引脚/电源/设备树时用于其他板卡。

第二分区只在**首次部署**或明确决定替换完整根文件系统时写入rootfs.ext4；须先正常关机、离线确认目标分区与备份。不要用猜测的 `/dev/sdX` 命令直接写盘。普通用户态更新不需要重写SD分区。

## 初次验证顺序

1. 读取 `/proc/cmdline`、`findmnt /`，确认EXT4挂载可写。
2. 用 `get_status.sh` 核对UIO身份、视频状态与温度；该脚本只读，不复位视频或发送串口数据。
3. 确认摄像头连续更新及USB触摸；UI通常只启动一次。看门狗满足特定条件才重开，不能覆盖所有黑屏模式。
4. 安静环境下按应用的校准流程生成本机噪声门限；`noise_peak=0` 配置示例不是已校准的板端值。
5. 播放固定2 kHz或3 kHz声源，先在约30 cm处验证左右移动、静音门控、实时坐标和稳定坐标；该步骤是功能检查，不等同物理定位精度验收。
6. 抓拍一次、查看相册，再验证停止播放后的状态清空。相册恢复发生时会返回初始UI页面。
7. 机械臂接入前核对协议、电平、UART1引脚和映射。Linux侧目前使用9600、8N1，见 `video_robot_service.c`；`robot-regions.conf`只配置区域映射与超时。旧对话中的115200不代表最终配置，控制器固件须与当前服务一致。

UIO、DDR和用户态接口见 [interfaces.md](interfaces.md)。本地 `release_assets/` 保存已核对的BOOT、bit/DCP/LTX及关键ARM程序；文件清单见 `build/frozen_assets_sha256.json`。它不是完整SD镜像，也不进入源码ZIP。

## 已知边界

摄像头为30 cm接线下验证的降频变体，水平翻转在传感器端进行；热图和定位横坐标在PL同步翻转。原始抓拍因此包含摄像头最终方向，不包含UI或热图。JPEG等第三方解码库不是本版相册依赖。

顶栏XADC读数是芯片内部温度；90/95°C为界面提示阈值，不表示建议在该温度持续测试。供电和触摸线可能造成反向供电，完全掉电必须考虑所有能供电的USB路径。

测量JSON是同次软件请求的记录，不保证相机、声学定位和FFT严格硬件同帧。原始计数未标定为dB SPL。
