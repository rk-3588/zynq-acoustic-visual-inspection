# 预编译镜像 SD 最小启动流程

本流程用于在与项目一致的Zynq-7020开发板、16路阵列、OV5640及HDMI触摸屏上运行系统。只需配套预编译文件，无需安装Vivado、Vitis或PetaLinux，也不需要重新编译源码。

**镜像交付状态（2026-11-03）：**源码仓库不包含完整SD运行镜像。本次仅更新说明和Word报告，最终运行镜像待原样机SD卡离线导出并核验后另行提供。现有旧构建根文件系统中的UI为v8，缺少相册、机械臂服务及UI看门狗，不能作为最终版镜像使用。使用以下流程前，须先取得包含最终版用户程序的配套文件。

## 1 准备文件和硬件

| 文件 | 部署位置 |
| --- | --- |
| `BOOT.BIN` | SD第一分区FAT32根目录 |
| `image.ub` | SD第一分区FAT32根目录 |
| `boot.scr` | SD第一分区FAT32根目录 |
| `system.dtb` | SD第一分区FAT32根目录 |
| `rootfs.ext4` | 作为文件系统镜像写入SD第二分区 |

五个文件应来自配套发布组合。`rootfs.ext4` 须包含最终版UI、声学与视频程序、相册、机械臂服务、看门狗及自启动脚本。仅更新四个启动文件不会自动更新EXT4内的应用。

麦克风与摄像头接线按硬件约束核对，触摸USB接开发板USB Host；机械臂可以在基础显示和声学验证完成后再连接。硬件地址与UART参数见[接口说明](interfaces.md)。

## 2 写入 SD 卡

在Linux主机或虚拟机中识别SD卡并备份资料，准备第一分区FAT32、第二分区用于EXT4。将前四个文件复制到第一分区根目录。卸载第二分区，核对目标后再写入根文件系统。

```sh
lsblk -o NAME,MODEL,SIZE,FSTYPE,UUID,MOUNTPOINTS

# 替换为已经核对且卸载的SD第二分区。
ROOT_PART='/dev/disk/by-id/<SD设备标识>-part2'
test -b "$ROOT_PART" || exit 1
test -f rootfs.ext4 || exit 1
sudo dd if=rootfs.ext4 of="$ROOT_PART" bs=4M status=progress conv=fsync
sync
sudo e2fsck -f "$ROOT_PART"
sudo resize2fs "$ROOT_PART"
sync
```

写入会覆盖目标分区。`ROOT_PART` 不能指向整张卡、FAT32启动分区或电脑系统盘；文件系统检查若存在未修复错误，先停止排查，不继续扩容。完成后安全卸载两个分区，再移回板卡。

`rootfs.ext4` 不是复制到FAT分区的普通数据文件，`BOOT.BIN` 也不能作为整卡镜像直接覆盖整张卡。

## 3 选择 SD 启动并上电

连接HDMI、触摸USB和UART0调试串口，选择SD启动模式后上电。串口使用115200 bit/s、8N1。系统自动装载PL并运行Linux、视频、声学和UI服务，机械臂自动功能默认关闭。

登录后检查：

```sh
findmnt /
cat /run/video-pipeline.state
```

根文件系统应来自SD第二分区，为可写EXT4；视频状态应表明正常运行。完整引导参数与故障诊断见[上板说明](README.md)。

## 4 首次静音校准

保持环境安静、停止测试声源，以root权限执行约5秒校准：

```sh
/etc/init.d/acoustic-display-control stop
/usr/bin/acoustic_display_control --calibrate
/etc/init.d/acoustic-display-control start
```

校准失败时先核查阵列与声学状态。配置示例 `noise_peak=0` 不是有效背景基线，不应直接沿用。

## 5 最小验收

1. 摄像头连续更新，触摸抽屉和模式按钮可操作。
2. 静音时无持续背景闪色；阵列前约30 cm播放固定2 kHz声源，热点、坐标和主峰频率可更新。
3. 移动声源后热点跟随，停声后热点和有效位置消退。
4. 抓拍一次并从相册打开，确认摄像头原图和测量记录保存成功。
5. 通过设置页正常关机，待完成后断电。

这些步骤用于基础功能运行验收，不代替统一条件下的物理定位精度测试。单麦频率尚未与热点做空间分离绑定，测量JSON为同次软件请求的记录。

机械臂另行核对UART1的9600/8N1、电平和区域映射，再进行现场任务验证；它不是最小启动的前置条件。

## 后续整卡镜像

待从原样机SD卡正常关机后的离线副本制作整卡镜像，并验证分区、应用版本和冷启动后，可将操作缩为“校验镜像、写入整卡、上电、首次校准与验收”。当前未生成或上传该整卡镜像。
