# AC-16：Zynq 声源可视化与视觉辅助检测平台

基于 Zynq-7020 的便携式声学相机，集成 16 路麦克风阵列、OV5640 摄像头、HDMI 触摸界面和机械臂串口控制。PL 完成声学成像、图像处理和视频合成，Linux 负责测量、交互、记录与任务控制。

**版本：2026-10-31 · 器件：XC7Z020-CLG400-2 · 工具：Vivado / Vitis / PetaLinux 2026.1**

## 功能

- **声源定位**：16 麦双环阵列，200 Hz 高通、延时求和、64×36 能量网格，支持热点跟随、静音门控和动态色阶平滑。
- **图像处理**：1280×720 视频，原图、灰度、二值化、亮度偏移、反相、移位增益六种模式，支持全屏、手动 ROI 和声源跟随 ROI。
- **触摸交互**：顶部状态栏、半透明测量侧栏、模式与区域抽屉、参数滑块、XADC 温度显示和关机操作。
- **实时测量**：实时与稳定定位坐标、网格能量峰值、CH0 的 2048 点 FFT 主峰频率。
- **抓拍相册**：保存摄像头原图 BMP 和同次读取的测量 JSON，生成 160×90 缩略图，支持图片浏览与测量详情。
- **机械臂联动**：通过 UART 对接独立控制器，提供手动／自动夹取、12 区跟踪和测试动作页面。

## 硬件与软件

| 项目 | 配置 |
| --- | --- |
| 主控 | Zynq-7020，XC7Z020-CLG400-2 |
| 麦克风 | 16 个 ICS-43434，内环半径 25 mm、外环半径 50 mm |
| 声学采样 | 46.875 kHz，SCK 3 MHz |
| 摄像头与显示 | OV5640，1280×720，HDMI 输出 |
| 交互与控制 | USB 触摸、UART1 机械臂控制器接口 |
| 操作系统 | PetaLinux 2026.1，SD 第二分区 EXT4 根文件系统 |

声学数据在 PL 中经过采样、高通、分数延迟和能量计算，生成伪彩色热图。摄像头画面经 VDMA 和图像预处理，与热图叠加后进入 Video Mixer；Linux 渲染的 RGBA 双缓冲界面由 Mixer 合成输出。

## 快速开始

```sh
git clone https://github.com/rk-3588/zynq-acoustic-visual-inspection.git
cd zynq-acoustic-visual-inspection
```

### 1. 构建 FPGA 工程

安装包含 Zynq-7000 器件支持的 Vivado 2026.1，在 Vivado 命令环境中运行：

```sh
vivado -mode batch -source build/create_project.tcl -tclargs D:/ac16_build
vivado -mode batch -source build/build_hardware.tcl -tclargs D:/ac16_build/ac16_final.xpr
```

Windows 使用短输出路径以适应生成文件的路径长度。Linux 可省略 `-tclargs`，使用默认的 `work/vivado` 工程目录。构建结果输出到 `work/implementation`，包含 bit、XSA 和实现报告。

### 2. 准备 PetaLinux

创建 Zynq PetaLinux 2026.1 工程并导入生成的 XSA，再导入本项目配置与应用配方：

```sh
python3 build/stage_petalinux.py /path/to/petalinux_project
```

已有不同配置时，使用 `--replace` 备份后替换。在 PetaLinux 工程中配置本地登录账号，核对 EXT4 根文件系统和应用包后执行 `petalinux-build`。启动文件打包及 SD 部署见 [上板说明](board/README.md)。

### 3. 构建 Linux 应用

Linux 主机需要 Python 3.10+ 和 GCC 或 Clang：

```sh
python3 build/build_ps.py
```

程序默认输出到 `work/bin`。构建板端 ARM 程序时，先加载 PetaLinux SDK 环境，再执行相同命令。

### 4. 运行测试

软件回归在 Linux 上运行，另需安装 BusyBox：

```sh
python3 build/audit_release.py
python3 build/test_software.py
```

在对应工具命令环境中运行 RTL 和 HLS 测试：

```sh
vivado -mode batch -source sim/run_rtl.tcl
vitis-run --mode hls --tcl sim/run_hls.tcl
```

测试项目与结果见 [验证记录](report/verification.md)。

### 5. 首次上板

确认摄像头、触摸和温度显示正常后，在安静环境下校准声学背景。播放固定 2 kHz 声源，检查热点、坐标和左右跟随，再验证抓拍与相册。机械臂接线、串口参数和区域映射见 [接口说明](board/interfaces.md)。

## 实现数据

| 项目 | 最终版本 |
| --- | --- |
| LUT | 25603 / 53200（48.13%） |
| FF | 46470 / 106400（43.67%） |
| BRAM | 129 / 140（92.14%） |
| DSP | 133 / 220（60.45%） |
| Setup WNS / Hold WHS | +0.136 ns / +0.036 ns |
| TNS / THS | 0 / 0 |

对应报告保存在 [build/reports](build/reports)，版本与文件哈希见 [release_manifest.json](board/release_manifest.json)。

## 目录

```text
src/
  rtl/           声学与视频 RTL
  constraints/   引脚、时钟和 CDC 约束
  vivado/        BD、IP 配置与导出 RTL
  hls/           HLS C/C++ 源码
  ps/            Linux 应用与服务
  petalinux/     配方、设备树和启动配置
sim/             软件测试与 RTL 仿真
build/           构建、归档脚本和实现报告
board/           上板流程、接口及实测记录
data/            阵列几何和延迟 ROM
skill/           可复用 UART 接收诊断技能
report/          设计报告与验证资料
```

## 文档

- [设计报告](report/design_report.md)
- [上板与启动说明](board/README.md)
- [UIO、DDR 与串口接口](board/interfaces.md)
- [HLS 源码与测试](src/hls/README.md)
- [验证记录](report/verification.md)
- [UART 接收诊断技能](skill/README.md)

## 致谢与许可

基础声学相机参考 [wzwyz/ZYNQ-based-acoustic-camera](https://github.com/wzwyz/ZYNQ-based-acoustic-camera)。

本项目原创贡献采用 [MIT License](LICENSE)。第三方材料的来源和许可范围见 [third_party.md](third_party.md) 与 [license_scope.md](license_scope.md)。
