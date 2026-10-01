# STM32 + FreeRTOS 架构与本地验收

2026-10-01追加结论：已通过逐段DMA内存/计数对照定位并修补Renode的
TIM1到DMA5额外请求接线和DMA EN恒假读回。完全相同的默认DMA固件
在项目内修补模型下完成264字节跨半满/重装/回绕验证及19/19场景，
fallback=0。见 [DMA诊断报告](dma_renode_diagnosis.md)。
当前DMA全链路状态为 VERIFIED（限定修补后的模拟环境）；实板未验证。
下文保留首次验收数据和当时的失败分析，最新结论以本追加记录为准。
更新：2026-10-01；证据目录 results/resume_alignment/20260930T143425Z/。
基线 HEAD 801a64af6c7d96cdc4196191e8b69137ab99cd65，本轮变更未提交。

## 目标与实际变化
应用是模拟烟机控制系统：外部 UART 传入烟雾、温度、湿度、光照、压差，
任务完成校验、控制策略、故障与健康监控。风机、灯光及传感器均按模拟项目描述。
原接收实现是单字节 HAL_UART_Receive_IT → 256 字节软件 ring → CommTask。
默认构建现为真正 HAL DMA 配置：
USART1 RX → DMA1_Channel5 circular buffer → HAL IDLE/HT/TC 回调 →
512 字节软件 ring → Direct Notification → CommTask 解帧。
软件 ring 使用一个空槽区分满/空，可用容量 511 字节；DMA buffer 为 256 字节。

## 外设和所有权
USART1：PA9 TX、PA10 RX、115200 baud、8 data bits、no parity、1 stop bit，无流控。
DMA1 Channel5：peripheral-to-memory，外设地址不增、内存地址自增，byte/byte，
circular，高优先级。DMA 时钟/初始化/链接在 HAL_UART_MspInit，CubeMX .ioc 同步记录。
USART1_IRQn、DMA1_Channel5_IRQn 的抢占优先级均为 5，
符合当前 FreeRTOS configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5，
同优先级两个中断不互相抢占，软件 ring 保持单生产者语义。

启动：CommTask → HAL_UARTEx_ReceiveToIdle_DMA。
IDLE：USART1_IRQHandler → HAL_UART_IRQHandler → HAL_UARTEx_RxEventCallback。
HT/TC：DMA1_Channel5_IRQHandler → HAL_DMA_IRQHandler → 同一 RxEventCallback。
回调读取实时 NDTR 得到写入位置，不盲用可能晚到的 HT 固定 Size。
uart_dma_drain 计算新增窗口、处理末尾及回绕，推送 ring，更新计数，
vTaskNotifyGiveFromISR + portYIELD_FROM_ISR 唤醒 CommTask。
ISR 不解析业务、不打印、不动态分配；CommTask 用 ulTaskNotifyTake 等待，
在任务上下文处理分片、粘连、非法字符、NUL、超长帧、失步恢复。
UART 错误回调只标记恢复并通知，任务中 AbortReceive/重启并丢弃残帧。

HT/TC 必须及时处理；一个完整 DMA 周期未服务时仅靠 NDTR 无法恢复丢失圈数。
115200 8N1 下 128 字节约 11.1 ms，256 字节约 22.2 ms。
这些是容量推导的服务预算，不是已测得的最坏 ISR 延迟。

## 任务与参数
| 任务 | FreeRTOS 优先级 | 栈配置（32-bit words） | 此次最小高水位（words） |
|---|---:|---:|---:|
| CommTask | 4 | 384 | 106 |
| SensorTask | 3 | 256 | 220 |
| ControlTask | 3 | 384 | 204 |
| FaultTask | 2 | 256 | 92 |
| MonitorTask | 1 | 512 | 310 |

MonitorTask 的 CMSIS 初始栈为 512*4 字节，运行后设置优先级 1。
高水位来自 irq-functional-01，不是 DMA 硬件负载下测量；乘以 4 才是字节。
FreeRTOS 有效 heap 配置 12288 字节，观测最低剩余 4176 字节。
配置头文件早期 3072 定义随后被 undef/12288 覆盖，不能只读第一行判断实际值。

| 参数 | 当前值 | 选择理由/调整影响 |
|---|---|---|
| DMA/ring | 256 / 512 B | ring 容纳两段 DMA 突发；扩大增加 SRAM、缓冲时延，缩小降低抗突发能力 |
| frame buffer | 96 B | 固定上限避免栈/解析无限增长；过长帧丢弃到换行 |
| 控制周期 | 100 ms | 模拟控制节拍；缩短更灵敏但占用更多 CPU |
| 健康周期 | 250 ms | 独立检查任务心跳；缩短增加监控开销 |
| 输入超时 | 2000 ms | 容忍短输入间断；增大故障反应变慢 |
| 任务超时 | 1500 ms | 软件健康监控阈值；不是独立硬件看门狗 |
| 恢复周期 | 10 | 恢复迟滞；增加减少抖动但恢复较慢 |
| SensorData queue | 4 | 有界任务解耦；增大掩盖消费迟滞并增加陈旧数据 |
| 烟雾阈值 | 100/300/600/850 | 模拟策略值，不能宣称认证安全阈值 |
| 压差/暗光 | -10 / 30 | 模拟倒灌/照明策略 |

保持 INIT、NORMAL、WARNING、FAULT、RECOVERY 五态。
故障注入使用 APP_SIMULATION_TEST_HOOKS=1 的 T,task,ms 命令，
用于暂停任务、触发队列/ring 溢出与健康报警；部署实机前需要关闭该模拟协议。

## 测试结果和环境边界
默认 DMA 固件编译/链接通过：text=29388、data=108、bss=18772 字节。
这是 size 工具各段数据，不把十进制合计 48268 当成 SRAM 使用量。
静态 RAM data+bss=18880，F103C8 标称 20 KiB SRAM 下余量很有限，
还需结合链接脚本预留、主栈及最坏中断嵌套做实机确认。

DMA 窗口主机单测通过：首段、重复位置、部分窗口、精确末尾、回绕、
跨边界、多帧、分帧、ring 满、顺序、无重复/无丢失。
1000 个受支持窗口共 63112 字节逐字节对账通过。
原 ring/状态策略单测也通过（state-ring-final.log）。

默认 DMA ELF 在当前 Renode 的第一次场景运行：
boot、RTOS heartbeat 通过；idle NORMAL 失败。
日志出现 DMA events 增长但有效帧为 0；原始 dma-renode-01 全部保留。
平台 stm32f103.repl 使用 STM32G0DMA 模型，TIM1 Update 与 DMA 通道存在共享接线；
当前证据不足以把整个失败唯一归因于模型，也不能声称 DMA 全链路已验证。
没有修改模拟器使测试凭空通过。DMA ISR/真实外设时序仍待匹配模型或实板验证。

为验证不依赖 DMA 仿真的控制业务，单独构建 APP_UART_RX_IT_FALLBACK=1，
保存在 build-irq-functional 和 irq-functional-01，默认 DMA 固件未被替换。
该显式 IRQ 模拟构建通过全部 19/19：
启动、心跳、NORMAL、中风、高风、倒灌 BOOST、暗光开灯、非法输入 FAULT、
恢复迟滞、通信超时、分片/三帧粘连、畸形/NUL/超长拒绝、ring 溢出、
ControlTask 停顿检测、队列溢出、最终恢复、全部任务栈余量。
模拟时间 43.7 秒。对应 text/data/bss=28524/108/18508 字节。
不能将 19/19 表述为 DMA 硬件测试通过。

固件 HSI 8 MHz、PLL off，Renode nvic 与 timer1 均设置 8000000；
串口日志心跳 tick=0/1000/2000，避免用 72 MHz 默认值把超时缩短约九倍。

## 后续设计与简历边界
下一步优先补 DMA/IDLE 模型或硬件串口验收、实测 ISR 延迟和 UART 错误恢复，
再决定是否缩栈/调整日志以腾出 SRAM。当前不要增加更多大静态缓冲区。
本轮按用户要求仅做本地验证，没有物理传感器、风机、电气安全或云通信验证。

| RESUME CLAIM | SOURCE FILES/FUNCTIONS | TEST | RESULT | STATUS |
|---|---|---|---|---|
| DMA1 Channel5 circular + IDLE | stm32f1xx_hal_msp.c、stm32f1xx_it.c、comm_task.c | DMA build / dma-renode-01 | 链接成功；模拟全链路未通过 | IMPLEMENTED_NOT_FULLY_VALIDATED |
| DMA 增量窗口/512 B ring | uart_dma_rx.c、uart_ring.c | dma_rx_test.c | 63112 字节对账通过 | VERIFIED |
| ISR Direct Notification | comm_task.c:RxEventCallback/CommTask | 源码/编译；IRQ 功能回归 | DMA ISR 到任务完整路径仍待验证 | IMPLEMENTED_NOT_FULLY_VALIDATED |
| 五任务、状态机和监控 | hood_app.c、sensor_task.c、control_task.c、fault_monitor.c | irq-functional-01 | 19/19，限定 IRQ 仿真构建 | VERIFIED |
| 堆/栈/时间参数 | FreeRTOSConfig.h、renode/hood.resc | 日志 / stm32-final-resources.json | 指定仿真下资源有余量 | VERIFIED |
