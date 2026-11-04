# 启动文件与最小复现

使用本目录的预编译文件即可运行系统。制卡需要一台Linux电脑或Linux虚拟机；不需要安装Vivado、Vitis或重新构建PetaLinux。重新构建时使用完整源码包，操作见 [从源码构建](build_from_source.md)。

本文分为两处操作环境：**第1～7节在Linux电脑／虚拟机执行，第8～10节在开发板串口执行**。两个环境中的磁盘名、账号和sudo口令相互独立。

完整流程：核对文件 → 准备Linux工具 → 识别SD卡 → 分区 → 复制启动文件 → 写入并扩展根文件系统 → 安全取卡 → SD启动 → 登录、校准与功能检查。

## 1. 文件与硬件

### 1.1 文件位置

先解压完整源码包或独立复现包，在Linux终端进入其中的 `reproduce/`，后续制卡命令均从此目录开始。Windows建议先解压到短目录，如 `D:\ac16`，再将目录交给虚拟机访问，以免长路径导致IP文件解压不完整。各代码块依次执行，出现错误先按排障说明处理，再进入下一步。

```text
reproduce/
├── boot/
│   ├── BOOT.BIN
│   ├── image.ub
│   ├── boot.scr
│   └── system.dtb
├── rootfs.ext4.gz
├── sha256sums.txt
├── manifest.json
├── README.md
├── troubleshooting.md
└── build_from_source.md
```

| 文件 | 放置方式 |
| --- | --- |
| `boot/` 内四个文件 | 复制到SD第一分区FAT32的**根目录** |
| `rootfs.ext4.gz` | 解压为1 GiB的 `rootfs.ext4`，再写入SD第二分区 |
| `sha256sums.txt` | 检查上述五个发布文件是否完整 |
| `manifest.json` | 解压后镜像及各应用的版本、大小和SHA256 |

第一分区最终应直接看到 `BOOT.BIN`、`image.ub`、`boot.scr`、`system.dtb`，不应多套一层 `boot/`。`rootfs.ext4` 是第二分区的文件系统镜像，不是复制到FAT32目录的普通文件。本包也不是可直接覆盖整张SD卡的整卡 `.img`。

### 1.2 硬件连接

- 与项目一致的XC7Z020-CLG400-2开发板，启动模式设为SD／TF；拨码或跳线位置按开发板手册设置。
- OV5640摄像头、双环16路ICS-43434阵列（半径25／50 mm），按 [接口说明](../board/interfaces.md)、`src/constraints/` 和 [设计报告](../report/design_report.pdf) 中的接线图连接。
- 屏幕同时连接HDMI、供电及USB触摸数据。触摸数据接开发板的USB Host／配置为Host的OTG接口；电脑侧调试串口是另一条连接。
- UART0调试串口接电脑，参数为 **115200、8数据位、无校验、1停止位、无流控**。
- 连接散热装置并使用已验证稳定的供电方式。机械臂可在基本显示和声学检查完成后接入。

## 2. Linux制卡工具

推荐使用Ubuntu 24.04 LTS制卡。Ubuntu 24.04的e2fsprogs为1.47系列，可直接处理本镜像使用的EXT4特性。安装所需工具：

```bash
sudo apt update
sudo apt install e2fsprogs dosfstools parted util-linux gzip coreutils
```

在同一个Bash终端中选择文件系统工具，并确认输出为 **1.47.0或更新版**：

```bash
E2FSCK='/usr/sbin/e2fsck'
RESIZE2FS='/usr/sbin/resize2fs'
test -x "$E2FSCK" && test -x "$RESIZE2FS" || exit 1
"$E2FSCK" -V
```

Ubuntu 22.04自带的1.46.5会报告 `unsupported feature(s): FEATURE_C12`，这是工具不支持 `orphan_file` 特性。若已安装并构建本项目的PetaLinux工程，可改用其Linux主机工具；将下面路径替换成自己的工程路径：

```bash
E2TOOLS='/absolute/path/to/petalinux_project/build/tmp/sysroots-components/x86_64/e2fsprogs-native/sbin'
E2FSCK="$E2TOOLS/e2fsck"
RESIZE2FS="$E2TOOLS/resize2fs"
test -x "$E2FSCK" && test -x "$RESIZE2FS" || exit 1
"$E2FSCK" -V
```

两种方式选一种即可，后续使用选定的 `$E2FSCK` 和 `$RESIZE2FS`。无需为了制卡在新环境中安装完整PetaLinux；使用满足版本要求的Linux制卡环境即可。上述native工具运行在Linux电脑，不要复制到ARM开发板运行。

## 3. 校验与解压

确认当前目录是 `reproduce/`，本机磁盘至少留出2 GiB空间：

```bash
pwd
ls -lh boot rootfs.ext4.gz sha256sums.txt
df -h .
sha256sum -c sha256sums.txt
gzip -t rootfs.ext4.gz
gzip -dk rootfs.ext4.gz
stat -c '%n %s bytes' rootfs.ext4
printf '%s  rootfs.ext4\n' '92c54e675721344f526f04742b4c32b0222cb4d486860c9fdf7a5340dc588b8f' | sha256sum -c -
```

五个发布文件应全部显示 `OK`，解压后的 `rootfs.ext4` 应为 **1073741824字节**，最后一次校验也应显示 `OK`。`gzip -dk` 保留压缩源文件；如果提示目标已存在，先校验现有 `rootfs.ext4`，通过即可直接使用。

## 4. 识别SD卡与定义变量

建议用另一张SD卡复现，容量至少2 GB。先备份卡内资料。新建分区和写入镜像会覆盖目标卡上的内容。

使用虚拟机时，在虚拟机的可移动设备菜单中把**读卡器连接到Linux虚拟机**。Windows中的 `H:` 不是Linux磁盘名；读卡器交给虚拟机后，Windows盘符消失是正常现象。可在插入／连接前后各运行一次：

```bash
lsblk -dp -o NAME,SIZE,MODEL,TRAN
lsblk -o NAME,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINTS
```

用容量、型号和新增设备共同识别SD卡。不要根据字母顺序猜测；电脑系统盘及挂载 `/`、`/boot`、`/home` 的磁盘不是制卡目标。

| 位置 | 整卡示例 | 第一分区示例 | 第二分区示例 |
| --- | --- | --- | --- |
| Linux电脑上的USB读卡器 | `/dev/sdb` | `/dev/sdb1` | `/dev/sdb2` |
| Linux电脑上的内置SD读卡器 | `/dev/mmcblk0` | `/dev/mmcblk0p1` | `/dev/mmcblk0p2` |
| 开发板启动后 | `/dev/mmcblk0` | `/dev/mmcblk0p1` | `/dev/mmcblk0p2` |

下例中的 `/dev/sdX` 必须替换为刚确认的**整卡**设备。后续命令在同一个Bash终端执行，保留这些变量：

```bash
SD_DISK='/dev/sdX'
SD_DISK=$(readlink -f "$SD_DISK")
test -b "$SD_DISK" || { echo '请先填写正确的SD整卡设备'; exit 1; }
case "$SD_DISK" in
  *[0-9]) BOOT_PART="${SD_DISK}p1"; ROOT_PART="${SD_DISK}p2" ;;
  *) BOOT_PART="${SD_DISK}1"; ROOT_PART="${SD_DISK}2" ;;
esac
printf '整卡=%s\n启动分区=%s\n根分区=%s\n' "$SD_DISK" "$BOOT_PART" "$ROOT_PART"
lsblk -o NAME,SIZE,MODEL,TYPE,FSTYPE,MOUNTPOINTS "$SD_DISK"
```

先关闭文件管理器中正在浏览SD卡的窗口，并离开SD挂载目录。逐一卸载目标卡已挂载的分区：

```bash
while read -r part kind; do
  [ "$kind" = 'part' ] || continue
  if findmnt -rn -S "$part" >/dev/null; then
    sudo umount "$part" || exit 1
  fi
done < <(lsblk -lnpo NAME,TYPE "$SD_DISK")
lsblk -o NAME,SIZE,FSTYPE,MOUNTPOINTS "$SD_DISK"
```

目标卡分区的 `MOUNTPOINTS` 应为空。若出现 `target is busy`，关闭占用窗口或终端后重新卸载，先不要分区或写入。

## 5. 新卡分区与启动文件复制

### 5.1 建立MBR双分区

| 分区 | 布局 | 类型与作用 |
| --- | --- | --- |
| 第一分区 | 1 MiB起，大小128 MiB | FAT32，标签BOOT，设置boot标志 |
| 第二分区 | 129 MiB起，到卡末尾 | Linux分区，至少1 GiB，承载EXT4根文件系统 |

如果复用的卡已满足该布局要求，可跳过下面的分区和格式化命令，进入5.2节；第6节仍会覆盖其根文件系统。采用图形分区工具时，分区表选择 **MBR／msdos**，并使用两个主分区。

新卡执行以下命令前，再次核对打印的设备。确认时输入完整设备路径：

```bash
read -r -p "将重建 $SD_DISK，输入该完整设备路径继续：" CONFIRM_DISK
[ "$CONFIRM_DISK" = "$SD_DISK" ] || { echo '设备确认不一致，停止'; exit 1; }
sudo parted --script "$SD_DISK" mklabel msdos
sudo parted --script --align optimal "$SD_DISK" mkpart primary fat32 1MiB 129MiB
sudo parted --script "$SD_DISK" set 1 boot on
sudo parted --script --align optimal "$SD_DISK" mkpart primary ext4 129MiB 100%
sudo partprobe "$SD_DISK"
sudo udevadm settle
test -b "$BOOT_PART" && test -b "$ROOT_PART" || exit 1
sudo mkfs.vfat -F 32 -n BOOT "$BOOT_PART"
sudo parted --script "$SD_DISK" unit MiB print
```

`mkpart ... ext4` 只建立第二分区；其文件系统由后续 `dd` 写入，不需要先运行 `mkfs.ext4`。分区后若没有出现两个设备节点，先在 [排障表](troubleshooting.md) 检查，不要猜测设备名继续写。

### 5.2 复制四个启动文件

分区后桌面可能自动重新挂载卡，先再次执行第4节的卸载步骤。下面使用临时挂载目录复制，并逐个比对内容：

```bash
BOOT_MNT=$(mktemp -d /tmp/ac16-boot.XXXXXX)
sudo mount "$BOOT_PART" "$BOOT_MNT"
sudo cp -v boot/BOOT.BIN boot/image.ub boot/boot.scr boot/system.dtb "$BOOT_MNT/"
for file in BOOT.BIN image.ub boot.scr system.dtb; do
  sudo cmp "boot/$file" "$BOOT_MNT/$file" || exit 1
done
sudo ls -lh "$BOOT_MNT"
sync
sudo umount "$BOOT_MNT"
rmdir "$BOOT_MNT"
```

四次 `cmp` 没有输出即表示一致。复用旧FAT32分区时，应先把旧 `uEnv.txt`、额外启动脚本等备份到电脑并移出SD启动根目录，避免旧的 `bootargs` 覆盖本包设置；正常复现只需要表中四个启动文件。

## 6. 写入、检查并扩展根文件系统

### 6.1 写入第二分区

再次确认第二分区未挂载、容量足够且属于刚选定的整卡：

```bash
if findmnt -rn -S "$ROOT_PART" >/dev/null; then
  echo '第二分区仍被挂载，请先卸载'; exit 1
fi
test -f rootfs.ext4 || exit 1
test "$(lsblk -nro PKNAME "$ROOT_PART")" = "$(basename "$SD_DISK")" || exit 1
IMAGE_BYTES=$(stat -c %s rootfs.ext4)
PART_BYTES=$(sudo blockdev --getsize64 "$ROOT_PART")
[ "$PART_BYTES" -ge "$IMAGE_BYTES" ] || { echo '第二分区小于镜像'; exit 1; }
sudo dd if=rootfs.ext4 of="$ROOT_PART" bs=4M iflag=fullblock status=progress conv=fsync
sync
```

`if=` 是本机的镜像文件，`of=` 必须是SD**第二分区**。完成时写入量应为1073741824字节。等待 `dd` 和 `sync` 返回提示符，再执行检查；不要在这一阶段拔卡。

### 6.2 文件系统检查和扩容

使用第2节选好的工具。`-p` 只自动修复工具判定可安全处理的问题，也便于通过SSH执行。返回0表示正常，返回1表示已修复；其他返回码先处理，不继续扩容：

```bash
FSCK_RC=0
sudo "$E2FSCK" -f -p "$ROOT_PART" || FSCK_RC=$?
case "$FSCK_RC" in
  0|1) ;;
  *) echo "文件系统检查返回 $FSCK_RC，停止扩容"; exit 1 ;;
esac
sudo "$RESIZE2FS" "$ROOT_PART"
FSCK_RC=0
sudo "$E2FSCK" -f -p "$ROOT_PART" || FSCK_RC=$?
case "$FSCK_RC" in
  0|1) ;;
  *) echo "扩容后检查返回 $FSCK_RC，请先排查"; exit 1 ;;
esac
sync
```

不填写目标大小时，`resize2fs` 使用第二分区的可用容量。显示 `Nothing to do` 通常表示已扩展。`lsblk` 只显示分区容量，下一节的 `df -h` 才能确认文件系统容量。

## 7. 写后检查与安全取卡

把根分区只读挂载，检查容量及关键程序：

```bash
ROOT_MNT=$(mktemp -d /tmp/ac16-root.XXXXXX)
sudo mount -o ro,noload "$ROOT_PART" "$ROOT_MNT"
df -h "$ROOT_MNT"
for file in video_pipeline_init video_status_panel pl_control_daemon \
            acoustic_display_control acoustic_frequency acoustic_frequency_feed camera_snapshot \
            camera_album robot_control_daemon video_ui_watchdog; do
  sudo test -x "$ROOT_MNT/usr/bin/$file" || { echo "缺少程序：$file"; exit 1; }
done
sudo ls -l "$ROOT_MNT/etc/rc5.d/S95video-pipeline-init" \
           "$ROOT_MNT/etc/rc5.d/S99video-status-panel"
sudo umount "$ROOT_MNT"
rmdir "$ROOT_MNT"
sync
lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS "$SD_DISK"
```

两个分区的挂载点均应为空。虚拟机中安全断开读卡器，再从电脑移除SD卡。Windows只显示FAT32分区、看不到EXT4目录是正常现象；Windows若提示格式化另一个分区，选择取消。

## 8. 开发板上电与首次登录

### 8.1 启动顺序

1. 开发板完全断电，插入制好的SD卡，并设置SD启动模式。
2. 检查摄像头、阵列、HDMI和USB触摸接线，接入调试串口并打开115200／8N1、无流控的串口终端。
3. 上电，观察FSBL／U-Boot／Linux输出，等待 `acoustic_linux login:`。若终端打开得晚，按一次回车查看提示符。
4. 显示界面和服务随系统启动，无需额外JTAG下载，也无需手动再次启动相机初始化程序。

完整断电需考虑屏幕触摸USB等供电路径；只关闭一个供电开关而板卡仍亮灯时，USB PHY不一定经历了复位。

### 8.2 演示账号与口令

| 项目 | 内容 |
| --- | --- |
| 账号 | `root` 或 `petalinux` |
| 初始口令 | `ac16-demo` |
| 第一次登录 | 根据提示设置新口令 |

输入口令时**不显示字符或星号**，输入后回车即可。若出现 `Current password`，先输入初始口令；接着输入新口令并重复确认。完成后使用新口令登录。口令修改保存在EXT4中，正常重启后继续生效。

后面的板端命令使用root。以 `petalinux` 登录时先执行 `sudo -i`，输入该账号的新口令。Linux电脑上制卡时的sudo口令则是电脑／虚拟机账号的口令。

### 8.3 确认系统来自SD第二分区

在**板卡串口**执行：

```bash
cat /proc/cmdline
findmnt /
df -h /
cat /run/video-pipeline.state
/etc/init.d/video-status-panel status
/usr/bin/video_status_panel --version
/usr/bin/robot_control_daemon --version
```

检查重点：

- 命令行包含 `root=/dev/mmcblk0p2 rw rootwait` 和 `uio_pdrv_genirq.of_id=generic-uio`，不含维护模式的 `init=/bin/sh`。
- `/` 的类型为EXT4，来自SD第二分区，挂载选项包含 `rw`。`df` 的设备名也可能显示 `/dev/root`，结合 `findmnt` 与内核参数判断。
- 根文件系统容量与第二分区相近，而非始终只有1 GiB。
- 视频状态含 `video: running rc=0`，UI服务处于运行状态。
- UI版本包含 `video-status-arm-live-v30`，机械臂服务版本包含 `robot-control-v7`。

## 9. 静音校准与功能检查

### 9.1 首次静音校准

摄像头和界面正常后，先在底栏把声学识别置为ON。停止测试声源，保持阵列、手机和人员位置稳定，再以root执行：

```bash
/etc/init.d/acoustic-display-control stop
/usr/bin/acoustic_display_control --calibrate
/etc/init.d/acoustic-display-control start
cat /etc/acoustic-display.conf
cat /run/acoustic-display.state
```

校准约5秒，成功时输出 `CALIBRATED`，并写入本机背景门限。`Calibration failed` 时先排查麦克风输入和声学状态，不要用播放测试音的场景作静音基线。更换场地或安装方式后可按相同步骤重新校准。

### 9.2 按顺序验收

| 项目 | 操作与预期 |
| --- | --- |
| 摄像头 | 移动物体，图像连续更新 |
| 触摸 | 打开／关闭侧栏，操作底栏按钮和抽屉 |
| 声学 | 阵列前约30 cm播放固定2 kHz单音，热点、坐标和频率更新；移动后跟随，停声后消退 |
| 图像模式 | 切换原图、灰度、二值化、亮度偏移、反相和移位增益，测试参数滑块与ROI |
| 抓拍相册 | 单次抓拍，等待保存提示，再从相册打开原图与测量详情；公开镜像初始相册为空 |
| 机械臂 | 核对UART1的9600／8N1、电平、区域映射和控制器协议后接入；基础显示不依赖机械臂连接 |

UART0的115200用于电脑调试，UART1的9600用于机械臂，不要混用。机械臂自动功能默认关闭，实际动作通过执行页选择并开启。

## 10. 关机与后续维护

通过设置页关机，或在板端root串口执行 `poweroff`。等待系统完成关机、SD读写停止后再断电或取卡。`sync` 只能同步写入，不能代替关机流程。

重新写入 `rootfs.ext4` 会替换该分区中的账号、校准、照片和配置；保留运行数据时先另行备份。正常使用与仅更新用户程序不需要反复制卡。

遇到工具版本、分区、登录、黑屏、触摸或抓拍问题时，按 [关键卡点与排障](troubleshooting.md) 从对应阶段检查。测试项目与结果见 [测试与验证](../report/verification.md)，组件来源与许可见 [许可范围](../license_scope.md)。

## 工具参考

- [Ubuntu 24.04 e2fsprogs软件包](https://packages.ubuntu.com/noble/e2fsprogs)
- [e2fsck检查与返回码](https://manpages.ubuntu.com/manpages/noble/en/man8/e2fsck.8.html)
- [resize2fs扩容](https://manpages.ubuntu.com/manpages/noble/en/man8/resize2fs.8.html)
- [parted分区参数](https://manpages.ubuntu.com/manpages/noble/en/man8/parted.8.html)
