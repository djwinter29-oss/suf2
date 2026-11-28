#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "update_request.h"

void app_main(void)
{
    const gpio_config_t config = {.pin_bit_mask = 1ULL << CONFIG_SUF2_BLINK_GPIO, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_LOGI("blink", "External LED on GPIO %d; type u on UART to return to SUF2", CONFIG_SUF2_BLINK_GPIO);
    bool on = false;
    for (;;) {
        ESP_ERROR_CHECK(gpio_set_level(CONFIG_SUF2_BLINK_GPIO, on));
        on = !on;
        if (getchar() == 'u') {
            suf2_esp32_set_update_request();
            esp_restart();
        }
        clearerr(stdin);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
