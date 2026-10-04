#ifndef HOOD_APP_H
#define HOOD_APP_H
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "app_types.h"
#include "uart_ring.h"
#include "sensor_acquisition.h"
enum { TASK_COMM, TASK_SENSOR, TASK_CONTROL, TASK_FAULT, TASK_MONITOR, TASK_COUNT };
typedef struct {
    SensorData latest;
    HoodState output;
    uint32_t last_frame, invalid, frame_ok, frame_error, queue_drop;
    uint32_t queue_fault_tick, faults, health_mask;
    TickType_t heartbeat[TASK_COUNT], stall_until[TASK_COUNT];
} AppRuntime;
extern AppRuntime app_runtime;
extern UartRing app_ring;
extern SemaphoreHandle_t app_mutex;
extern QueueHandle_t sensor_queue;
extern TaskHandle_t app_tasks[TASK_COUNT];
void hood_app_init(void);
void app_log(const char *format, ...);
void app_beat(unsigned task);
void CommTask(void *argument);
void SensorTask(void *argument);
/* Configure before starting the scheduler; UART remains the default simulator. */
void sensor_task_set_backend(SensorBackend backend);
void ControlTask(void *argument);
void FaultTask(void *argument);
void MonitorTask(void *argument);
int sensor_frame_parse(const char *frame, SensorData *value);
void hood_step(HoodState *state, const SensorData *data, uint32_t faults);
#endif
