#include "uart_ring.h"
#include "app_types.h"
#include <assert.h>
#include <stdio.h>
void hood_step(HoodState *, const SensorData *, uint32_t);
int main(void)
{
    UartRing ring={0};
    uint8_t value;
    assert(!uart_ring_pop(&ring,&value));
    for (unsigned i=0;i<UART_RING_CAPACITY-1;++i) assert(uart_ring_push_isr(&ring,(uint8_t)i));
    assert(!uart_ring_push_isr(&ring,0));
    assert(ring.overflow==1 && ring.bytes_received==UART_RING_CAPACITY);
    for (unsigned i=0;i<UART_RING_CAPACITY-1;++i) {
        assert(uart_ring_pop(&ring,&value)); assert(value==(uint8_t)i);
    }
    for (unsigned i=0;i<1000;++i) {
        assert(uart_ring_push_isr(&ring,(uint8_t)i));
        assert(uart_ring_pop(&ring,&value)); assert(value==(uint8_t)i);
    }
    HoodState state={0}; SensorData data={.valid=1,.light=100};
    hood_step(&state,&data,0); assert(state.state==SYS_NORMAL && state.fan==FAN_OFF);
    data.smoke=APP_SMOKE_MEDIUM; hood_step(&state,&data,0); assert(state.fan==FAN_MEDIUM);
    data.smoke=APP_SMOKE_HIGH; hood_step(&state,&data,0); assert(state.fan==FAN_HIGH);
    data.differential_pressure=APP_BACKFLOW_DP; hood_step(&state,&data,0);
    assert(state.state==SYS_WARNING && state.fan==FAN_BOOST);
    hood_step(&state,&data,FAULT_INVALID); assert(state.state==SYS_FAULT && state.faults==1);
    data.differential_pressure=0; hood_step(&state,&data,0); assert(state.state==SYS_RECOVERY);
    for (unsigned i=1;i<APP_RECOVERY_CYCLES;++i) { hood_step(&state,&data,0); assert(state.state==SYS_RECOVERY); }
    hood_step(&state,&data,0); assert(state.state==SYS_NORMAL);
    data.light=0; hood_step(&state,&data,0); assert(state.light_on);
    puts("PASS ring full/empty/FIFO/wraparound and thresholds/fault/recovery/light");
    return 0;
}
