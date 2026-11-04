# 关键卡点与排障

按问题发生的阶段检查。**A、B节在Linux电脑／虚拟机执行；C、D节在板卡串口执行。** 制卡变量和分区约定见 [最小复现](README.md)。

## A. 文件与工具

| 现象 | 检查与处理 |
| --- | --- |
| `rootfs.ext4.gz: No such file`、校验清单找不到 | 用 `pwd`、`ls` 确认进入解压后源码的 `reproduce/`，此处应同时有 `boot/`、镜像和清单 |
| Windows解压提示路径过长或缺少 `.xci` | 重新解压到短目录，如 `D:\ac16`；Linux下可放 `~/ac16`。使用支持RAR5的解压工具 |
| `sha256sum` 显示FAILED或压缩包CRC错误 | 重新取得完整发布包；先让校验通过，再写卡。不要混用不同包的启动文件和根文件系统 |
| `gzip` 提示 `rootfs.ext4 already exists` | 先按主说明校验已有解压文件；一致即可继续 |
| 解压报 `No space left on device` | 用 `df -h .` 检查镜像所在的电脑磁盘；这是本机空间，不是SD剩余容量。解压需要容纳1 GiB文件 |
| `FEATURE_C12`、`orphan_file`、`Get a newer version of e2fsck` | 用 `e2fsck -V` 检查版本，选择1.47.0或更新版，再从检查步骤继续。旧工具可能同时打印“filesystem still has errors”，不等于镜像已损坏 |
| `e2fsck: need terminal for interactive repairs` | SSH或脚本没有交互终端。按主说明使用 `-f -p` 并检查返回码；不能安全修复时停止，不直接改成自动同意所有修复的 `-y` |
| native工具存在却报 `No such file`、缺少共享库或 `Exec format error` | 使用当前电脑架构的工具。PetaLinux的 `x86_64/e2fsprogs-native` 是Linux x86-64主机工具；板端程序是ARM程序，不可在电脑上直接运行。可使用满足版本要求的Ubuntu系统工具 |
| 不知道sudo该输入什么口令 | 制卡阶段输入Linux电脑／虚拟机账号口令；板卡登录阶段才使用演示账号及其更改后的口令 |

## B. SD识别、写入与扩容

### B.1 虚拟机看不到卡

在虚拟机的可移动设备菜单中把读卡器连接到虚拟机，然后执行：

```bash
lsblk -dp -o NAME,SIZE,MODEL,TRAN
lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINTS
sudo dmesg | tail -n 30
```

同一个USB读卡器由Windows或虚拟机其中一方接管；Windows盘符不能填入Linux的 `dd of=`。若内置读卡器无法交给虚拟机，可使用USB读卡器，按容量与型号重新识别。

### B.2 分区或写入失败

| 现象 | 处理 |
| --- | --- |
| `umount: target is busy` | 关闭文件管理器中的SD窗口；终端执行 `cd ~` 离开挂载目录，再卸载 |
| 重分区后没有两个分区设备 | 执行 `sudo partprobe "$SD_DISK"` 和 `sudo udevadm settle` 后再看 `lsblk`。仍不出现时，确认写入已结束，安全断开并重新接入读卡器，重新定义变量 |
| `dd: No such file` | 检查镜像是否已解压；分清 `if=` 的本机文件路径与 `of=` 的SD第二分区路径 |
| `dd: Permission denied` | 块设备写入需要sudo；同时检查读卡器和卡的写保护开关 |
| dd阶段 `No space left on device` | 第二分区须至少1073741824字节，核对 `blockdev --getsize64`，不能只看整卡容量 |
| `Input/output error`、USB反复断开重连 | 停止本次写入，检查读卡器、线缆、接口和卡；恢复稳定后重新写入并校验 |
| `resize2fs: Nothing to do` | 文件系统可能已达到分区容量，挂载后用 `df -h` 确认 |
| 分区十几GB，启动后根目录只有1 GiB | 分区够大但文件系统未扩展。正常关机后离线卸载第二分区，使用匹配版本的工具检查、扩容，再用 `df` 确认 |
| Windows提示格式化“另一个分区” | 取消。Windows通常不能直接浏览EXT4；制卡完成后不应再次格式化第二分区 |

`e2fsck` 返回0和1可继续；返回2表示工具要求重启，返回4、8等表示仍有错误或操作问题，应先处理。检查和扩容时根分区保持卸载。

## C. 上电、登录与视频

### C.1 串口无输出

1. 确认打开的是开发板UART0调试口对应的电脑串口，而非触摸接口。
2. 设置115200／8N1、无流控，关闭占用同一串口的其他终端。
3. 核对SD启动模式、卡已插稳、第一分区为FAT32且四个文件直接位于根目录。
4. 重新上电观察U-Boot和Linux输出。终端打开较晚时按回车查看提示。首次启动会生成新的SSH主机密钥，初始化期间等待串口继续输出。

### C.2 停在U-Boot提示符

以下命令在 **U-Boot的 `Zynq>` 等提示符**执行，不是在Linux终端执行：

```text
mmc list
mmc dev 0
mmc rescan
fatls mmc 0:1
printenv scriptaddr bootargs
```

应能列出四个启动文件，`scriptaddr` 应为 `0x08000000`。MMC编号以 `mmc list` 的识别结果为准。

`bootargs` 未设置时可由 `image.ub` 内嵌设备树提供；以Linux最终显示的参数为准。若这里显示旧的 `root=/dev/ram0` 或维护模式参数，可按下面步骤临时覆盖。

文件正常时，可临时直接加载本包FIT镜像，以分辨启动脚本与内核阶段：

```text
setenv bootargs console=ttyPS0,115200 earlycon root=/dev/mmcblk0p2 rw rootwait uio_pdrv_genirq.of_id=generic-uio
fatload mmc 0:1 0x10000000 image.ub
bootm 0x10000000
```

这组命令只设置本次启动参数，不执行 `saveenv`。若无法识别卡或FAT分区，先处理分区与启动文件。

### C.3 内核启动后卡住或根分区不正确

| 现象 | 检查 |
| --- | --- |
| `Waiting for root device /dev/mmcblk0p2`、无法挂载根目录 | 核对第二分区、镜像写入位置、板端实际MMC编号及启动文件 |
| 根目录为 `/dev/ram0` 或initramfs | 核对四个FAT文件、`uEnv.txt` 及U-Boot的 `bootargs`，排查另一套启动参数 |
| 直接进入维护shell，没有正常服务 | 检查是否保留 `init=/bin/sh`、只读维护参数或旧 `uEnv.txt`；恢复主说明中的SD根参数后正常启动 |
| 画面或程序仍为旧版本 | 四个启动文件更新不等于EXT4程序更新。检查第二分区写入的镜像和板上 `--version` 输出 |

正常登录后先读取：

```bash
cat /proc/cmdline
findmnt /
df -h /
```

### C.4 第一次登录或改口令卡住

- 账号和初始口令见主说明。口令输入不回显是正常行为，不要因此重复键入。
- 首次登录按提示输入旧口令，再设置新口令并确认；要求重登时，用新口令登录。
- 核对大小写和键盘布局；公开复现镜像使用独立演示口令，原卡口令并未修改。
- 若出现 `Authentication token manipulation error`，先看 `findmnt /` 是否可写、是否处于维护参数。内核同时报告EXT4错误时，到制卡电脑离线检查分区。

### C.5 HDMI无画面、黑屏或条纹

先区分“无信号”和“有信号但黑屏”，再以root读取状态：

```bash
cat /run/video-pipeline.state
tail -n 60 /var/log/video-pipeline.log
/etc/init.d/video-status-panel status
cat /run/video-status-panel-watchdog.state
tail -n 50 /run/video-status-panel.log
```

- **无信号**：核对显示器输入源、HDMI线、是否已进入Linux，以及视频状态是否到达 `video: running rc=0`。
- **视频初始化提前失败**：根据日志定位摄像头SCCB、UIO或VDMA阶段，先修复该阶段。
- **视频已运行，UI开启时整屏黑／竖纹**：保存以上状态后，可正常重启一次UI服务：

```bash
/etc/init.d/video-status-panel restart
```

服务脚本会正常退出与重新启用UI图层。若报告停止失败或锁忙，先看日志，不用强制杀进程或连续重启视频。自动恢复的触发条件见 [显示看门狗](../board/evidence/ui_watchdog.md)。

### C.6 触摸失效

HDMI传视频，USB传触摸；显示正常并不表示触摸USB已连通。以root执行：

```bash
cat /proc/bus/input/devices
/usr/bin/video_status_panel --touch-check
dmesg | tail -n 60
```

`--touch-check` 应显示设备、坐标范围和方向参数。没有设备时，检查数据线是否插在开发板Host接口、是否为可传数据的USB线，以及屏幕和板卡供电。出现USB描述符或PHY超时时，正常关机后确认所有可能供电的USB路径均断开，再完整上电。

能识别设备但坐标方向不对时，核对 `/etc/video-status-panel-touch.conf` 的 `swap_xy`、`invert_x`、`invert_y`，保存后正常重启UI。主说明对应默认值均为0。

### C.7 摄像头绿点、错色或采集停帧

保持本包BOOT与半频摄像头初始化组合，检查排线长度、接触和PCLK接地配对。按视频日志判断输入链路，不用删除错误检查或在线反复切换PCLK极性处理。

## D. 声学、抓拍与机械臂

### D.1 没有热点或主峰频率

1. 将底栏声学识别置为ON；OFF时热点和有效位置被逻辑门控关闭。
2. 用固定2 kHz单音、约30 cm距离开始，保持位置与音量稳定。
3. 在安静环境校准，并读取：

```bash
cat /run/acoustic-display.state
cat /run/acoustic-frequency.state
tail -n 40 /run/acoustic-display.log
tail -n 40 /run/acoustic-frequency-feed.log
```

4. 校准提示有效样本不足时，检查阵列供电、共地、WS／SCK、LR接地及16路DATA与XDC映射。`quiet` 表示静音门控，数据源失效按状态字段另行处理。

用以下只读命令检查PL接口：

```bash
for uio in /sys/class/uio/uio*; do
  [ -d "$uio" ] || continue
  printf '%s ' "$uio"
  cat "$uio/name" "$uio/maps/map0/addr"
done
/usr/bin/pl_control_daemon --check-uio
```

程序按名称和物理地址识别UIO，设备号不固定。声学控制基址为 `0x44a20000`，FFT快照基址为 `0x40030000`，其他接口见 [接口表](../board/interfaces.md)。

### D.2 抓拍失败或相册为空

先确认摄像头连续更新，再检查可写空间和抓拍预检：

```bash
findmnt /
df -h /
/usr/bin/camera_snapshot --check
tail -n 60 /run/video-status-panel.log
```

预检只检查可否抓拍，不执行抓拍。公开镜像初始相册为空，成功抓拍后才增加记录；数据目录为 `/var/lib/acoustic-camera/captures/`。根目录只读、空间不足或预检报告VDMA异常时，先处理原因，不连续点击抓拍。文件系统仅1 GiB时，按主说明离线扩容。

### D.3 机械臂显示未连接或回包超时

基础显示、声学和抓拍可以独立运行。接入机械臂时核对UART1的9600／8N1、TX／RX交叉、共地、电平和控制器协议；UART0的115200是调试口设置。

服务打开串口不等于控制器已响应，界面依赖实际协议回包。检查：

```bash
/etc/init.d/robot-control status
tail -n 60 /run/robot-control.log
```

在执行页选择夹取、跟踪或测试，并按现场工作区确认后操作；不使用调试工具额外发送随机串口字节验证连接。

## E. 提供诊断信息

需要进一步核查时，保存串口启动报错及下列信息，保留问题发生时的原始状态：

```bash
cat /proc/cmdline
findmnt /
df -h /
cat /run/video-pipeline.state
cat /run/video-status-panel.state
cat /run/video-status-panel-watchdog.state
tail -n 80 /var/log/video-pipeline.log
tail -n 80 /run/video-status-panel.log
```

同时说明屏幕是无信号、黑屏、条纹还是某个功能失效，以及供电、USB接法和所用发布包。启动文件与EXT4中的程序分别确认，应一并提供主说明中的版本输出。
