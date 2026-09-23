#ifndef UART_RING_H
#define UART_RING_H
#include <stdint.h>
#include "app_config.h"
typedef struct {
    uint8_t data[UART_RING_CAPACITY];
    volatile uint16_t head, tail;
    volatile uint32_t bytes_received, overflow;
} UartRing;
int uart_ring_push_isr(UartRing *ring, uint8_t byte);
int uart_ring_pop(UartRing *ring, uint8_t *byte);
#endif
