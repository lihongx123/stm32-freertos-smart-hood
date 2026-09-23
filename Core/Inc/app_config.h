#ifndef APP_CONFIG_H
#define APP_CONFIG_H
#define UART_RING_CAPACITY 256U
#define APP_FRAME_CAPACITY 96U
#define APP_PERIOD_MS 100U
#define APP_HEALTH_PERIOD_MS 250U
#define APP_INPUT_TIMEOUT_MS 2000U
#define APP_TASK_TIMEOUT_MS 1500U
#define APP_RECOVERY_CYCLES 10U
#define APP_QUEUE_LENGTH 4U
#define APP_SMOKE_LOW 100
#define APP_SMOKE_MEDIUM 300
#define APP_SMOKE_HIGH 600
#define APP_SMOKE_BOOST 850
#define APP_BACKFLOW_DP -10
#define APP_DARK_LIGHT 30
/* Explicit simulation-only fault injection, not a production control protocol. */
#define APP_SIMULATION_TEST_HOOKS 1
#endif
