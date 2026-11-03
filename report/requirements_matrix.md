# 竞赛要求与发布材料对照

依据用户提供的AMD自选题指南原页19～22（本地4页PDF），已阅读全部页面；PDF不随源码复制。SHA256见 `submission_status.json`。

| 要求 | 本包材料 | 当前状态 |
| --- | --- | --- |
| 3.3.3.1 Vivado/Vitis 2026.1或2025.2，说明版本 | README、构建脚本、最终实现报告均记录2026.1 | 已整理 |
| 3.3.3.2 明确器件、板端实测 | XC7Z020-CLG400-2，`board/release_manifest.json`、`board/evidence/` | 已整理已有记录；不冒充新的板测 |
| 3.3.3.3 GitHub/Gitee开源与再分发许可 | MIT原创贡献、`license_scope.md`、`third_party.md` | 发布仓库为 `rk-3588/zynq-acoustic-visual-inspection`；上游/板级授权待确认 |
| 3.3.4 基线与优化对比、资源合理性 | 设计报告、最终资源/时序报告、采集降频与显示开销对照 | 已有明确记录；统一物理精度、功耗和时延基准仍待测 |
| 3.3.5.1 完整RTL/HLS/PS源码 | `src/`，包括独立IP、总线定义、最终摄像头变体 | 已整理，排除工作缓存 |
| 3.3.5.1 仿真与功能正确性说明 | `sim/`、ROM独立参考、软件/PTY测试、`report/verification.md` | 有可执行入口及真实结果；完整系统重新实现未重复执行 |
| 3.3.5.1 综合/实现报告 | `build/reports/`：时序、资源、DRC、布线、bus skew | 冻结bit对应报告已整理 |
| 3.3.5.1 上板流程、实测/参考比对 | `board/README.md`、只读状态脚本、`board/evidence/`、技能包黄金对照 | 已整理，镜像与源码分开 |
| 3.3.5.1 可复现构建脚本 | `build/create_project.tcl`、`build_hardware.tcl`、`build_ps.py`、`stage_petalinux.py` | 已提供并按验证记录标注测试范围 |
| 3.3.5.2 `skill/README.md`、适用/使用/失效条件 | UART接收技能、离线回放、Python/C前后对照、30项测试 | 已有完整包；不声称PYNQ通用Skill加分 |
| 3.3.5.3 设计原理、分工、优化判断和复现说明 | `report/design_report_v1.docx`、`design_report.md`、`design_facts.md`、`board/sd_quickstart.md` | 已提供含实物图的Word报告及最小启动说明；团队信息、量化指标和最终提交审阅仍待补，完整运行镜像另行提供 |
| 3.3.5.3 真实提示词、回答与纠错轨迹 | `report/ai_collaboration.md`、技能内 `references/collaboration.md` | 提供可追溯摘录，标注不是完整原始对话 |
| 3.3.5.4 英文目录/文件名和目录映射 | ASCII路径；普通文件小写，标准README/SKILL/LICENSE入口保留大小写 | 自动审计检查 |

指南中的PYNQ为推荐方向，不是本项目必须改用的框架。当前保留已验证的PetaLinux＋UIO实现，不为材料评分临时更换运行架构。

准备GitHub资料不等于已经通过竞赛全部验收。尤其不能将功能性观察改写成未测量的定位误差、异常识别准确率、功耗或SPL标定结果。
