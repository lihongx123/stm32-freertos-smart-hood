#include "hood_app.h"
#include "main.h"
#include "uart_dma_rx.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#ifdef STM32F407xx
#include "motor_sim_task.h"
#endif
extern UART_HandleTypeDef huart1;
#if APP_UART_RX_IT_FALLBACK
static uint8_t rx_byte;
#else
static uint8_t dma_rx[UART_DMA_RX_CAPACITY];
static UartDmaCursor dma_cursor;
#endif
static volatile unsigned rx_recovery;
volatile uint32_t app_dma_events, app_dma_bytes, app_uart_errors;
int sensor_frame_parse(const char *frame, SensorData *data)
{
    long values[5];
    const char *p = frame;
    if (p[0] != 'S' || p[1] != ',') return 0;
    p += 2;
    for (unsigned i=0; i<5; ++i) {
        char *end;
        if (*p != '-' && (*p < '0' || *p > '9')) return 0;
        errno = 0;
        values[i] = strtol(p, &end, 10);
        if (end == p || errno == ERANGE || (i<4 ? *end!=',' : *end!='\0')) return 0;
        p = end + (i<4);
    }
    if (values[0]<0 || values[0]>1000 || values[1]<-40 || values[1]>125 ||
        values[2]<0 || values[2]>100 || values[3]<0 || values[3]>1000 ||
        values[4]<-1000 || values[4]>1000) return 0;
    data->smoke=values[0]; data->temperature=values[1]; data->humidity=values[2];
    data->light=values[3]; data->differential_pressure=values[4]; data->valid=1;
    return 1;
}
#if APP_UART_RX_IT_FALLBACK
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance != USART1) return;
    BaseType_t wake = pdFALSE;
    uart_ring_push_isr(&app_ring, rx_byte);
    HAL_UART_Receive_IT(uart, &rx_byte, 1);
    vTaskNotifyGiveFromISR(app_tasks[TASK_COMM], &wake);
    portYIELD_FROM_ISR(wake);
}
#else
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size)
{
    (void)size;
    if (uart->Instance != USART1 || !app_tasks[TASK_COMM]) return;
    /* Sample NDTR, not an old HT callback's fixed Size. USART/DMA IRQs have
     * equal preemption priority, so this SPSC producer cannot nest itself. */
    uint16_t position = UART_DMA_RX_CAPACITY - __HAL_DMA_GET_COUNTER(uart->hdmarx);
    if (!position && HAL_UARTEx_GetRxEventType(uart) == HAL_UART_RXEVENT_TC)
        position = UART_DMA_RX_CAPACITY;
    ++app_dma_events;
    unsigned count = uart_dma_drain(&dma_cursor, dma_rx, UART_DMA_RX_CAPACITY,
                                    position, &app_ring);
    app_dma_bytes += count;
    if (count) {
        BaseType_t wake = pdFALSE;
        vTaskNotifyGiveFromISR(app_tasks[TASK_COMM], &wake);
        portYIELD_FROM_ISR(wake);
    }
}
#endif
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance != USART1 || !app_tasks[TASK_COMM]) return;
    ++app_uart_errors;
    rx_recovery = 1;
    BaseType_t wake = pdFALSE;
    vTaskNotifyGiveFromISR(app_tasks[TASK_COMM], &wake);
    portYIELD_FROM_ISR(wake);
}
static HAL_StatusTypeDef start_receive(void)
{
#if APP_UART_RX_IT_FALLBACK
    return HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
#else
    dma_cursor.position = 0;
    return HAL_UARTEx_ReceiveToIdle_DMA(&huart1, dma_rx, sizeof(dma_rx));
#endif
}
void CommTask(void *argument)
{
    (void)argument;
    char frame[APP_FRAME_CAPACITY];
    unsigned length=0, discard=0;
    uint32_t overflow=0;
    if (start_receive() != HAL_OK) Error_Handler();
    app_log("COMM_READY");
    for (;;) {
        app_beat(TASK_COMM);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_PERIOD_MS));
        if (rx_recovery) {
            /* Abort/restart and frame resynchronization happen in task context. */
            taskENTER_CRITICAL();
            HAL_UART_AbortReceive(&huart1);
            rx_recovery = 0;
            app_ring.tail = app_ring.head;
            HAL_StatusTypeDef result = start_receive();
            taskEXIT_CRITICAL();
            discard = 1; length = 0;
            if (result != HAL_OK) rx_recovery = 1;
        }
        uint8_t byte;
        while (uart_ring_pop(&app_ring, &byte)) {
            if (overflow != app_ring.overflow) { overflow=app_ring.overflow; discard=1; length=0; }
            if (byte=='\r') continue;
            if (byte!='\n') {
                if (byte<32 || byte>126) { discard=1; continue; }
                if (!discard && length+1<sizeof(frame)) frame[length++]=(char)byte;
                else discard=1;
                continue;
            }
            frame[length]=0;
#if APP_SIMULATION_TEST_HOOKS
            unsigned task, ms; int consumed=0;
            if (!discard && sscanf(frame,"T,%u,%u%n",&task,&ms,&consumed)==2 &&
                frame[consumed]==0 && task<TASK_MONITOR && ms<=5000) {
                xSemaphoreTake(app_mutex,portMAX_DELAY);
                app_runtime.stall_until[task]=xTaskGetTickCount()+pdMS_TO_TICKS(ms);
                xSemaphoreGive(app_mutex);
                app_log("TEST_STALL,task=%u,ms=%u",task,ms);
                length=0; app_beat(TASK_COMM); continue;
            }
#ifdef STM32F407xx
            unsigned enabled;
            consumed = 0;
            if (!discard && sscanf(frame,"M,STALL,%u%n",&enabled,&consumed)==1 &&
                frame[consumed]==0 && enabled<=1) {
                motor_sim_set_fault(MOTOR_SIM_STALL, enabled != 0);
                app_log("TEST_MOTOR_STALL,%u",enabled);
                length=0; continue;
            }
            consumed = 0;
            if (!discard && sscanf(frame,"M,HALL_LOSS,%u%n",&enabled,&consumed)==1 &&
                frame[consumed]==0 && enabled<=1) {
                motor_sim_set_fault(MOTOR_SIM_HALL_LOSS, enabled != 0);
                app_log("TEST_HALL_LOSS,%u",enabled);
                length=0; continue;
            }
#endif
#endif
            SensorData data={0};
            const int valid=!discard && sensor_frame_parse(frame,&data);
            xSemaphoreTake(app_mutex,portMAX_DELAY);
            app_runtime.last_frame=xTaskGetTickCount();
            if (valid) {
                data.tick=xTaskGetTickCount(); app_runtime.latest=data;
                app_runtime.invalid=0; ++app_runtime.frame_ok;
            } else { app_runtime.invalid=1; ++app_runtime.frame_error; }
            xSemaphoreGive(app_mutex);
            length=0; discard=0;
        }
    }
}
