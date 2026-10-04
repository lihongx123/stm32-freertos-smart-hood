#include "main.h"
#include "hood_app.h"
#include "motor_sim_task.h"
#include "reset_reason.h"
#include <stdio.h>

void board_f407_init(void);
extern UART_HandleTypeDef huart1;

int main(void)
{
    HAL_Init();
    board_f407_init();
    HAL_UART_Transmit(&huart1, (uint8_t *)"BOOT\r\n", 6, 100);
    char reset_line[64];
    int reset_len = snprintf(reset_line, sizeof(reset_line),
                             "RESET,raw=0x%08lX,flags=%lu\r\n",
                             (unsigned long)board_f407_reset_csr(),
                             (unsigned long)reset_reason_decode_f407(
                                 board_f407_reset_csr()));
    if (reset_len > 0 && reset_len < (int)sizeof(reset_line))
        HAL_UART_Transmit(&huart1, (uint8_t *)reset_line,
                          (uint16_t)reset_len, 100);
    hood_app_init();
    if (xTaskCreate(MotorSimTask, "MotorSim", 384, NULL, 3, NULL) != pdPASS)
        Error_Handler();
    if (xTaskCreate(MonitorTask, "MonitorTask", 512, NULL, 1,
                    &app_tasks[TASK_MONITOR]) != pdPASS)
        Error_Handler();
    HAL_UART_Transmit(&huart1, (uint8_t *)"RTOS_START\r\n", 12, 100);
    vTaskStartScheduler();
    Error_Handler();
}

void Error_Handler(void)
{
    __disable_irq();
    for (;;) { }
}
