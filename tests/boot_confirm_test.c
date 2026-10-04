#include "boot_confirm.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(!f407_boot_should_confirm(0, 0, 0));
    assert(!f407_boot_should_confirm(4999, 0, 0));
    assert(!f407_boot_should_confirm(5000, 1, 0));
    assert(!f407_boot_should_confirm(5000, 0, 1));
    assert(f407_boot_should_confirm(5000, 0, 0));
    assert(f407_boot_should_confirm(10000, 0, 0));
    puts("PASS pending-image health window: >=5000 ms, all tasks alive, motor healthy");
    return 0;
}
