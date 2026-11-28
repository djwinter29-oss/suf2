/*
 * Example application for the SUF2 loader: blinks the LED and, over USB
 * serial, reboots into update mode when you type 'u'.
 */
#include <stdio.h>

#include "pico/stdlib.h"
#include "suf2_app.h"

int main(void)
{
    stdio_init_all();
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif
    bool on = false;
    for (;;) {
#ifdef PICO_DEFAULT_LED_PIN
        gpio_put(PICO_DEFAULT_LED_PIN, on = !on);
#endif
        int c = getchar_timeout_us(250 * 1000);
        if (c == 'u' || c == 'U') {
            printf("Rebooting into SUF2 update mode\n");
            stdio_flush();
            sleep_ms(50);
            suf2_app_request_update();
        }
        if (c != PICO_ERROR_TIMEOUT) printf("SUF2 blink example: type 'u' to update\n");
    }
}
