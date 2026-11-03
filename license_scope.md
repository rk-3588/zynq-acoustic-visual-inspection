# 许可证适用范围

仓库维护者已选择MIT授权本项目原创贡献，根目录保留目标仓库已有的 `LICENSE`。该许可证**不重新许可第三方或上游材料**。

MIT适用于本项目新编写的发布/构建/验证脚本、独立UI及状态/频率/图像控制程序、看门狗、机械臂Linux协议服务、原创技能包内容，以及新增的 `acoustic_capture.v`、`video_preprocess.v`、`acoustic_result_adapter.v`、`acoustic_hpf_200.v`。对上游文件的新增修改部分也按MIT提供，但原文件的底层授权仍必须满足。

以下例外优先于根目录MIT：

| 内容 | 来源 / 状态 |
| --- | --- |
| 继承的采样、缓冲、延时求和/叠加基础、HLS和工程连接 | `wzwyz/ZYNQ-based-acoustic-camera`，本地参考提交 `fe725b6882e491c85ea2ff42e0774adebc58306c`；未发现明确LICENSE，允许修改/再分发的授权待确认 |
| `src/rtl/sensors.v`、`buffer_ctrl.v`、`vram_ctrl.v`、`video_driver.v`，HLS基础及相关导出IP | 上游或板级参考工程的适配版本；保留版权，不能仅凭根目录MIT授权其全部内容 |
| 摄像头SCCB基础初始化/寄存器表，以及摄像头/HDMI/极值搜索IP | 板级/上游材料；分别保留文件内声明，允许再分发的范围待确认 |
| `src/vivado/ip_catalog` 内AMD/Xilinx生成的IP、算术支持代码及驱动模板 | 保留原声明，使用和再分发受对应工具/IP许可约束，不标成团队原创MIT代码 |
| `video_status_font.h` 中字形数据 | Noto Sans SC：OFL-1.1；DejaVu：Bitstream Vera相关条款，许可证文本见 `src/ps/video/font_ofl.txt`、`font_dejavu.txt` |
| FSBL本体、U-Boot、Linux、PetaLinux系统组件 | 外部依赖，遵守各自许可证；`reproduce/` 提供相同板型的预编译运行文件，根目录MIT不重新许可其中第三方程序。对应应用源码与构建入口在本仓库，操作系统和工具来源见复现说明；不包含Vivado/Vitis/PetaLinux工具安装包 |

`src/petalinux/recipes/video-pipeline-init.bb` 暂保留组合包的CLOSED/字体声明，避免把含授权待确认的初始化基础强行标为全部MIT。独立原创应用配方可按MIT构建。

上游和板级材料的待确认状态保留在 `board/release_manifest.json` 与 `report/requirements_matrix.md` 中；源码仓库公开不改变这些材料各自的授权状态。
