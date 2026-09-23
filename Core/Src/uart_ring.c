#include "uart_ring.h"
/* Single ISR producer, single task consumer. One slot distinguishes full/empty. */
int uart_ring_push_isr(UartRing *ring, uint8_t byte)
{
    uint16_t next = (ring->head + 1U) % UART_RING_CAPACITY;
    ++ring->bytes_received;
    if (next == ring->tail) { ++ring->overflow; return 0; }
    ring->data[ring->head] = byte;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    ring->head = next;
    return 1;
}
int uart_ring_pop(UartRing *ring, uint8_t *byte)
{
    if (ring->tail == ring->head) return 0;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    *byte = ring->data[ring->tail];
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    ring->tail = (ring->tail + 1U) % UART_RING_CAPACITY;
    return 1;
}
