#include "boot_image.h"
#include "boot_lifecycle.h"
#include "boot_flash_stm32.h"
#include "boot_uart.h"
#include "stm32f4xx_hal.h"

enum {
    BOOT_STATUS_START = 1u,
    BOOT_STATUS_NO_METADATA = 2u,
    BOOT_STATUS_BAD_IMAGE = 3u,
    BOOT_STATUS_JUMPING = 5u,
    BOOT_STATUS_UPDATE_FAILED = 6u,
    BOOT_UART_WINDOW_MS = 500u
};

volatile uint32_t boot_status;
static BootLifecycle lifecycle;

void SysTick_Handler(void)
{
    HAL_IncTick();
}

__attribute__((noreturn)) static void recovery_loop(uint32_t status)
{
    boot_status = status;
    for (;;) __WFI();
}

__attribute__((noreturn)) static void jump_to_application(void)
{
    const uint32_t *vectors = (const uint32_t *)BOOT_APP_BASE;
    const uint32_t stack = vectors[0];
    const uint32_t entry = vectors[1];
    boot_uart_deinit();
    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    for (unsigned i = 0; i < 8; ++i) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }
    SCB->VTOR = BOOT_APP_BASE;
    __DSB();
    __ISB();
    boot_status = BOOT_STATUS_JUMPING;
    __enable_irq();
    __asm volatile ("msr msp, %0\n bx %1" :: "r"(stack), "r"(entry) : "memory");
    __builtin_unreachable();
}

static void recovery_uart_window(void)
{
    BootWireParser parser = {0};
    BootWireFrame frame;
    const uint32_t started = HAL_GetTick();
    uint32_t last_frame = started;
    bool engaged = false;
    while (HAL_GetTick() - started < BOOT_UART_WINDOW_MS ||
           (engaged && HAL_GetTick() - last_frame <= BOOT_UPDATE_TIMEOUT_MS)) {
        uint8_t byte;
        if (!boot_uart_poll(&byte)) continue;
        BootParseResult parsed = boot_wire_feed(&parser, byte, &frame);
        if (parsed != BOOT_PARSE_FRAME) continue;
        engaged = true;
        last_frame = HAL_GetTick();
        BootUpdateResult result = boot_lifecycle_receive(&lifecycle, &frame,
                                                          last_frame);
        uint8_t reply[BOOT_WIRE_MAX_BYTES];
        size_t count = boot_update_response(frame.type, frame.sequence,
                                             result, reply, sizeof(reply));
        boot_uart_send(reply, count);
        if (frame.type != BOOT_FRAME_END || result != BOOT_UPDATE_OK)
            continue;
        boot_flash_error_code = boot_flash_error_address = 0;
        BootLifecycleResult activated = boot_lifecycle_activate(&lifecycle);
        BootWireFrame status = {0};
        status.type = BOOT_FRAME_STATUS;
        status.sequence = frame.sequence;
        status.length = 7;
        status.payload[0] = (uint8_t)activated;
        status.payload[1] = (uint8_t)lifecycle.last_step;
        status.payload[2] = (uint8_t)boot_flash_error_code;
        for (unsigned i = 0; i < 4; ++i)
            status.payload[3 + i] =
                (uint8_t)(boot_flash_error_address >> (8u * i));
        count = boot_wire_encode(&status, reply, sizeof(reply));
        boot_uart_send(reply, count);
        boot_uart_drain();
        boot_status = activated == BOOT_LIFECYCLE_OK ?
            BOOT_STATUS_JUMPING : BOOT_STATUS_UPDATE_FAILED;
        NVIC_SystemReset();
    }
}

int main(void)
{
    boot_status = BOOT_STATUS_START;
    if (HAL_Init() != HAL_OK || !boot_flash_layout_valid())
        recovery_loop(BOOT_STATUS_UPDATE_FAILED);
    boot_uart_init();
    boot_lifecycle_init(&lifecycle, boot_flash_stm32_backend());
    if (!lifecycle.has_record)
        recovery_loop(BOOT_STATUS_NO_METADATA);
    if (boot_lifecycle_boot(&lifecycle) != BOOT_ACTION_BOOT_ACTIVE)
        recovery_loop(BOOT_STATUS_BAD_IMAGE);
    /* Pending images must boot without opening another update session. */
    if (lifecycle.latest.state == BOOT_STATE_CONFIRMED)
        recovery_uart_window();
    BootRecord *latest = &lifecycle.latest;
    BootImageHeader image = {BOOT_IMAGE_MAGIC, BOOT_IMAGE_FORMAT,
                             latest->active_version, latest->active_bytes,
                             latest->active_crc, 0};
    image.header_crc32 = boot_header_crc32(&image);
    if (boot_image_validate(&image, (const uint8_t *)BOOT_APP_BASE,
                            BOOT_APP_SLOT_BYTES) != BOOT_IMAGE_OK)
        recovery_loop(BOOT_STATUS_BAD_IMAGE);
    jump_to_application();
}
