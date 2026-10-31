# HLS源码与冻结的导出RTL

本目录保存 `calculate`、`delay`、`inter`、`vram_add`、`add_image`、`color_mapping` 的C/C++源文件和已有测试。默认Vivado构建使用 `src/vivado/ip_catalog/` 中实际用于最终bit的导出RTL，因此不要求重新生成旧HLS组件，也不依赖开发电脑的HLS工程缓存。

两项C仿真可从仓库根目录执行：

```sh
vitis-run --mode hls --tcl sim/run_hls.tcl
```

需要Vitis HLS 2026.1及Zynq-7000器件支持。默认输出为 `work/hls_csim/`；Windows如需短路径，可将 `AC16_HLS_WORK` 环境变量设为独立的短输出目录。脚本只做C仿真，不综合、不覆盖冻结IP。脚本中的5ns仅为C仿真工程占位时钟，不是各导出IP的最终综合约束。

`delay`测试有独立参考和断言，检查2304方向、16路地址/权重、缓存指针回卷、ROM与有符号边界；`color_mapping`仅为单输入编译/运行冒烟检查，没有完整色阶正确性断言。其余旧测试保留为来源材料，不计入本包通过项：例如旧 `tb_vram_add.cpp` 的53位函数声明与当前55位接口不一致，不能原样当作当前接口验证。

`add_image`还依赖与原组件API匹配的Vitis Vision头文件（`common/xf_common.hpp` 等），这些外部库未复制进本包。本次没有重新综合全部HLS源文件或证明最新Vision库可直接替换。若修改算法后重新导出IP，需要核对VLNV、端口宽度、延迟、II和时钟约束，再独立重跑系统实现与上板验证；不能只替换C文件就视为已经更新最终bit。
