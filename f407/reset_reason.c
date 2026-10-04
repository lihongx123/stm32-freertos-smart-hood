#include "reset_reason.h"

uint32_t reset_reason_decode_f407(uint32_t csr)
{
    uint32_t result = 0;
    if (csr & (1u << 25)) result |= RESET_REASON_BROWNOUT;
    if (csr & (1u << 26)) result |= RESET_REASON_EXTERNAL;
    if (csr & (1u << 27)) result |= RESET_REASON_POWER_ON;
    if (csr & (1u << 28)) result |= RESET_REASON_SOFTWARE;
    if (csr & (1u << 29)) result |= RESET_REASON_IWDG;
    if (csr & (1u << 30)) result |= RESET_REASON_WWDG;
    if (csr & (1u << 31)) result |= RESET_REASON_LOW_POWER;
    return result;
}
