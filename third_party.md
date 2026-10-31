# 来源与第三方材料

- 基础声学相机：[wzwyz/ZYNQ-based-acoustic-camera](https://github.com/wzwyz/ZYNQ-based-acoustic-camera)，参考提交 `fe725b6882e491c85ea2ff42e0774adebc58306c`。原阵列接收、延时求和、摄像头/伪彩色叠加思路是继承基础，本项目贡献是器件/阵列适配、Linux迁移、稳定化、UI和区域执行联动。
- Vivado/Vitis 2026.1及PS、VDMA、VTC、Video In/Out、Mixer、BRAM、互连等AMD IP属于工具依赖；其生成代码中的版权声明不被删除。
- DVI与OV5640捕获IP保留openedv/alientek/Xilinx等文件内声明，未将“公开可下载”等同于可按MIT再分发。
- 字体的原许可随源码一起保留。生成字形子集不是新增字体版权授权。

已核对本地参考仓库的跟踪文件和公开仓库入口，没有发现根目录LICENSE。GitHub说明：公开仓库的可见性不等于开源许可；见 [GitHub Licensing a repository](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository)。

正式发布前应补齐上游与板级材料的许可文本、许可链接或作者允许再分发的记录；尚未收到的授权不由本目录臆造。详见 `license_scope.md`。
