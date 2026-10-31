# 实时区域手动夹取与未知结果超时退出

2026-10-31，Asia/Singapore。已完成本地修改、主机/伪终端测试、ARM构建、虚拟机源码同步与板端部署；用户确认画面、实时左中右更新及确认窗口正常。本轮没有发送机械臂动作指令。

## 行为

- 手动夹取改用pl_control_daemon的最新有效raw_x/raw_y，不要求position_valid/stable。64×36网格按列0～20、21～42、43～63映射左、中、右。
- 夹取页及确认窗口持续更新实时区域和坐标。区域改变不会使确认窗口消失；确认按钮使用最新区域。短暂静音或无有效位置时保留窗口、禁用确认；声源恢复后可继续确认。原10秒确认期限保留。
- 服务接受手动请求时独立重新读取实时坐标。UI区域是提示值，若已变化，使用服务该次读取的最新区域，再应用robot-regions.conf映射、持久化并发送。
- 声学开启且已应用、active、≤150ms的新鲜数据、合法坐标、区域已启用、无其他任务在执行、有效持久化和nonce去重仍保留。
- 自动夹取的稳定位置、1.5秒驻留、每次声源只执行一次、静音重装/冷却和12区跟踪条件不变。本次放宽的是手动确认发送路径。
- 夹取等待结果仍默认30秒。进入未知结果后提示保留5秒，随后自动关闭自动模式、清除连接确认，持久化转为本地READY。界面在没有新回包时显示“未连接”，允许下一次有效的手动确认。
- 该退出不生成成功结果，不发送停止、探测、重试或下一条动作。恢复启动时遇到历史未知夹取任务也按5秒规则退出，任务号、nonce及最后已知完成ID保留。
- 提示保留期内匹配的迟到最终结果仍可解决旧任务，迟到“执行中”不延长截止时间；转READY后旧回包按非活动任务忽略。持久化/配置故障仍闭锁；跟踪/测试模式的未知不自动解除。

## 修改位置

`petalinux-video/video_robot_core.h`、`video_robot_service.c`：实时目标选择、手动服务端重新取区、未知结果计时和持久化退出。

`video_status_robot.h`、`video_status_robot_model.h`、`video_status_view.h`、`video_status_robot_render.h`：实时区域、动态确认窗口、失效时按钮禁用及提示文字。

`video_robot_modes.h`：显式保留自动夹取的原稳定目标路径。`video_status_panel.h`：版本更新。`robot-control.md`新增当前行为说明，覆盖下方历史规定。

## 测试

生产的5秒未知保留时间用于真实服务伪终端测试，未用缩短的hold替代验证。覆盖moving但raw有效、全部64列映射、UI提示与服务实时区域不一致、相同nonce无重复字节、执行中互斥、静音/过期/原始位置失效拒绝、默认未知提示计时、迟到进度/最终结果、持久未知重启、READY持久化及退出后新任务。

真实UI tick测试用临时状态文件和不可用的测试socket，覆盖左→右变化不关闭确认、静音禁用确认、声源恢复、按最新中区确认和10秒期限。所有操作均在主机临时目录/PTY进行。

FORTIFY及ASan/UBSan通过；既有串口字节协议、130字节FIFO分批交付、UI触摸/窗口、跟踪/测试互斥、配置、租约、自动夹取/跟踪及无重试回归均通过。日志：`build.log`、`ui-update-test.log`。

## 发布与部署

| 程序 | 版本 | SHA256 |
| --- | --- | --- |
| video_status_panel | 20261031-video-status-arm-live-v30 | 209d9a1fb5e0e6537106afec7170bc02c3be8feceb6e3ae88575a5d4570b91a8 |
| robot_control_daemon | 20261031-robot-control-v7 | d6c8ffb9d60904c8aee90989480cd0917e7645e1e5300b8bdedc6d2990d0f5fe |

板端旧程序/状态/日志备份：`/var/lib/video-pipeline-backups/before-arm-live-v30.ajXhgbDY/`。

已同步虚拟机PetaLinux配方的八个相关源码文件；原文件备份在 `<petalinux_project>/diagnostics/arm-live-20261031.cMZXTLeo/`。交叉编译使用该工程现有run.do_compile导出的工具链和环境。本轮没有完整重建镜像；更新直接写入板端EXT4根文件系统，重启继续使用。

BOOT SHA256仍为 `afb669a767b0d34bc73041bbc7ad5298cbdf94be8ddc9a4786047caee5a94445`，摄像头初始化器仍为 `60577766db1820e6d69378b5b5a5fa7d526b0dd90c202ac4acb86021b18a1e57`。未修改FPGA、摄像头降频/镜像、VDMA、VTC或相册。

部署只正常停止UI、正常停止机械臂服务、替换匹配程序对、启动机械臂服务并等待未知退出，再启动UI。原task390从phase2转phase0，日志 `unknown_expired_ready_no_retry`，UART1 TX/RX和新服务tx_frames/rx_frames均为0，自动模式0。

第一次开启UI再次出现已有公共显示异常：VPP接受SOF约80/s、采集无错。保存现场后正常stop/start同版UI，恢复约60/s，用户确认正常。未重启Linux、摄像头或视频服务。该显示同步问题仍独立存在，本次机械臂改动不作为其根治；记录 `ui-recover.log`，板端现场备份路径在其中。

程序配对必须一致，因为UI严格校验服务版本。回退程序时应正常停止两服务并恢复备份中的两个二进制；不要恢复旧任务状态文件覆盖之后产生的任务号。正常启动两服务不发送动作。

本地源文件修改前备份在 `before/`；构建输入和产物哈希在 `build_state.json`。部署及板端验证见 `board.log`、`board_install.json`、`ui-recover.log`。用户本轮只做了界面/声源观察与取消，未由本轮测试触发实际夹取。
