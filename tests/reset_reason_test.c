#include "reset_reason.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(reset_reason_decode_f407(0) == 0);
    assert(reset_reason_decode_f407(1u << 27) == RESET_REASON_POWER_ON);
    assert(reset_reason_decode_f407(1u << 29) == RESET_REASON_IWDG);
    assert(reset_reason_decode_f407((1u << 27) | (1u << 26)) ==
           (RESET_REASON_POWER_ON | RESET_REASON_EXTERNAL));
    assert(reset_reason_decode_f407(0xfe000000u) == 0x7fu);
    assert(reset_reason_decode_f407(1u << 24) == 0);
    puts("PASS F407 RCC reset-flag decoding and multi-flag preservation");
    return 0;
}
