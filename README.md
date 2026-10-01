# STM32 FreeRTOS 智能烟机控制系统

基于 STM32F103C8T6、HAL 和 FreeRTOS 的多任务控制项目，使用 Renode 运行固件、Python 注入传感数据与故障场景。

## 核心功能

- USART1 循环 DMA + IDLE/HT/TC 接收，环形缓冲与任务通知解耦中断和解析。
- 五任务协作完成通信、采样、控制、故障检测和运行监测。
- INIT / NORMAL / WARNING / FAULT / RECOVERY 状态机，支持风量分级、逆风告警、照明和恢复滞回。
- 输入超时、非法帧、队列溢出和任务停滞检测。
- 自动化场景测试、DMA 分段内存核对、栈余量与堆使用观测。

## 数据链路

```text
Python 场景 → Renode USART1 → DMA 256 B → 软件环形缓冲 512 B
→ 通知 CommTask → 传感快照 → SensorTask → 队列 → ControlTask
                                          FaultTask / MonitorTask
```

## 构建与运行

需要 Arm GNU Toolchain、Make、Python 3 和 Renode。在本项目目录运行：

```bash
make -j4
arm-none-eabi-size build/SensorTelemetry.elf
python3 tests/run_scenarios.py --corrected-dma-model --output results/my-dma-run
```

测试使用项目内的 Renode DMA 模型适配。输出目录使用新名称，端口 12345/12346 保持空闲；可用 `--renode /path/to/renode` 指定程序。

## 验证结果

| 场景 | 结果 |
| --- | --- |
| DMA 分段接收 | 264 字节逐段核对，覆盖半缓冲、回绕及使能位读取 |
| DMA 模式自动场景 | 19/19，通过 43.7 秒模拟运行 |
| 固件资源 | text 29388 B，data 108 B，bss 18772 B |
| 场景内最小剩余堆 | 4176 B |

[DMA 场景结果](results/resume_alignment/dma-diagnosis-20261001/dma-scenarios/validation-summary.json) · [分段验证证据](results/resume_alignment/dma-diagnosis-20261001/verified-enable/) · [故障定位过程](docs/dma_renode_diagnosis.md)

## 阅读导航

- [参数与技术学习手册](docs/TECHNICAL_GUIDE.md)
- [架构与验收记录](docs/resume_architecture.md)
- `Core/`：任务、状态机、UART DMA 与环形缓冲。
- `tests/`：主机单元测试与 Renode 场景。
- `renode/`：平台配置及项目内 DMA 模型。

## 来源

工程保留 STM32Cube/HAL、CMSIS、FreeRTOS 的原始署名与许可。Renode DMA 模型基于 Antmicro 实现调整，许可见 [MIT.txt](renode/licenses/MIT.txt)，修改说明见 DMA 故障定位文档。
