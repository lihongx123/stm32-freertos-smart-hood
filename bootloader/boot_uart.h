#ifndef BOOT_UART_H
#define BOOT_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void boot_uart_init(void);
bool boot_uart_poll(uint8_t *byte);
void boot_uart_send(const uint8_t *bytes, size_t count);
void boot_uart_drain(void);
void boot_uart_deinit(void);

#endif
