# 架构升级后验收

2026-10-01追加：DMA全链路已在项目内修补Renode模型下通过，
状态VERIFIED（仿真）。证据 dma-diagnosis-20261001/verified-enable/
及 dma-diagnosis-20261001/dma-scenarios/，264字节逐段对账、19/19场景。
固件ELF与首次失败版本散列完全相同。详情 ../../docs/dma_renode_diagnosis.md。
以下是首次验收记录，保留追溯。

更新 2026-10-01。运行目录 20260930T143425Z/。
用户最新要求：本地短验证，排除云端，不追加长时间benchmark。

默认 DMA 架构已实现和编译，真实 DMA 全链路状态为 IMPLEMENTED_NOT_FULLY_VALIDATED。
- dma-host.log：位置/回绕/重复事件/满缓冲/63112字节顺序对账通过。
- state-ring-final.log：原状态策略与ring单测通过。
- dma-renode-01：保留默认DMA固件首次模拟失败，idle NORMAL未通过。
- irq-functional-01：显式APP_UART_RX_IT_FALLBACK=1功能仿真19/19，不能替代DMA验证。
- stm32-final-resources.json：每任务高水位、4176字节最低空闲heap、两种固件大小。
- 完整架构和限制见 ../../docs/resume_architecture.md。
- git diff --check通过；历史54个结果文件大小/mtime不变。

所有相对测试路径均位于 20260930T143425Z/。
最终源码散列、修改文件及git状态见 20260930T143425Z/final-state.json。
