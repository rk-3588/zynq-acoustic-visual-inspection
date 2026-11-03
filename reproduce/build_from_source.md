# 从源码构建

预编译运行文件见本目录 [SD复现流程](README.md)。命令从仓库根目录执行。

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

已有不同配置时，使用 `--replace` 备份后替换。在 PetaLinux 工程中配置本地登录账号，核对 EXT4 根文件系统和应用包后执行 `petalinux-build`。启动文件打包及 SD 部署见 [上板说明](../board/README.md)。

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

测试项目与结果见 [验证记录](../report/verification.md)。

### 5. 首次上板

确认摄像头、触摸和温度显示正常后，在安静环境下校准声学背景。播放固定 2 kHz 声源，检查热点、坐标和左右跟随，再验证抓拍与相册。机械臂接线、串口参数和区域映射见 [接口说明](../board/interfaces.md)。

## 实现数据

| 项目 | 最终版本 |
| --- | --- |
| LUT | 25603 / 53200（48.13%） |
| FF | 46470 / 106400（43.67%） |
| BRAM | 129 / 140（92.14%） |
| DSP | 133 / 220（60.45%） |
| Setup WNS / Hold WHS | +0.136 ns / +0.036 ns |
| TNS / THS | 0 / 0 |

对应报告保存在 [build/reports](../build/reports)，版本与文件哈希见 [release_manifest.json](../board/release_manifest.json)。
