#include "boot_uart.h"
#include "stm32f407xx.h"

void boot_uart_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    GPIOA->MODER = (GPIOA->MODER & ~((3u << 18) | (3u << 20))) |
                   (2u << 18) | (2u << 20);
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~((15u << 4) | (15u << 8))) |
                    (7u << 4) | (7u << 8);
    GPIOA->OSPEEDR |= (3u << 18) | (3u << 20);
    GPIOA->PUPDR = (GPIOA->PUPDR & ~((3u << 18) | (3u << 20))) |
                   (1u << 18) | (1u << 20);
    USART1->CR1 = 0;
    USART1->BRR = (SystemCoreClock + 57600u) / 115200u;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

bool boot_uart_poll(uint8_t *byte)
{
    if (!byte) return false;
    uint32_t sr = USART1->SR;
    if (sr & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) {
        (void)USART1->DR;
        return false;
    }
    if (!(sr & USART_SR_RXNE)) return false;
    *byte = (uint8_t)USART1->DR;
    return true;
}

void boot_uart_send(const uint8_t *bytes, size_t count)
{
    if (!bytes) return;
    for (size_t i = 0; i < count; ++i) {
        while (!(USART1->SR & USART_SR_TXE)) { }
        USART1->DR = bytes[i];
    }
}

void boot_uart_drain(void)
{
    while (!(USART1->SR & USART_SR_TC)) { }
}

void boot_uart_deinit(void)
{
    USART1->CR1 = 0;
}
