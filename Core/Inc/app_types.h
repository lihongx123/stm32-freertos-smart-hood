#ifndef APP_TYPES_H
#define APP_TYPES_H
#include <stdint.h>
typedef enum { FAN_OFF, FAN_LOW, FAN_MEDIUM, FAN_HIGH, FAN_BOOST } FanMode;
typedef enum { SYS_INIT, SYS_NORMAL, SYS_WARNING, SYS_FAULT, SYS_RECOVERY } SystemState;
typedef struct {
    int smoke, temperature, humidity, light, differential_pressure;
    uint32_t tick;
    uint8_t valid;
} SensorData;
enum { FAULT_SENSOR_TIMEOUT=1, FAULT_INVALID=2, FAULT_COMM_TIMEOUT=4,
       FAULT_QUEUE=8, FAULT_TASK=16, FAULT_MOTOR_STALL=32,
       FAULT_HALL_LOSS=64 };
typedef struct {
    SystemState state;
    FanMode fan;
    uint8_t light_on;
    uint32_t recovery_cycles, transitions, faults;
} HoodState;
#endif
