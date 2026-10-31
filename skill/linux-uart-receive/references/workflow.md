# UART 接收诊断与回放输入

## 1. 最小诊断材料

收集协议版本、请求/响应 TYPE 与完整长度、ID 端序、串口格式、驱动与 RX 配置、接收循环源码，以及同一请求的 TX/RX 计数、主机交付块长度/时间、解析拒绝/过期计数。材料不足时列出具体缺口，先不修改物理链路。

在现有串口拥有者的 `read()` 返回处采集单调时间和长度；一批数据共用这次交付时间。不要并开第二个读者争抢设备，也不要把批次中的字节按理论字符耗时倒推出一组“真实到达时间”。驱动计数增长可证明收到字节，不能独立证明帧完整、字段正确或控制器业务成功。

## 2. 三类时间分别判断

| 时间 | 含义 | 判断依据 |
|---|---|---|
| 线上字节空闲 | 电气接口上相邻字节的真实间隔 | 逻辑分析仪、硬件时间戳或协议发送端证据 |
| 主机交付空闲 | 应用收到相邻数据块的时间间隔 | 驱动行为与用户态单调时间 |
| 请求总截止 | 发出请求到允许接受完整响应的期限 | 业务协议及事务状态机 |

例如 9600/8N1 每字节占10位，56字节在链路上传输约58.33 ms，130字节约135.42 ms。主机可能连续接到多个FIFO批次；应用记录的59 ms间隔并不能证明线上有59 ms空闲。

`fifo_fill_estimate_ms`只估算水位数量字符的传输耗时。实际交付可能由RX timeout、DMA、USB串口桥、调度和应用缓冲共同决定；不能仅靠这一估算认定根因。

## 3. 离线 JSON 格式

参考两个完整样例：`../fixtures/fifo_config.json`与`../fixtures/alternate_protocol.json`。

```json
{
  "schema_version": 1,
  "origin": "synthetic example",
  "serial": {
    "baud": 9600,
    "data_bits": 8,
    "parity": "none",
    "stop_bits": 1,
    "fifo_trigger_bytes": 56
  },
  "protocol": {
    "type_lengths": {"02": 8},
    "max_frame_bytes": 130,
    "host_idle_ms": 200
  },
  "request": {
    "response_type": "02",
    "task_id": 8,
    "sent_at_ms": 0,
    "deadline_ms": 650
  },
  "events": [
    {"at_ms": 10, "data_hex": "aa 55 02 08 00 00 0d 0a"}
  ]
}
```

- `type_lengths`键为两位十六进制TYPE，值为整个响应的字节数；帧固定为`AA55 + TYPE + ID低/高 + 负载 + 0D0A`。TYPE不认识时进行重同步。
- `at_ms`为主机交付块的单调毫秒时间；`sent_at_ms`与之使用相同基准。时钟回退会撤销半包，并在结果中单独计数。
- `request`可省略。提供时只检查TYPE、ID和截止期限；不解释动作成功、标定有效等业务负载。
- 脚本支持无/奇/偶/mark/space校验，以及1/1.5/2停止位的字符耗时计算。输入受文件、事件数和总字节预算限制。

## 4. 使用与结果

在交付包根目录执行：

```text
python skill/linux-uart-receive/scripts/uart_rx.py analyze skill/linux-uart-receive/fixtures/fifo_config.json
python skill/linux-uart-receive/scripts/uart_rx.py replay skill/linux-uart-receive/fixtures/fifo_config.json --host-idle-ms 50
python skill/linux-uart-receive/scripts/uart_rx.py replay skill/linux-uart-receive/fixtures/fifo_config.json --host-idle-ms 200
python skill/linux-uart-receive/scripts/verify.py
```

`analyze`输出理论字符耗时、批次大小和主机交付间隔；`replay`输出帧、过期/拒绝/残留计数，以及按请求筛选的匹配、忽略和迟到帧。命令默认输出JSON，`--output FILE`可显式保存；输入文件不会被覆盖。

回放命令返回0表示输入已完成分析，不表示必然收到帧。历史50 ms案例得到0帧属于预期负对照；`verify.py`负责断言这些结果，并在失败时返回非零。

完整验证编译`protocol_before.h`和`protocol_after.h`，用相同合成轨迹驱动两份真实C代码，再与Python模型比对。C探针只读stdin并写stdout，不打开串口、网络或控制器IPC。`--python-only`明确跳过C验证，报告标记为`partial`。

## 5. 迁移步骤与失效条件

1. 复制轨迹格式，替换串口格式、响应类型长度、期望ID及请求期限；保留每次真实交付块的边界和时间。
2. 若帧格式包含LEN、不同端序、转义或CRC，先提供对应解码适配及测试；附带Python解码器和冻结C代码不能直接代表该协议。
3. 在目标驱动与代表性负载下观察交付间隔，选择有限的主机半包阈值，再验证总请求期限和异常恢复；不要直接推广本例200 ms。
4. 确认目标生产代码也通过等价测试。冻结C对照只证明本包所附版本；修改另一个项目后须重新编译并测试它的实际实现。

丢字节、真实断线、驱动错误、协议版本不匹配、设备实际未回复或响应总耗时超限时，放宽半包阈值不能解决问题。无CRC时，保持合法外形的负载错误仍可能通过组帧；附带测试明确展示这一边界。

## 6. 输出给使用者的结论

说明观测到什么、哪些假设被排除、修改了哪个时间判据、哪些协议/事务检查被保留。分别报告合成回放、生产源码验证和真实硬件结果；若没有实机权限或数据，保留未知结论。现场检查仍由原设备负责人授权，不能从离线技能调用推导出发送动作或重试命令的权限。
