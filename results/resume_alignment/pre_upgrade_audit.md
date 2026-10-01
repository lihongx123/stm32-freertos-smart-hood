# Pre-upgrade audit — 2026-09-30

Requested repository name: stm32-freertos-smart-hood

Actual absolute path: //wsl.localhost/Ubuntu-24.04/home/hello/projects/smart-hood-baseline/SensorTelemetry

HEAD: 801a64af6c7d96cdc4196191e8b69137ab99cd65

Current RX uses HAL_UART_Receive_IT one byte at a time, ring256 and notification. Five tasks/state machine/19 scenario assertions exist. STM32F1 HAL contains HAL_UARTEx_ReceiveToIdle_DMA and RxEventCallback. Need DMA1 Channel5 circular256, software ring512, IDLE/HT/TC byte-position processing and host regression tests. Keep 8MHz SysTick/TIM1 configuration. Renode exists at /home/hello/tools/renode/renode; DMA/IDLE modeling must be experimentally checked. Untracked docs/INTERVIEW_SOURCE_GUIDE.md preserved.

Raw status/source fingerprints and historical evidence inventory are saved under `20260930T143425Z/pre-state.json`. New results use this dated directory only. No old evidence is to be overwritten. This is a pre-upgrade audit, not a PASS record; new claims start NOT_IMPLEMENTED until source and tests justify another allowed status.
