---
name: linux-uart-receive
description: "Diagnose Linux UART binary replies that time out or fail only on long frames. Distinguish driver-batched read delivery from wire byte timing, replay fragmented responses, and verify framing and transaction deadlines. Use for receive-side faults; hardware rewiring and actuator commands require their own task scope."
---

# Linux UART Receive Diagnostics

从已有源码、接收日志和协议约定定位串口长回包超时，形成可重复的离线证据，再决定最小修改。诊断方法适用于 Linux 串口；附带解码器针对 `AA 55 | TYPE | LE16 ID | PAYLOAD | 0D 0A`、按 TYPE 确定总长的协议族。

## 先确定时间与协议

- 分别记录波特率、每字符位数、帧长、驱动/FIFO 配置、每次 `read()` 的长度及单调时间。应用取到数据的时间只能说明主机交付时间。
- 按 `字符数 × (起始位 + 数据位 + 校验位 + 停止位) / 波特率` 估算批次形成时间。FIFO 水位提供一种批量交付机制，实际分批还受 RX timeout、调度和读取缓冲影响。
- 分开线上字节空闲约束、主机半包交付超时和完整请求截止时间。不能用主机时间戳精确恢复线上字节间隔。
- 核对响应 TYPE、总长、端序、ID 匹配和业务结果。成功组帧仅证明帧格式可解析。

## 工作流程

1. 先读 [诊断与输入格式](references/workflow.md)，收集最小材料；未经证据支持，不把超时直接归因于断线或波特率错误。
2. 用 `scripts/uart_rx.py analyze TRACE` 检查字符耗时、批次长度与交付间隔，用 `replay` 在显式主机超时下回放。脚本只处理本地 JSON，不打开串口、Socket 或设备节点。
3. 构造能区分旧行为与候选修复的测试。参考 `fixtures/fifo_config.json`；必须标记合成数据，保留真实记录的来源和时间含义。
4. 修改接收逻辑时保持协议长度、TYPE/ID 匹配、字段检查和请求总截止时间。主机交付阈值应由目标驱动和负载下的观测决定；本案例的 200 ms 不是通用常数。
5. 运行 `scripts/verify.py`，检查冻结的历史 C 解析器和参数化 Python 回放器。迁移到新协议时，还需对目标生产解析器运行等价回归。
6. 输出根因证据、修改范围、测试前后差异、仍未知事项和适用边界。历史实测与本次离线测试分别列出。

## 验证与约束

- 至少覆盖：分片/连续帧、分片头、负载内伪帧头尾、噪声/未知 TYPE、截断/坏帧尾、真正的主机空闲、错误 ID/TYPE、总截止超时和恢复组帧。
- 按已知 TYPE 长度定位帧尾，不能用遇到第一个 `0D 0A` 就截断长负载。无 CRC 的协议不能检出所有内容错误。
- 不自动更改波特率/FIFO、刷新 RX 缓冲、重试动作或发送探测帧。离线包不提供这些操作；实机验证须沿用用户已授权的范围和目标设备协议。
- 本案例的修复验证只涉及读取标定配置，不证明机械臂动作、夹持成功或其他板卡已通过测试。

## 按需参考

- [完整案例与报告正文](references/case-study.md)：问题、修正依据、历史板端结果及限制。
- [协作摘录与证据来源](references/collaboration.md)：真实摘录与事后技术总结分开标记。
- `references/evidence/`：冻结的修复前后头文件、脱敏字段摘录和来源 SHA256。
- `validation/result.json`：本轮离线验证结果；重新验证后该文件会更新。
