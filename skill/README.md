# 大模型协作技能包

本目录为竞赛交付的可复用技能包。当前版本包含 `linux-uart-receive`：从 Linux 串口长回包超时案例提炼诊断流程、参数化回放工具、冻结 C 源码对照及验证证据。

## 适用范围

- Linux 串口长响应、分片接收和超时误判的证据分析。
- 按 TYPE 确定长度的二进制协议离线组帧验证。
- 附带脚本的协议族为 `AA 55 / TYPE / LE16 ID / PAYLOAD / 0D 0A`；其他帧格式可复用诊断方法，须适配解码器和校验规则。

## 从零运行

工作目录为本 `README.md` 的上一级，即交付包根目录。需要 Python 3.10 或更新版本；完整 C 对照验证另需支持 C99 的 GCC 或 Clang。回放和单元测试仅使用 Python 标准库。

```text
python skill/linux-uart-receive/scripts/uart_rx.py analyze skill/linux-uart-receive/fixtures/fifo_config.json
python skill/linux-uart-receive/scripts/uart_rx.py replay skill/linux-uart-receive/fixtures/fifo_config.json --host-idle-ms 50
python skill/linux-uart-receive/scripts/uart_rx.py replay skill/linux-uart-receive/fixtures/fifo_config.json --host-idle-ms 200
python skill/linux-uart-receive/scripts/verify.py
python skill/package_uart_skill.py
```

完整验证会在系统临时目录编译修复前后两个冻结 C 解析器，执行 Python/C 行为对照，并更新技能内的 `validation/result.json`。若只有 Python，可显式使用 `verify.py --python-only`；结果会标为部分验证，不能作为 C 验证通过的依据。

打包命令要求完整验证通过且输入未变化，生成ZIP、逐文件SHA256清单及ZIP校验文件，并核对压缩包内部内容。ZIP按`skill/`根目录组织，解压后可在任意目录按上述命令重新验证。修改技能文件后须先重新验证再打包。

## 供模型调用

在任务中指定读取 `skill/linux-uart-receive/SKILL.md`，并提供目标协议、源码和接收日志。也可将完整 `linux-uart-receive` 目录复制到所用工具的技能目录后调用 `$linux-uart-receive`。本交付未更改全局技能安装或模型配置。

## 已验证效果

历史 Zynq/Linux 案例中，130 字节配置响应以 56/56/18 字节交付，出现 59 ms 主机交付间隔。旧解析器的 50 ms 半包阈值丢弃候选；修复后将主机交付阈值独立为 200 ms，保持原请求期限及协议检查。历史板端两项只读配置查询得到 163 字节、2 个有效响应，过期/拒绝/忽略均为 0。来源与边界见技能内案例。

本轮离线验证的实际项目和结果记录在 `linux-uart-receive/validation/result.json`，与历史板端记录分开保存。全部测试均不访问真实串口或机械臂。

报告2.4案例正文见 `linux-uart-receive/references/case-study.md`；交互摘录、技术总结的来源与区别见同目录`collaboration.md`。

## 交付内容与边界

- `SKILL.md`：模型工作流程和决定条件。
- `scripts/`、`tests/`、`fixtures/`：离线分析、回放、真实 C 解析器对照和回归。
- `references/`：案例正文、协作摘录、来源摘要及适用/失效条件。
- `validation/`：本轮验证及文件校验值。

迁移时须重新核对帧格式、驱动交付行为、请求截止时间和业务规则。本包没有证明 200 ms 适合所有设备；也不将组帧成功或配置读取成功表述为动作执行成功。原项目凭据、部署脚本和完整串口登录日志不进入交付包。
