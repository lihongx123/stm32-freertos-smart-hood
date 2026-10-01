# DMA 接收逐段诊断与修复
日期：2026-10-01。范围：本地 Renode 模型与测试基础设施，未修改固件接收实现或 HAL。
证据：results/resume_alignment/dma-diagnosis-20261001/。
结论：两处模拟环境问题已通过隔离实验确认并修补；默认 DMA 固件在项目内修补模型下通过 19/19 场景。
这是软件模拟验证，不是物理 STM32、电气串口或最坏中断延迟认证。

## 固件对照
失败时保存的 dma-renode-01/SensorTelemetry.elf 与本次 build/SensorTelemetry.elf：
SHA-256均为 e0506759928ce4b706e95eb2597d1b5974e46f3bcc4159b2335b6a6ad3d10f46。
本次测试固件 APP_UART_RX_IT_FALLBACK=0，UART日志明确 fallback=0。

## 问题一：没有输入却搬运数据
原平台 stm32f103.repl 中：
TIM1 UpdateInterrupt 同时连接 NVIC25 和 DMA1 请求通道5。
本项目 TIM1 仅作 HAL tick，没有启用定时器 DMA 请求。
原模型接线把每次 tick 也作为 UART RX 使用通道的 DMA 请求，空 UART DR 被读为零。
boot-no-input：模拟50ms，NDTR从256降到207，49次无输入搬运，DMA内存是零。
加载另一个overlay重新连接NVIC不会删除原有DMA接线，timer-overlay实验保留为失败对照。
随后从原平台派生新平台，只删除 UpdateInterrupt 到 dma1@5 的连接：
没有输入时NDTR保持256，发送8/8/17字节后为248/240/223，DMA内存逐字节匹配。
底层UART/DMA引擎真实运行，没有直接把预期字节写入SRAM，也未伪造中断。

## 问题二：接收停在128字节
只隔离定时器的 wrap-isolated 实验：累计128字节后NDTR固定128，
发送后续字节也不再推进。
安装版本：Renode v1.17.0，build 1.17.0+20260922gitd6193cf47。
该Renode提交引用Infrastructure提交：
88afd12bb6b17b63ef58b1f842172ba015b60fec。
对应 STM32G0DMA.cs 对 EN 位配置 valueProviderCallback: _ => false，
读取CCR无法返回实际使能状态。HAL半传输中断会读取CCR；
该模型行为使随后接收无法继续。
项目内 HoodSTM32DMA.cs 保留上游模型，仅改类名并移除EN的恒假读回，
使用寄存器字段实际存储的使能值。模型其余行为保留。
固定模型的显式CCR读回测试同样通过，确认读取不会中断传输。

上游来源（保留版权与MIT许可，非独立原创整个DMA引擎）：
https://github.com/renode/renode-infrastructure/blob/88afd12bb6b17b63ef58b1f842172ba015b60fec/src/Emulator/Peripherals/Peripherals/DMA/STM32G0DMA.cs
许可：renode/licenses/MIT.txt。

## 逐段核对结果
verified-enable 包含每阶段DMA寄存器和完整256字节内存快照。
每步断言：完整环形内存等于发送序列映射、NDTR等于预期、
app_dma_bytes等于累计发送量、CCR.EN仍为1。
| 累计发送字节 | 预期NDTR | 实测NDTR | 内存/已交付计数 |
|---:|---:|---:|---|
| 0（启动及空闲） | 256 | 256 | 一致 |
| 8 | 248 | 248 | 一致 |
| 16 | 240 | 240 | 一致 |
| 33 | 223 | 223 | 一致 |
| 128 | 128 | 128 | 一致 |
| 192 | 64 | 64 | 一致 |
| 256 | 256（循环重装） | 256 | 一致 |
| 264 | 248 | 248 | 一致 |
| 再空闲200ms | 248 | 248 | 无额外搬运/重复计数 |

首次原平台baseline也读取了CCR，可能提前影响模型状态，不能用其后续停在49字节单独解释原故障。
后续wrap-isolated实验不额外读取CCR，仍准确复现HAL半传输中断后的128字节停顿。
修补版verified-enable故意恢复CCR观测，仍可顺利通过回绕，构成更严格对照。

## 场景回归
dma-scenarios/validation-summary.json：firmware_mode=dma，
corrected_dma_model=true、passed=true，19/19，模拟时间43.7秒。
涵盖正常风档、故障/恢复、通信超时、分片/三帧粘连、畸形/NUL/超长拒绝、
软件ring溢出、任务停顿、队列溢出及最终恢复。
末次1秒采样：DMA搬运4391字节、frame_ok175、frame_error14、
ring_overflow514、queue_drop17。这些错误/溢出由故障场景主动注入，
不是正常场景零错误的反例；末状态NORMAL，fault_mask=0。
最低heap4176 B；日志末段Comm/Sensor/Control/Fault/Monitor栈高水位
106/220/204/92/300 words。固件大小未变。
模型仍非周期精确硬件，保留的上游HT标志实现可能产生额外半传输回调，
不要据此推导真实ISR次数、CPU利用率或硬件极限吞吐。

## 复现
在 SensorTelemetry 仓库根目录执行，输出目录必须为新目录：
```bash
python3 tests/dma_probe.py --output results/resume_alignment/dma-probe-new --isolate-timer --fixed-dma --read-enable --verify
python3 tests/run_scenarios.py --corrected-dma-model --output results/resume_alignment/dma-scenarios-new
```
不加corrected-dma-model保留原平台运行方式，可用于反证。
修补模型仅动态加载到本次Renode实例，系统安装目录未被改写。
旧失败日志、旧IRQ19/19证据和首次DMA失败ELF都保留。

修改文件：
tests/dma_probe.py、tests/run_scenarios.py、
renode/HoodSTM32DMA.cs、renode/licenses/MIT.txt、
renode/uart-dma-diagnostic.repl（保留无效overlay实验说明）及相关验收文档。
git diff --check通过，未提交/推送。
