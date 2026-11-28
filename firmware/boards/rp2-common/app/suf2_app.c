#include "suf2_app.h"

#include "hardware/sync.h"
#include "hardware/watchdog.h"

void suf2_app_request_update(void)
{
    (void)save_and_disable_interrupts();
    watchdog_hw->scratch[0] = SUF2_UPDATE_REQUEST_MAGIC;
    watchdog_hw->scratch[1] = ~SUF2_UPDATE_REQUEST_MAGIC;
    watchdog_reboot(0, 0, 0);
    for (;;) {
        __wfi();
    }
}
