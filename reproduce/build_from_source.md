# 从源码构建与部署

本指南用于重新生成 FPGA 硬件、PetaLinux 系统及应用。只需运行成品时，使用 [启动文件与最小复现](README.md)，无需执行源码构建。

构建顺序：**Vivado 生成 bit／XSA → 新建 PetaLinux 工程并导入 XSA → 导入项目配置与配方 → 设置本地账号 → 构建系统 → 打包 BOOT.BIN → 制卡与验收**。

## 1. 环境与目录

| 环境 | 用途与要求 |
| --- | --- |
| Windows 或 Linux | Vivado 2026.1，安装 Zynq-7000 器件支持，目标 `xc7z020clg400-2`；具备工程所用 IP 的有效许可 |
| Linux 构建主机 | PetaLinux 2026.1、Python 3.10+；使用普通用户，在 Linux 本地文件系统构建 |
| 主机软件检查 | GCC／Clang、BusyBox；独立编译 ARM 应用另需 PetaLinux SDK |
| 可选 HLS 测试 | Vitis 2026.1 HLS，依赖见 [HLS 源码说明](../src/hls/README.md) |
| SD 卡部署 | Linux／虚拟机及读卡器，工具要求见 [制卡说明第2节](README.md#2-linux制卡工具) |

PetaLinux 主机须符合 AMD 的安装要求，包括受支持的 Linux 版本、系统依赖及 Bash 配置；官方基线为至少 8 GB 内存、8 核 CPU 和 100 GB 可用磁盘空间。首次构建需要下载依赖，不能只按最终镜像大小预留空间。安装和构建均不使用 root，工程不放在 VMware 共享目录、NTFS 或带空格／中文的路径中。

Linux 示例假定源码位于 `$HOME/work/ac16`，PetaLinux 安装在 `$HOME/petalinux/2026.1`。可从仓库获取：

```bash
mkdir -p "$HOME/work"
cd "$HOME/work"
git clone https://github.com/rk-3588/zynq-acoustic-visual-inspection.git ac16
cd ac16
```

已解压源码包时直接进入其根目录，跳过克隆。随后在同一个 Bash 终端定义：

```bash
export REPO_ROOT="$PWD"
export PLNX_INSTALL="$HOME/petalinux/2026.1"
export PLNX_PROJECT="$HOME/work/acoustic_linux_rebuild"
test -f "$REPO_ROOT/build/create_project.tcl"
test -f "$PLNX_INSTALL/settings.sh"
source "$PLNX_INSTALL/settings.sh"
```

`REPO_ROOT` 是源码仓库；`PLNX_PROJECT` 是稍后创建的独立 PetaLinux 工程。关闭终端后需重新定义变量并加载工具环境。各段依次执行，任何一步报错先排查，不跳过错误继续打包。下文“构建主机”指 Linux 电脑／虚拟机，“板端”指开发板串口登录后的系统。

## 2. 生成 FPGA 工程、bit 与 XSA

### 2.1 Windows 构建

将源码放在短路径，例如 `D:\ac16`，在该目录打开 PowerShell。把工具路径改成实际安装位置，`D:/ac16_build` 选用尚未创建工程的新目录：

```powershell
$Vivado = 'D:\AMD\2026.1\Vivado\bin\vivado.bat'
& $Vivado -version
& $Vivado -mode batch -source build/create_project.tcl -tclargs D:/ac16_build
& $Vivado -mode batch -source build/build_hardware.tcl -tclargs D:/ac16_build/ac16_final.xpr
```

第一条构建命令成功后再执行第二条。脚本会拒绝覆盖已有同名 `.xpr`，再次完整重建时选择另一个短目录。Windows 工程输出路径长度限制为 50 个字符，避免 IP 生成文件的长路径问题。

### 2.2 Linux 构建

在已加载 Vivado 2026.1 环境的终端，从源码根目录执行：

```bash
cd "$REPO_ROOT"
vivado -mode batch -source build/create_project.tcl
vivado -mode batch -source build/build_hardware.tcl
```

默认工程为 `work/vivado/ac16_final.xpr`。创建脚本导入 RTL、约束、BD、延迟 ROM 和 `src/vivado/ip_catalog` 中已导出的 IP，生成 BD 输出与顶层封装。**默认硬件构建直接使用已提供的 HLS RTL IP，无需先执行 HLS C 综合。** 修改 `src/hls/` 中的 C 文件不会自动替换这些 IP，需重新导出、更新对应 IP 后再生成硬件。

### 2.3 输出与通过条件

无论 `.xpr` 是否使用自定义目录，实现结果都位于**源码根目录的** `work/implementation/`：

| 输出 | 用途 |
| --- | --- |
| `system_wrapper.bit` | PL 配置，后续打包进 BOOT.BIN |
| `system_wrapper.xsa` | 含 bit 的硬件描述，供 PetaLinux 导入 |
| `system_wrapper.ltx`、`.dcp` | 调试探针与实现检查点 |
| 各类 `.rpt` | 时序、资源、DRC、布线及 bus skew 报告 |

成功标记分别为 `PASS_FINAL_PROJECT_CREATED_AND_BD_VALIDATED` 和 `PASS_FINAL_HARDWARE_BUILD`。完整构建脚本检查 setup／hold、DRC、布线和 bus skew；生成 bit 本身不能代替这些检查。保留项目的引脚、时钟及 CDC 约束，不通过删除约束绕过报错。

若 FPGA 在 Windows 构建、PetaLinux 在虚拟机中构建，须将**本次配套的** XSA 和 bit 复制到 Linux 源码树的 `work/implementation/`，核对大小与 SHA256。`work/` 不受 Git 管理，单独克隆仓库不会带入这些生成文件。

## 3. 新建 PetaLinux 工程并导入硬件

以下命令在构建主机执行。先确认 `PLNX_PROJECT` 不存在；已有工程请换一个新的名字和路径：

```bash
source "$PLNX_INSTALL/settings.sh"
test -s "$REPO_ROOT/work/implementation/system_wrapper.xsa"
test -s "$REPO_ROOT/work/implementation/system_wrapper.bit"
test ! -e "$PLNX_PROJECT"
mkdir -p "$(dirname "$PLNX_PROJECT")"
cd "$(dirname "$PLNX_PROJECT")"
petalinux-create project --template zynq -n "$(basename "$PLNX_PROJECT")"
cd "$PLNX_PROJECT"
petalinux-config --get-hw-description="$REPO_ROOT/work/implementation/system_wrapper.xsa" --silentconfig
```

使用 `zynq` 模板，即 Zynq-7000 的 32 位 ARM 工程。这里向 `--get-hw-description` 传入**具体 XSA 文件**，该形式已用 PetaLinux 2026.1 的新建工程配置流程检查。不能使用 ZynqMP／AArch64 模板，也不能把 bit 当作硬件描述文件。

## 4. 导入配置、设置账号与核对平台

### 4.1 导入项目配方

```bash
python3 "$REPO_ROOT/build/stage_petalinux.py" "$PLNX_PROJECT" --replace
cd "$PLNX_PROJECT"
petalinux-config --silentconfig
```

新建工程已有默认配置，因此首次导入明确使用 `--replace`。脚本导入 98 个文件，包括 6 个应用配方、内核配置、设备树、FSBL／U-Boot 定制及项目配置。不同的旧文件先备份到 `final-release-backup/<时间>/`；不加 `--replace` 时，发现内容不同会拒绝替换。

保留完整层级，特别是 `meta-user/meta-xilinx-tools/`：该处 U-Boot 设备树附加配方由 `meta-user/conf/layer.conf` 的 `BBFILES_DYNAMIC` 加载。应用源文件会复制到各配方的 `files/`，不是直接引用原仓库。

### 4.2 设置本地登录账号

```bash
cd "$PLNX_PROJECT"
petalinux-config -c rootfs
```

进入 `PetaLinux RootFS Settings → Add extra users`，按工具提示设置本地账号，格式为 `用户名:口令`，多个账号用分号分隔。配置保留了 `petalinux` 的用户组／sudo 规则，可继续使用该用户名并设置自己的口令；改名时同步调整用户组和 sudo 配置。需要 root 登录时也在本地配置 root 口令，保存退出。

公开源码的额外用户字段为空，**自行构建的系统不会自动获得预编译镜像的演示口令**。之后若再次完整导入并使用 `--replace`，根文件系统配置也会被替换，需恢复本地账号及额外选项，再生成配置。

### 4.3 关键配置

| 项目 | 本工程配置及作用 |
| --- | --- |
| 根文件系统 | `EXT4 (SD/eMMC)`，根分区 `/dev/mmcblk0p2` |
| 内核命令行 | 见下方完整字符串，保留 `rootwait` 和 UIO 参数 |
| PL 加载 | FSBL 从 BOOT.BIN 加载 bit，不启用 FPGA Manager 二次覆盖 |
| UIO | 内核启用 `CONFIG_UIO`、`CONFIG_UIO_PDRV_GENIRQ`，设备树保留控制节点 |
| 保留 DDR | 起始 `0x01000000`、长度 `0x03000000`，保留 `no-map`，供视频／UI 缓冲使用 |
| USB | USB0 Host／ULPI，以及 FSBL 的 MIO8 PHY 复位定制 |
| 串口 | UART0 为 115200 控制台；UART1 为独立机械臂接口，最终服务为 9600、8N1 |
| U-Boot | `scriptaddr=0x08000000`，保留项目的启动命令及设备树定制 |

```text
console=ttyPS0,115200 earlycon root=/dev/mmcblk0p2 rw rootwait uio_pdrv_genirq.of_id=generic-uio
```

FSBL 附加任务使用 **XSCT 流程**，要求 `XILINX_WITH_ESW="xsct"`，在 `FsblHookBeforeHandoff` 中加入 USB PHY 复位。若报 `Board USB PHY hook requires the validated XSCT FSBL layout`，应核对所选流程及配方，不应删掉检查或把补丁随意移到其他生成文件。复位为拉低 20 ms，释放后等待 700 ms。

核对六个应用包已启用：

```bash
grep -E '^CONFIG_(video-pipeline-init|video-preprocess-control|acoustic-display-control|acoustic-frequency|pl-control-daemon|pl-control-init)=y' \
    "$PLNX_PROJECT/project-spec/configs/rootfs_config"
grep -E 'ROOTFS_EXT4|USER_CMDLINE|FPGA_MANAGER' \
    "$PLNX_PROJECT/project-spec/configs/config"
```

第一条应输出六项 `=y`。应用与配方关系如下：

| BitBake 配方 | 主要内容 |
| --- | --- |
| `video-pipeline-init` | 摄像头／VDMA／VTC／Mixer 初始化、触摸 UI、显示看门狗、抓拍、相册、机械臂服务 |
| `acoustic-display-control` | 静音校准、门控、动态色阶与平滑 |
| `pl-control-daemon` | 真实定位寄存器、UIO 身份识别、位置有效性及稳定判定 |
| `pl-control-init` | 定位服务启动脚本 |
| `acoustic-frequency` | 单麦快照读取及 FFT 频率服务 |
| `video-preprocess-control` | 图像模式、参数及 ROI 控制 |

## 5. 构建系统与检查产物

```bash
cd "$PLNX_PROJECT"
petalinux-build
ls -lh images/linux/{zynq_fsbl.elf,u-boot.elf,system.dtb,image.ub,boot.scr,rootfs.ext4}
```

构建成功后再打包。`zynq_fsbl.elf` 包含板级初始化；`u-boot.elf` 和 `system.dtb` 对应本次硬件；`image.ub` 包含内核与设备树；`boot.scr` 为启动脚本；`rootfs.ext4` 包含应用、配置和服务。

摄像头程序的最终编译入口是 `video_init_half_clock_hflip.c`，包含降频与方向设置，不能改成直接编译基础文件 `video_pipeline_init.c`。视频包同时安装 `S95video-pipeline-init`、`S98robot-control` 和 `S99video-status-panel` 等启动链接，UI 不需要另建同名配方。

FSBL 构建日志应含 `Installed MIO8 USB3320 reset in FsblHookBeforeHandoff`。需要检查根文件系统时，在构建主机只读挂载：

```bash
CHECK_MNT="$HOME/work/ac16-rootfs-check"
mkdir -p "$CHECK_MNT"
sudo mount -o loop,ro,noload "$PLNX_PROJECT/images/linux/rootfs.ext4" "$CHECK_MNT"
file "$CHECK_MNT/usr/bin/video_status_panel"
ls -l "$CHECK_MNT/etc/rc5.d/S95video-pipeline-init" \
      "$CHECK_MNT/etc/rc5.d/S98robot-control" \
      "$CHECK_MNT/etc/rc5.d/S99video-status-panel"
sudo umount "$CHECK_MNT"
```

程序应为 **32 位 ARM ELF**。若没有程序或启动链接，先检查包选择及 rootfs 构建，复制新 `image.ub` 不能代替修正根文件系统。

## 6. 用本次产物打包 BOOT.BIN

[board/boot.bif](../board/boot.bif) 展示冻结版本的布局，其 `release_assets/` 输入不随 Git 分发。从源码重建时创建自己的打包目录，明确使用**本次**文件：

```bash
export BOOT_DIR="$REPO_ROOT/work/rebuilt_boot"
mkdir -p "$BOOT_DIR"
cp "$PLNX_PROJECT/images/linux/zynq_fsbl.elf" "$BOOT_DIR/"
cp "$PLNX_PROJECT/images/linux/u-boot.elf" "$BOOT_DIR/"
cp "$PLNX_PROJECT/images/linux/system.dtb" "$BOOT_DIR/"
cp "$REPO_ROOT/work/implementation/system_wrapper.bit" "$BOOT_DIR/system.bit"
cat > "$BOOT_DIR/boot.bif" <<'BIF'
the_ROM_image:
{
    [bootloader] zynq_fsbl.elf
    system.bit
    u-boot.elf
    [load=0x00100000] system.dtb
}
BIF
(
    cd "$BOOT_DIR"
    bootgen -arch zynq -image boot.bif -o BOOT.BIN -w on
)
test -s "$BOOT_DIR/BOOT.BIN"
sha256sum "$BOOT_DIR/BOOT.BIN"
```

`bootgen` 来自已加载的 AMD 工具环境。四个载荷依次为 FSBL、PL bit、U-Boot 和加载到 `0x00100000` 的 DTB，不要漏掉最后一个。BOOT 内的 DTB、外部 `system.dtb` 和 `image.ub` 应来自同一次 PetaLinux 构建。导出 XSA 或生成 bit 不会自动更新 SD 上的 BOOT.BIN。

## 7. 整理自己的制卡文件

收集输出到独立目录，不覆盖仓库内的预编译文件：

```bash
export SD_FILES="$REPO_ROOT/work/rebuilt_sd"
mkdir -p "$SD_FILES/boot"
cp "$BOOT_DIR/BOOT.BIN" "$SD_FILES/boot/"
cp "$PLNX_PROJECT/images/linux/"{image.ub,boot.scr,system.dtb} "$SD_FILES/boot/"
cp "$PLNX_PROJECT/images/linux/rootfs.ext4" "$SD_FILES/"
cd "$SD_FILES"
sha256sum rootfs.ext4 > rootfs.ext4.sha256
gzip -c rootfs.ext4 > rootfs.ext4.gz
sha256sum boot/BOOT.BIN boot/image.ub boot/boot.scr boot/system.dtb rootfs.ext4.gz > sha256sums.txt
sha256sum -c sha256sums.txt
sha256sum -c rootfs.ext4.sha256
ls -lh boot rootfs.ext4*
```

按 [最小复现第4～7节](README.md#4-识别sd卡与定义变量) 识别 SD、分区、复制启动文件、写入／扩展 EXT4 并安全取卡，但将该流程的工作目录换为 `$SD_FILES`。

- 使用自己生成的校验清单、实际镜像尺寸和本地账号。预编译镜像的固定 SHA256、1 GiB 尺寸及演示口令不适用于源码重建。
- FAT32 第一分区根目录放四个启动文件；`rootfs.ext4` 写入第二分区，不能只复制到 FAT32。
- 第二分区须容纳本次 `rootfs.ext4`。示例的 128 MiB FAT32 第一分区也须容纳本次四个文件，扩展内核／FIT 后应重新核对容量。
- 完整根文件系统写入会替换已有配置、照片与账号，先正常关机并离线备份。只修改应用时也可独立更新用户态程序，无需每次重写分区。

## 8. 首次启动与验收

接线、SD 启动模式及 UART0 登录沿用 [最小复现第8节](README.md#8-开发板上电与首次登录)，使用自己配置的账号。板端先检查：

```sh
cat /proc/cmdline
findmnt /
df -h /
cat /run/video-pipeline.state
/etc/init.d/video-status-panel status
for u in /sys/class/uio/uio*; do
    printf '%s: ' "$u"
    cat "$u/name"
done
```

应从 `/dev/mmcblk0p2` 以 EXT4 可写挂载，容量对应扩展后的第二分区。UIO 按名称识别，不能要求某个 IP 固定为 `/dev/uio0`。控制地址和保留内存布局见 [接口说明](../board/interfaces.md)。

确认摄像头连续更新、触摸及温度显示后，在安静环境中以 root 权限生成本机噪声门限：

```sh
/etc/init.d/acoustic-display-control stop
/usr/bin/acoustic_display_control --calibrate
/etc/init.d/acoustic-display-control start
```

保持安静直至约 5 秒校准完成。在阵列前约 30 cm 播放固定 2 kHz，检查热点、频率、实时／稳定坐标；左右移动后应跟随，停声后叠加消退。再验证图像模式、ROI、抓拍和相册。机械臂在基本功能通过后接入，先核对 UART1 的 **9600、8N1**、电平及 [协议与区域映射](../board/interfaces.md)，由现场操作开启自动功能。

退出时使用界面关机或 `poweroff`，等待写盘结束后再断电。功能检查与量化性能测试的范围见 [测试与验证](../report/verification.md)。

## 9. 修改源码后的增量构建

**仓库源文件、PetaLinux 配方中的副本、SD 卡中的程序是三个位置。** 修改仓库不会自动更新后两者。例如修改 UI 头文件，先同步，再构建所属包及系统镜像：

```bash
cp "$REPO_ROOT/src/ps/video/video_status_render.h" \
   "$PLNX_PROJECT/project-spec/meta-user/recipes-apps/video-pipeline-init/files/"
cd "$PLNX_PROJECT"
petalinux-build -c video-pipeline-init
petalinux-build
```

修改多个文件时需同步全部依赖；也可完整运行导入脚本，但要留意第4节的账号／配置覆盖。怀疑包使用旧缓存时，针对该包清理后重建：

```bash
cd "$PLNX_PROJECT"
petalinux-build -c video-pipeline-init -x cleansstate
petalinux-build -c video-pipeline-init
petalinux-build
```

`video_status_panel`、`camera_snapshot`、`camera_album` 和 `robot_control_daemon` 均属于 `video-pipeline-init`，不能将程序名当作 `-c` 后的配方名。组件编译不会更新已部署 SD，也不等于完成根文件系统生成。

| 修改内容 | 后续处理 |
| --- | --- |
| 用户态 C／头文件、UI、服务 | 同步配方 → 构建包及 rootfs → 更新第二分区中的程序／配置，或离线写入新 rootfs |
| 设备树、内核或启动脚本 | 重建 PetaLinux，更新相关 FAT32 文件；DTB 改变时按四载荷布局重新打包 BOOT |
| FSBL、USB PHY 定制、U-Boot | 重建相应组件及系统，重新打包 BOOT.BIN |
| PL RTL、BD、IP 或约束 | 重建 bit／XSA → 重新导入硬件并构建系统 → 重新打包；地址变化同步控制接口 |

EXT4 模式下，仅改变应用时 `image.ub` 哈希可能不变，应检查新 `rootfs.ext4` 内的程序和包构建日志。保留文件清单，避免只更新 FAT32 四个文件而仍运行第二分区的旧应用。

## 10. 独立应用编译与回归

### 10.1 主机编译检查

PetaLinux 配方自动交叉编译应用，完整系统构建不要求另装 SDK。只检查源码编译时，在普通 Linux 主机终端运行：

```bash
cd "$REPO_ROOT"
env -u CC -u CFLAGS -u LDFLAGS python3 build/build_ps.py --output work/host-bin
```

通过标记为 `PASS_PS_BUILD 12 programs`，同时生成 `sha256.json`。这些是**主机架构程序**，不能复制到板端代替 ARM 程序。

### 10.2 ARM SDK 编译

需要脱离 BitBake 编译应用时，在 PetaLinux 终端生成并安装 SDK：

```bash
cd "$PLNX_PROJECT"
petalinux-build --sdk
petalinux-package sysroot --sdk "$PLNX_PROJECT/images/linux/sdk.sh" --dir "$HOME/work/ac16-sdk"
```

另开 Bash 终端，加载安装目录下实际生成的 `environment-setup-*` 文件。本平台为 Cortex-A9 32 位 ARM，例如：

```bash
source "$HOME/work/ac16-sdk/environment-setup-cortexa9t2hf-neon-amd-linux-gnueabi"
cd "$HOME/work/ac16"
python3 build/build_ps.py --output work/arm-bin
file work/arm-bin/video_status_panel
```

文件名不同时先查看 SDK 目录，再选择对应环境。脚本读取 SDK 的 `CC`、`CFLAGS`、`LDFLAGS`，输出应为 32 位 ARM ELF。SDK 终端与 PetaLinux 系统构建终端分开使用，避免混杂主机／目标编译器设置。

### 10.3 测试入口

在具备 GCC／Clang、Python 和 BusyBox 的普通 Linux 终端执行：

```bash
cd "$REPO_ROOT"
python3 build/audit_release.py
python3 build/test_software.py
```

对应工具终端中的 RTL 与 HLS C 仿真入口：

```bash
vivado -mode batch -source sim/run_rtl.tcl
vitis-run --mode hls --tcl sim/run_hls.tcl
```

HLS 入口执行 C 仿真，不会重新导出生产 IP。文件审计用于校验原始发布包；自行改动源码后，若清单哈希变化，应先审查再更新自己的发布清单。测试内容与证据见 [测试与验证](../report/verification.md)。

## 11. 常见构建问题

| 现象 | 检查与处理 |
| --- | --- |
| 找不到器件或 BD IP | 检查 2026.1 的 Zynq-7000 支持、IP 许可，以及源码解压完整性 |
| Windows 无法生成文件、路径过长 | 使用短路径和新输出目录，不混入其他工程的 `.gen`／`.runs` |
| 导入提示 `Existing files differ` | 确认选定独立工程后使用 `--replace`，旧文件会先备份 |
| 有配方但镜像没有应用 | 检查六项 rootfs 选择、同步的 `files/`，完成根文件系统构建 |
| FSBL hook 布局错误 | 保持 XSCT 路径及配方层级，核对工具版本与生成任务日志 |
| 打包找不到 `release_assets` | 按第6节用本次输出建立 BIF，参考 BIF 不附带输入二进制包 |
| 导入新 XSA 后仍为旧硬件 | 核对打包 bit、BOOT 哈希和实际 SD 文件，再完整掉电启动 |
| 更新启动文件后 UI 仍是旧版 | 检查第二分区的应用，不能只检查 `image.ub` |
| SDK 生成 x86 程序 | 加载正确 ARM SDK 环境，区分 `host-bin` 和 `arm-bin` |
| 登录、根分区或 USB 异常 | 使用自设账号，按 [运行排障](troubleshooting.md) 检查启动参数、EXT4 与供电 |

## 12. 实现参考与工具文档

| 项目 | 随包硬件的实现结果 |
| --- | --- |
| LUT | 25603 / 53200（48.13%） |
| FF | 46470 / 106400（43.67%） |
| BRAM | 129 / 140（92.14%） |
| DSP | 133 / 220（60.45%） |
| Setup WNS / Hold WHS | +0.136 ns / +0.036 ns |
| TNS / THS | 0 / 0 |

这些是随包版本的参考数据，不是每次重新实现的固定输出。报告见 [build/reports](../build/reports)，冻结文件关系见 [release_manifest.json](../board/release_manifest.json)。

AMD PetaLinux 2026.1 官方说明：[安装要求](https://docs.amd.com/r/en-US/ug1144-petalinux-tools-reference-guide/Installation-Requirements)、[新建工程](https://docs.amd.com/r/en-US/ug1144-petalinux-tools-reference-guide/petalinux-create-project-Options)、[本地用户](https://docs.amd.com/r/en-US/ug1144-petalinux-tools-reference-guide/Adding-Extra-Users-to-the-PetaLinux-System)、[生成 SDK](https://docs.amd.com/r/en-US/ug1144-petalinux-tools-reference-guide/Building-SDK)、[安装 SDK](https://docs.amd.com/r/en-US/ug1144-petalinux-tools-reference-guide/Installing-SDK)。
