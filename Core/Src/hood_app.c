#include "hood_app.h"
#include "main.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
extern UART_HandleTypeDef huart1;
AppRuntime app_runtime;
UartRing app_ring;
SemaphoreHandle_t app_mutex;
QueueHandle_t sensor_queue;
TaskHandle_t app_tasks[TASK_COUNT];
static SemaphoreHandle_t tx_mutex;
void app_log(const char *format, ...)
{
    char line[192];
    va_list args;
    va_start(args,format);
    vsnprintf(line,sizeof(line)-3,format,args);
    va_end(args);
    size_t length=strlen(line);
    line[length++]='\r'; line[length++]='\n';
    xSemaphoreTake(tx_mutex,portMAX_DELAY);
    HAL_UART_Transmit(&huart1,(uint8_t *)line,(uint16_t)length,100);
    xSemaphoreGive(tx_mutex);
}
void app_beat(unsigned task)
{
    xSemaphoreTake(app_mutex,portMAX_DELAY);
    TickType_t now=xTaskGetTickCount(), until=app_runtime.stall_until[task];
    app_runtime.heartbeat[task]=now;
    xSemaphoreGive(app_mutex);
    if ((int32_t)(until-now)>0) vTaskDelay(until-now);
}
void hood_app_init(void)
{
    app_mutex=xSemaphoreCreateMutex();
    tx_mutex=xSemaphoreCreateMutex();
    sensor_queue=xQueueCreate(APP_QUEUE_LENGTH,sizeof(SensorData));
    configASSERT(app_mutex && tx_mutex && sensor_queue);
    configASSERT(xTaskCreate(CommTask,"CommTask",384,NULL,4,&app_tasks[TASK_COMM])==pdPASS);
    configASSERT(xTaskCreate(SensorTask,"SensorTask",256,NULL,3,&app_tasks[TASK_SENSOR])==pdPASS);
    configASSERT(xTaskCreate(ControlTask,"ControlTask",384,NULL,3,&app_tasks[TASK_CONTROL])==pdPASS);
    configASSERT(xTaskCreate(FaultTask,"FaultTask",256,NULL,2,&app_tasks[TASK_FAULT])==pdPASS);
}
