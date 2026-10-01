#ifndef UART_DMA_RX_H
#define UART_DMA_RX_H
#include "uart_ring.h"
typedef struct {
    uint16_t position;
    uint32_t windows, bytes;
} UartDmaCursor;
/* position is the live DMA write offset in [0, capacity]; same-position events
 * contain no new data. HT/TC must be serviced before a complete unseen lap. */
unsigned uart_dma_drain(UartDmaCursor *cursor, const uint8_t *dma,
                        uint16_t capacity, uint16_t position, UartRing *ring);
#endif
