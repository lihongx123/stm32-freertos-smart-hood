# STM32F103 + FreeRTOS 智能烟机：源码面试指南

> 仅据本仓库当前源码、CubeMX `.ioc`、HAL/FreeRTOS 配置、Renode/Python 脚本和既有 `results/scenarios-final/`；本轮未改控制逻辑、未重跑测试。以下验证是 **Renode 仿真**，不是开发板或真实传感器/电机测试。源码位置均相对 `SensorTelemetry/`。

## 1. 从复位到五任务

`startup_stm32f103xb.s::Reset_Handler` 调 `SystemInit`，复制 `.data`、清零 `.bss`、调用 `__libc_init_array` 后进入 `main`；`Core/Src/main.c::main` 依次 `HAL_Init`、`SystemClock_Config`（HSI 8 MHz、无 PLL 倍频）、`MX_GPIO_Init`、`MX_USART1_UART_Init`，`osKernelInitialize`，`osThreadNew(StartDefaultTask,...)` 创建默认 CMSIS 任务，用户 `hood_app_init()` 再建 4 个 FreeRTOS 任务，`osKernelStart`。默认任务入口 `StartDefaultTask` 调 `MonitorTask`，所以运行中恰为 5 个应用任务。`startup`、`main.c` 的 CubeMX 框架及 HAL/CMSIS/FreeRTOS 库是生成/第三方代码；`hood_app.c`、`comm_task.c`、`sensor_task.c`、`control_task.c`、`fault_monitor.c`、`hood_state.c`、`uart_ring.c` 等是本项目业务实现。不能说自己编写 HAL 内核。证据：`startup_stm32f103xb.s`、`Core/Src/main.c`、`Core/Src/hood_app.c`。

### 硬件/外设实际配置

`.ioc` 为 `STM32F103C(8-B)Tx` LQFP48；构建与链接脚本具体选 **STM32F103C8Tx，Cortex-M3、Flash 64 KiB、SRAM 20 KiB**（`Makefile`、`STM32F103xx_FLASH.ld`）。USART1 PA9 TX/PA10 RX，115200 8N1；PC13 GPIO output；TIM1 用于 HAL tick timebase，FreeRTOS SysTick 另由内核配置；HSI 8 MHz。`Core/Src/stm32f1xx_hal_msp.c` 设置 USART1 IRQ priority 5。**应用 UART RX 是单字节 `HAL_UART_Receive_IT`，没有 DMA buffer、DMA 接收或 USART IDLE 中断处理。** 即使 Makefile 编入 HAL DMA 驱动，也不代表项目使用 DMA。无 IWDG、真实传感器、电机输出外设、FOC、Bootloader。证据：`.ioc`、`main.c`、`stm32f1xx_it.c`、`stm32f1xx_hal_msp.c`、`comm_task.c`。

## 2. Task 架构与调度

```text
USART1 IRQ → HAL RX complete → UartRing[256] → notify → CommTask(prio4)
                                                        ↓ mutex: latest SensorData
SensorTask(prio3, 100ms) → SensorData Queue[4] → ControlTask(prio3)
          ↓ heartbeat                             ↓ hood_step → logical fan/light/state
FaultTask(prio2, 100ms) ← invalid/timeout/queue/health → faults
MonitorTask(prio1, 250ms; CubeMX defaultTask) ← task heartbeat → health_mask/logs
                                         shared app_mutex / UART TX tx_mutex
```

| 任务 | 创建/入口 | 优先级、栈 | 等待与周期 | 输入→输出 |
|---|---|---|---|---|
| Comm | `hood_app_init` → `CommTask` (`comm_task.c`) | 4，384 **words** | `ulTaskNotifyTake(pdTRUE,100ms)`；无 DelayUntil | UART ring→帧解析→共享 latest/invalid/counters；启动 RX IT |
| Sensor | `hood_app_init` → `SensorTask` (`sensor_task.c`) | 3，256 words | `vTaskDelayUntil(100ms)` | mutex 快照 latest→`xQueueSend(...,0)` |
| Control | `hood_app_init` → `ControlTask` (`control_task.c`) | 3，384 words | `xQueueReceive(...,100ms)`；无 DelayUntil | queue+faults→`hood_step`→output、UART 状态日志 |
| Fault | `hood_app_init` → `FaultTask` (`fault_monitor.c`) | 2，256 words | `vTaskDelayUntil(100ms)` | invalid/时间戳/queue/health→faults |
| Monitor | `main.c` CMSIS defaultTask → `StartDefaultTask`→`MonitorTask` (`fault_monitor.c`) | 创建时 `osPriorityNormal`、stack_size=512×4 **bytes**；入口改 FreeRTOS priority 1 | `vTaskDelayUntil(250ms)` | 4 任务 heartbeat→health_mask；约 1s 输出指标 |

`FreeRTOSConfig.h`：`configUSE_PREEMPTION=1`，`configTICK_RATE_HZ=1000`，`configMAX_PRIORITIES=56`，heap_4，`configTOTAL_HEAP_SIZE` 后部有效覆盖为 12288 B（不能只看前面的 3072），mutex enabled；time slicing 未在项目配置显式定义，vendor `FreeRTOS.h` 默认 1。高优先 Comm 降低串口溢出风险；Sensor/Control 同级，Fault/Monitor 较低；这是设计意图，不是实测最优性。`configUSE_TIMERS=1` 不等于业务采用 FreeRTOS software timer，本项目周期逻辑使用 DelayUntil。证据：`Core/Inc/FreeRTOSConfig.h`、`Drivers/` 内 FreeRTOS 定义、`hood_app.c`、`main.c`。

## 3. USART → ISR → Ring → Notification → CommTask

`CommTask` 先 `HAL_UART_Receive_IT(&huart1,&rx_byte,1)`；硬件收到 1 字节 → `stm32f1xx_it.c::USART1_IRQHandler` → `HAL_UART_IRQHandler` → `comm_task.c::HAL_UART_RxCpltCallback` → `uart_ring_push_isr` 将字节写 256 槽 SPSC ring → 立刻再启动下一字节 RX IT → `vTaskNotifyGiveFromISR(app_tasks[TASK_COMM],&wake)` → `portYIELD_FROM_ISR(wake)`，仅在高优先任务被唤醒时请求调度。`CommTask` 用 `ulTaskNotifyTake(pdTRUE,100ms)` 清零式计数通知，醒后一直 `uart_ring_pop` 到空；按 `\n` 结束、忽略 `\r`、ASCII 可打印/长度检查，`sensor_frame_parse` 读 `S,smoke,temp,humidity,light,differential_pressure` 五整数并验证范围。错误回调重新 arm RX。ISR 不解析字符串、不持有业务 mutex、不做阻塞 UART TX；这是降低 IRQ 占用的源码事实。无 DMA buffer、IDLE 检测函数，不能在简历答为“DMA+IDLE 接收”。证据：`Core/Src/comm_task.c`、`Core/Src/stm32f1xx_it.c`、`Core/Src/uart_ring.c`。

`UartRing` 为 256 byte 数组、volatile head/tail/counters，单 ISR producer、单 CommTask consumer，`next(head)==tail` 为满、`head==tail` 为空，保留 1 槽，最多 255 byte；溢出计数递增。源码用原子 fence 辅助可见性，但只能陈述其目标是 SPSC 无 mutex，不宜未经目标平台内存模型证明声称“通用完全 lock-free”。CommTask 观察 overflow 变化即丢弃当前帧直到换行；非打印字符/超长帧也丢弃并记录 invalid。direct notification 比二值 semaphore 更轻量是【工程分析】，当前源码实际使用计数型 give/take 且 `pdTRUE` 清除计数，不能说是队列载荷传递。证据：`Core/Inc/uart_ring.h`、`Core/Src/uart_ring.c`、`comm_task.c`。

其他关键 IRQ：`TIM1_UP_IRQHandler` 经 HAL TIM 回调推进 `HAL_IncTick`（HAL timebase）；FreeRTOS SysTick/PendSV/SVC 由启动向量和内核 port 处理调度，不是项目业务 ISR；USART1 IRQ priority5 与 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 相容，故可调用 `...FromISR`。没有应用 DMA IRQ/IDLE 回调/IWDG IRQ。证据：`Core/Src/stm32f1xx_it.c`、`Core/Src/main.c`、`Core/Inc/FreeRTOSConfig.h`。

## 4. 数据、队列、锁与生命周期

`tools/sensor_sim.py` 生成五字段 ASCII 帧 → `tests/run_scenarios.py` 通过 Renode UART TCP terminal 送入 → USART1 → ring → CommTask 的 `sensor_frame_parse` → `app_runtime.latest` → SensorTask 快照 → `sensor_queue` → ControlTask `hood_step` → app_runtime.output/UART 日志 → Python 断言。`SensorData` 包括烟雾、温度、湿度、光照、压差、tick、valid；温湿度被解析/校验，但当前风机决策主要依据烟雾、压差、光照和 fault，不能说有温湿度闭环。证据：`Core/Inc/app_types.h`、`comm_task.c`、`hood_state.c`、`tools/sensor_sim.py`。

唯一业务队列 `sensor_queue=xQueueCreate(4,sizeof(SensorData))`，SensorTask `xQueueSend(...,0)` 非阻塞，ControlTask `xQueueReceive(...,100ms)`；满则 `queue_drop++`、记 fault tick。用值拷贝隔离生产/消费的时序与同一全局结构竞争，但共享 latest/faults/output 仍需 `app_mutex`。`app_mutex` 保护 `app_runtime` 多任务读写；`tx_mutex` 序列化 `app_log` 中的阻塞 `HAL_UART_Transmit(...,100ms)`，避免多任务日志交织。二者是 FreeRTOS mutex，内核 mutex 使用优先级继承；但无法消除所有高优先任务被低优先日志传输拖延的问题。全局对象/handle 在 `hood_app.c` 定义、`hood_app_init` 初始化，任务运行期间静态存活：`app_runtime`、`app_ring`、`app_mutex`、`sensor_queue`、`app_tasks[]`，`tx_mutex` 文件静态；`HoodState` 在 ControlTask 栈上，`SensorData` 在 Comm/Sensor/Control 数据路径按值传递。证据：`Core/Inc/hood_app.h`、`Core/Src/hood_app.c`、`sensor_task.c`、`control_task.c`。

## 5. 五态状态机与故障恢复

定义：`SystemState {INIT,NORMAL,WARNING,FAULT,RECOVERY}`、`FanMode {OFF,LOW,MEDIUM,HIGH,BOOST}` (`app_types.h`)；阈值 `smoke 100/300/600/850`、backflow `dp<=-10`、dark `light<30`、恢复计数10 (`app_config.h`)。唯一转移函数 `hood_step` (`hood_state.c`)：

```text
任意状态 -- faults != 0 --> FAULT (recovery_cycles=0)
FAULT -- faults == 0 --> RECOVERY (counter=0)
RECOVERY -- 连续无 faults、counter<10 --> RECOVERY
RECOVERY/INIT/NORMAL/WARNING -- 满足计数或正常判定且 valid -->
        smoke>=850 或 dp<=-10 ? WARNING : NORMAL
```

FAULT 是 fault bit 非零的即时安全态，风机 BOOST；无 fault 后先入 RECOVERY，十次 `hood_step` 无 fault 才回 NORMAL/WARNING，这个滞后降低故障清除瞬间的反复切换，但不是通用输入去抖。warning 条件（烟雾≥850 或倒灌压差≤−10）也使风机 BOOST；否则烟雾≥600 HIGH、≥300 MEDIUM、≥100 LOW、其余 OFF；有效且光照<30 则 light_on，失效关闭灯。`INIT` 是初始默认状态；后续由有效数据推进。`ControlTask` 调 `hood_step`，FaultTask **只算 fault bits**，MonitorTask **只算 health mask**，不要说 FaultTask 直接改风机。`HoodState` 记录 transitions 和进入 FAULT 次数。注意若无 faults 但 `data.valid=0`，`hood_step` 可停留在既有状态而 fan OFF；通常 invalid 另被 FaultTask 检出，解释需考虑任务时序。证据：`hood_state.c`、`control_task.c`、`app_types.h`、`app_config.h`。

FaultTask 每 100ms：invalid→bit2；有效数据/tick 超过2000ms→sensor timeout bit1；最后完整帧超过2000ms→communication timeout bit4；近2000ms 曾 queue_drop→bit8；health_mask 非零→task bit16。MonitorTask 每250ms比较 Comm/Sensor/Control/Fault 4 个 heartbeat，超过1500ms 标位，写 `health_mask`，约每 1s 输出 HEARTBEAT/METRIC/MEM/STATUS；它是**软件任务健康监测，不是硬件 IWDG，也不会复位 MCU**。若高优先任务彻底饿死低优先 Monitor，软件检查本身无法及时运行。`APP_SIMULATION_TEST_HOOKS=1` 允许 `T,task,ms` 模拟停顿用于 Renode 故障注入；面试需说明这是测试钩子，不是生产命令协议。证据：`fault_monitor.c`、`comm_task.c`、`app_config.h`。

## 6. 内存与 Renode 证据

`results/scenarios-final/size.txt`：text 27576 B（代码/只读等 Flash 段统计）、data 108 B（有初始化静态数据，运行时占 RAM 并有 Flash 初始化镜像）、bss 18164 B（零初始化静态 RAM，包括静态 FreeRTOS heap_4 数组等）。这不是“实际运行总 RAM=18164 B”：任务栈分配、动态对象、链接布局、堆剩余和具体段须分开讲。`MonitorTask` 打印 `xPortGetFreeHeapSize()`；`results/scenarios-final/measured-metrics.json` 跨采样的最低 observed free heap **4168 B**，不能误称为 API `xPortGetMinimumEverFreeHeapSize()` 的精确历史最低。`uxTaskGetStackHighWaterMark` 单位为 **words**，采样最低 Comm106/Sensor220/Control204/Fault92/Monitor310 words，均>0；这是被测负载下的余量，不证明所有真实场景都安全。构建链接 64KiB Flash/20KiB SRAM。证据：`FreeRTOSConfig.h`、`fault_monitor.c`、`results/scenarios-final/{size.txt,measured-metrics.json}`。

`renode/hood.resc` 加载 Renode 自带 `platforms/cpus/stm32f103.repl`，设置 NVIC/TIM1 为8MHz，加载 `build/SensorTelemetry.elf`，把 usart1 接到本地 TCP terminal 12346；Renode monitor 在12345。`tests/run_scenarios.py` 构建 ELF、存 hash manifest、启动 headless Renode、通过 monitor `emulation RunFor` 推进**虚拟时间**并由 `tools/sensor_sim.py` 写 UART，读取 MCU UART 输出按字符串/计数断言；保存 `renode-console.log`、`scenario-run.log`、`sensor-tx.jsonl`、`monitor.log`、`validation-summary.json`。这是 firmware-in-the-loop 仿真，不是硬件示波器/物理实时测量。`results/scenarios-final/validation-summary.json` 为 43.7 仿真秒内 **19/19 检查项通过**（不是 19 种独立场景）。

19 项逐一：① boot：BOOT/RTOS_START/COMM_READY；② RTOS heartbeat；③ idle→NORMAL；④ light cooking→MEDIUM；⑤ heavy cooking→HIGH；⑥ backflow→WARNING/BOOST；⑦ dark→LIGHT ON；⑧ invalid sensor→FAULT；⑨ recovery 先 RECOVERY 后 NORMAL；⑩ communication/sensor timeout→FAULT mask5；⑪ 碎片帧+三帧 burst 计数至少+3；⑫ malformed 拒绝→FAULT mask2；⑬ embedded NUL 拒绝；⑭ overlong 拒绝；⑮ 暂停 Comm 后 ring overflow>0；⑯ 暂停 Control 后 HEALTH stalled=4；⑰ queue_drop>0；⑱ 最终 NORMAL 且 fault_mask0；⑲ 所有任务栈 high-water>0。对应输入场景 `tools/sensor_sim.py::SCENARIOS` 与串口注入在 `tests/run_scenarios.py`。最终周期计数 uart_rx_bytes4391、frame_ok175、frame_error14、ring_overflow770、queue_drop17；这些错误是**故意注入**的测试事件，不应声称生产负载“零错误”。无真实传感器、电机、板卡、DMA+IDLE、硬件 IWDG、FOC、Bootloader 测试或实现。

## 7. 设计取舍与可说的限制

【源码事实】ISR 仅入 ring+rearm+notify，CommTask 解析；Sensor→Control 用长度4的值拷贝 Queue；共享状态用 mutex；5 任务分工；状态机及 10 次恢复；Renode 通过串口和 Python 判定。【工程分析】ISR 不解析可降低占用和中断优先级干扰；ring 缓冲 UART burst 但固定256可能溢出；通知只传“有新数据”而不是字节本体，比 semaphore 少一个独立内核对象；queue 解耦周期任务但满时数据丢弃被故障检测；mutex 维护一致快照但应缩短临界区；分任务提高职责清晰度但增加调度/栈成本；状态机使故障态/恢复态明确；Renode 提高可重复性，却不能替代板卡电气/时序/驱动验证。替代方案 DMA+IDLE、二值信号量、stream buffer、单任务循环或硬件 IWDG 均**未在当前项目实现或做量化对照**。

## 8. 面试题速记（每题有源码 S；适用时给结果 R）

口述时先指出事实，再谈原理和边界。缩写：A=`Core/Src/hood_app.c`，C=`Core/Src/comm_task.c`，RING=`Core/Src/uart_ring.c`，S=`Core/Src/sensor_task.c`，CTRL=`Core/Src/control_task.c`，F=`Core/Src/fault_monitor.c`，SM=`Core/Src/hood_state.c`，CFG=`Core/Inc/app_config.h`，RT=`Core/Inc/FreeRTOSConfig.h`，T=`tests/run_scenarios.py`，E=`results/scenarios-final/validation-summary.json`。

### MCU 基础 20

| # | 问题与答题锚点 | 证据 |
|---|---|---|
| M01 | 复位后第一段应用代码？Reset_Handler，随后 SystemInit/data/bss/main。 | S:startup_stm32f103xb.s |
| M02 | 实际 MCU 型号？构建为 F103C8Tx；`.ioc` 为 F103C(8-B)Tx。 | S:Makefile、SensorTelemetry.ioc |
| M03 | Cortex 核心？Cortex-M3。 | S:Makefile |
| M04 | Flash/SRAM？链接脚本 64KiB/20KiB。 | S:STM32F103xx_FLASH.ld |
| M05 | 时钟多少？当前 HSI 8MHz、无 PLL 倍频。 | S:Core/Src/main.c::SystemClock_Config |
| M06 | HAL_Init 在哪？main 起始。 | S:Core/Src/main.c |
| M07 | USART 引脚？PA9 TX/PA10 RX。 | S:SensorTelemetry.ioc、stm32f1xx_hal_msp.c |
| M08 | UART 波特率？115200 8N1。 | S:Core/Src/main.c::MX_USART1_UART_Init |
| M09 | TIM1 用途？HAL tick timebase。 | S:stm32f1xx_it.c、main.c |
| M10 | GPIO 用途？PC13 output 初始化，非真实电机控制。 | S:SensorTelemetry.ioc、main.c |
| M11 | USART1 IRQ 入口？USART1_IRQHandler→HAL_UART_IRQHandler。 | S:stm32f1xx_it.c |
| M12 | RX 用 DMA 吗？无，逐字节 HAL_UART_Receive_IT。 | S:C |
| M13 | 用 IDLE 判帧吗？无，按换行分帧。 | S:C |
| M14 | 中断优先级？USART1 NVIC priority5，允许 FromISR API。 | S:stm32f1xx_hal_msp.c、RT |
| M15 | UART 错误怎样恢复？ErrorCallback rearm IT。 | S:C |
| M16 | ISR 为什么短？仅 ring+notify，解析在任务上下文。 | S:C |
| M17 | `.data`/`.bss` 谁初始化？Reset_Handler。 | S:startup_stm32f103xb.s |
| M18 | 栈与堆如何区分？任务栈从 heap_4 创建，静态 heap 数组在 bss。 | S:RT、Makefile；R:results/scenarios-final/size.txt |
| M19 | 本项目用硬件 IWDG？没有，只有软件任务监测。 | S:F、main.c |
| M20 | 仿真与板卡区别？Renode 执行 ELF/UART，不验证真实电气。 | S:renode/hood.resc、T；R:E |

### FreeRTOS 20

| # | 问题与答题锚点 | 证据 |
|---|---|---|
| R01 | 抢占式吗？configUSE_PREEMPTION=1。 | S:RT |
| R02 | tick 频率？1000Hz。 | S:RT |
| R03 | time slicing？配置未显式定义，vendor 默认1。 | S:RT、FreeRTOS.h |
| R04 | 最大优先级数？56。 | S:RT |
| R05 | heap 哪种？heap_4，实际配置12288B。 | S:RT、Makefile |
| R06 | 为什么有5任务？main 默认任务转 Monitor，hood_app_init 建4。 | S:main.c、A |
| R07 | Comm 优先级？4，最高业务级。 | S:A |
| R08 | 同优先级有哪些？Sensor/Control 都3。 | S:A |
| R09 | 哪些 DelayUntil？Sensor/Fault/Monitor。 | S:S、F |
| R10 | Control 用什么等？xQueueReceive 100ms。 | S:CTRL |
| R11 | Comm 用什么等？ulTaskNotifyTake 清零、超时100ms。 | S:C |
| R12 | FromISR 通知谁？UART RX callback 通知 Comm。 | S:C |
| R13 | 何时 portYIELD_FROM_ISR？wake=pdTRUE 请求切换。 | S:C |
| R14 | queue 大小与类型？4×SensorData 值拷贝。 | S:A、S |
| R15 | queue 满如何办？不阻塞，drop计数+fault tick。 | S:S |
| R16 | 有几个 mutex？app_mutex、tx_mutex。 | S:A |
| R17 | mutex 优先级继承？FreeRTOS mutex 机制启用，非所有倒置都消除。 | S:A、RT |
| R18 | heap 可用量如何采？xPortGetFreeHeapSize 周期日志。 | S:F；R:measured-metrics.json |
| R19 | stack high-water 单位？words，且仅被测路径余量>0。 | S:F；R:measured-metrics.json |
| R20 | 应用 software timer？没有使用 xTimer，周期用 DelayUntil。 | S:S、F、RT |

### 当前源码 20

| # | 问题与答题锚点 | 证据 |
|---|---|---|
| C01 | RX callback 做什么？push/rearm/notify/yield。 | S:C |
| C02 | ring 总槽和有效容量？256 槽、最多255字节。 | S:RING、CFG |
| C03 | ring 谁写谁读？ISR 写、CommTask 读。 | S:C、RING |
| C04 | 帧分隔符？换行，CR 忽略。 | S:C |
| C05 | 帧最大缓冲？96 char，包括终止符空间。 | S:C、CFG |
| C06 | 五个输入字段？烟雾/温度/湿度/光照/压差。 | S:C、app_types.h |
| C07 | 输入范围在哪校验？sensor_frame_parse 的 strtol/range。 | S:C |
| C08 | NUL 怎么处理？非打印→discard 到换行。 | S:C；R:E |
| C09 | ring overflow 后？discard 当前帧并计数。 | S:C、RING；R:E |
| C10 | latest 谁更新？CommTask under app_mutex。 | S:C |
| C11 | Queue 谁生产/消费？SensorTask→ControlTask。 | S:S、CTRL |
| C12 | FaultTask 设什么？fault bit mask，不直接控电机。 | S:F、CTRL |
| C13 | ControlTask 何处执行控制？hood_step。 | S:CTRL、SM |
| C14 | 阈值是多少？100/300/600/850、dp -10、dark30。 | S:CFG、SM |
| C15 | WARNING 条件？smoke≥850 或 dp≤−10。 | S:SM |
| C16 | RECOVERY 何时结束？无 faults 约10次 hood_step。 | S:SM |
| C17 | 软件健康怎么判断？4任务 heartbeat 超1500ms。 | S:F、CFG |
| C18 | 测试停顿命令是什么？模拟钩子 T,task,ms。 | S:C、CFG；R:E |
| C19 | 日志谁串行化？tx_mutex+HAL_UART_Transmit。 | S:A |
| C20 | 输出是实际 PWM 吗？不是，状态和 UART 日志逻辑输出。 | S:CTRL、main.c |

### 故障定位 10

| # | 问题与答题锚点 | 证据 |
|---|---|---|
| X01 | heavy smoke 不到 HIGH 怎么查？检查注入帧、parse、阈值、UART log。 | S:tools/sensor_sim.py、C、SM；R:E |
| X02 | backflow 没 BOOST？看 dp≤−10、warning 分支。 | S:CFG、SM；R:E |
| X03 | 暗光灯不亮？看 valid 与 light<30 双条件。 | S:SM；R:E |
| X04 | 错帧后为何 FAULT？Comm invalid→FaultTask bit2→Control。 | S:C、F、CTRL；R:E |
| X05 | 无输入2s后为何 mask5？sensor bit1+comm bit4。 | S:F；R:E |
| X06 | Ring overflow 怎么复现？暂停 Comm、继续注入1024字节。 | S:C、RING、T；R:E |
| X07 | Queue 满怎么定位？queue_drop 与 fault tick；Control stall。 | S:S、T；R:E |
| X08 | Control 卡住怎么识别？Monitor heartbeat age>1500、stalled=4。 | S:F、T；R:E |
| X09 | 恢复为何不是立即 NORMAL？10次无 fault 的 RECOVERY。 | S:SM、CFG；R:E |
| X10 | Heap/stack 告警怎么看？free_heap/high-water 单位和采样窗口。 | S:F、RT；R:measured-metrics.json |

### 设计取舍 10

| # | 问题与可诚实回答 | 证据 |
|---|---|---|
| D01 | ISR 不解析的原因？缩短中断占用；解析在 CommTask。 | S:C |
| D02 | 为什么 Ring？缓存突发；容量255且可溢出。 | S:RING、CFG；R:E |
| D03 | 为什么 Notification？只需唤醒，字节留在 Ring；未比较性能。 | S:C |
| D04 | 为什么 Queue？快照值传递，解耦 Sensor/Control 周期。 | S:S、CTRL |
| D05 | 为什么 Mutex？共享 latest/faults/output 和串口日志。 | S:A、C、F |
| D06 | 为什么5任务？通信/采样/控制/故障/健康职责分离，有栈开销。 | S:A、main.c |
| D07 | 为什么状态机？显式 FAULT/RECOVERY 与 BOOST 安全输出。 | S:SM |
| D08 | 为什么恢复10次？避免故障位短暂清除即回正常；实测最优性未知。 | S:CFG、SM |
| D09 | 为什么 Renode？可重复注入并跑真实 ELF，不等同硬件。 | S:renode/hood.resc、T；R:E |
| D10 | 为何不声称 DMA+IDLE/IWDG？源码无这些功能，不能靠简历推测。 | S:C、F、main.c |

## 9. 推荐阅读与面试复述路线

`startup_stm32f103xb.s::Reset_Handler` → `Core/Src/main.c`（时钟/USART/GPIO/RTOS）→ `SensorTelemetry.ioc`、`stm32f1xx_hal_msp.c`、`stm32f1xx_it.c` → `FreeRTOSConfig.h` → `hood_app.c::hood_app_init` → `comm_task.c::HAL_UART_RxCpltCallback/CommTask` → `uart_ring.c` → `sensor_task.c`/Queue → `control_task.c::hood_step` → `hood_state.c` → `fault_monitor.c::FaultTask/MonitorTask` → `renode/hood.resc` → `tools/sensor_sim.py` → `tests/run_scenarios.py` → `results/scenarios-final/`。先能手画 USART→ISR→Ring→Notification→Task→Queue→状态机，再说明每个数字和限制的证据出处。
