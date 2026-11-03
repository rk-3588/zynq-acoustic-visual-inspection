# 启动文件与最小复现

本目录用于在相同硬件上直接写卡启动，无需重新运行Vivado、Vitis或PetaLinux。源码构建流程另见 [build_from_source.md](build_from_source.md)。

## 文件

| 文件 | 使用方式 |
| --- | --- |
| `boot/BOOT.BIN` | 复制至SD第一分区FAT32根目录 |
| `boot/image.ub` | 同上，内含内核和设备树 |
| `boot/boot.scr` | 同上，U-Boot启动脚本 |
| `boot/system.dtb` | 同上，配套外部设备树 |
| `rootfs.ext4.gz` | 解压为1 GiB的 `rootfs.ext4`，写入SD第二分区 |
| `sha256sums.txt` | 五个发布文件的SHA256校验值 |
| `manifest.json` | 镜像版本、应用哈希与验证范围 |

四个启动文件与原样机卡逐字节一致。公开根文件系统由原卡副本清理后重新生成：保留最终应用和硬件配置，去除原登录口令、SSH密钥、历史抓拍、任务状态、日志和调试备份。原卡与原始备份未修改。首次启动时SSH服务按原启动脚本重新生成主机密钥。

## 硬件与准备

使用XC7Z020-CLG400-2开发板、双环16路ICS-43434（25／50 mm）、OV5640、HDMI触摸屏。接线按 [接口说明](../board/interfaces.md) 与 `src/constraints/` 核对。触摸USB接开发板USB Host，选择SD启动；UART0调试口为115200、8N1。

使用另一张SD卡进行复现并备份其中资料。新卡采用MBR分区表：第一分区FAT32（建议128 MiB以上），第二分区为Linux分区（至少1 GiB，建议使用剩余容量）。创建分区会清除目标卡已有内容。

镜像含EXT4 `orphan_file` 特性，文件系统检查和扩容需要 **e2fsprogs 1.47.0或更新版**。本项目Ubuntu 22.04虚拟机自带1.46.5不支持；可以使用已构建PetaLinux工程内的 `build/tmp/sysroots-components/x86_64/e2fsprogs-native/sbin/` 工具。

## 写卡

进入本目录，校验并解压：

```sh
sha256sum -c sha256sums.txt
gzip -dk rootfs.ext4.gz
```

将 `boot/` 中四个文件复制到第一分区**根目录**。在Linux主机或虚拟机卸载第二分区并核对目标后操作，以下占位符必须替换为实际路径：

```sh
lsblk -o NAME,MODEL,SIZE,FSTYPE,UUID,MOUNTPOINTS
ROOT_PART='/dev/disk/by-id/<SD设备标识>-part2'
E2TOOLS='/<PetaLinux工程绝对路径>/build/tmp/sysroots-components/x86_64/e2fsprogs-native/sbin'
test -b "$ROOT_PART" || exit 1
test -x "$E2TOOLS/e2fsck" || exit 1
"$E2TOOLS/e2fsck" -V
sudo dd if=rootfs.ext4 of="$ROOT_PART" bs=4M status=progress conv=fsync
sync
sudo "$E2TOOLS/e2fsck" -f "$ROOT_PART"
# 确认检查通过后才继续扩容；第二分区必须保持卸载。
sudo "$E2TOOLS/resize2fs" "$ROOT_PART"
sync
```

`ROOT_PART` 必须是SD第二分区，不能是整卡、启动分区或电脑系统分区。两个分区安全卸载后再移回板卡。本目录提供分区文件组合，`rootfs.ext4` 不能作为整卡镜像使用。

## 启动与验收

1. 连接HDMI、触摸、阵列、摄像头和调试串口，SD启动后视频、声学与UI服务自动运行。机械臂自动功能默认关闭，先进行基本显示测试。
2. 演示账号为 `root` 或 `petalinux`，初始口令均为 `ac16-demo`，首次登录要求更改。这是公开演示口令，不用于实际部署环境。
3. 执行 `findmnt /`，应为SD第二分区的可写EXT4；查看 `cat /run/video-pipeline.state`，确认摄像头连续更新及触摸正常。
4. 停止测试声源，在安静环境以root权限重新校准约5秒：

```sh
/etc/init.d/acoustic-display-control stop
/usr/bin/acoustic_display_control --calibrate
/etc/init.d/acoustic-display-control start
```

5. 阵列前约30 cm播放固定2 kHz测试音，检查热点、坐标、频率和左右跟随；停声后有效位置应消退。
6. 抓拍一次，从相册打开原图和测量详情。公开镜像相册初始为空。
7. 机械臂另行核对UART1的9600／8N1、电平和区域映射，完成现场任务验证。最后通过设置页正常关机再断电。

## 验证范围

已完成启动文件校验、根文件系统离线检查、清理前后10个最终应用哈希比对，以及新文件系统的文件内容和权限比对。应用包含UI v30、机械臂v7、抓拍v3、相册v2、UI看门狗v2与摄像头半频／水平翻转初始化。

启动后按上文进行摄像头、触摸、声学与抓拍功能检查。测试项目与结果见 [测试与验证](../report/verification.md)，系统组件的来源与许可见 [许可范围](../license_scope.md)。
