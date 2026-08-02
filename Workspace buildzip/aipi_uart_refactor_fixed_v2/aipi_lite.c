#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "face.h"
#include "behavior.h"
#include "buttons.h"
#include "wifi_manager.h"
#include "ota_server.h"
#include "uart_console.h"

#include "driver/gpio.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define POWER_CONTROL_PIN  GPIO_NUM_10

static const char *TAG = "AIPI_FACE";















static void initialize_power(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << POWER_CONTROL_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_set_level(POWER_CONTROL_PIN, 1));
}






static void start_ota_after_wifi(void)
{
    ota_server_start();
}

void app_main(void)
{
    initialize_power();
    display_initialize();

    behavior_initialize();

    uart_console_initialize();
    wifi_manager_initialize(start_ota_after_wifi);

    behavior_start();
    buttons_initialize();
    buttons_start();

    ESP_LOGI(
        TAG,
        "Face task, UART, home Wi-Fi, OTA, and front buttons ready"
    );
}
