#include "uart_dma_rx.h"
unsigned uart_dma_drain(UartDmaCursor *cursor, const uint8_t *dma,
                        uint16_t capacity, uint16_t position, UartRing *ring)
{
    if (!capacity || position > capacity || cursor->position > capacity) return 0;
    unsigned count = 0;
    uint16_t previous = cursor->position;
    if (position == previous) return 0;
    if (position < previous) {
        while (previous < capacity) {
            uart_ring_push_isr(ring, dma[previous++]); ++count;
        }
        previous = 0;
    }
    while (previous < position) {
        uart_ring_push_isr(ring, dma[previous++]); ++count;
    }
    cursor->position = position;
    cursor->bytes += count;
    if (count) ++cursor->windows;
    return count;
}
