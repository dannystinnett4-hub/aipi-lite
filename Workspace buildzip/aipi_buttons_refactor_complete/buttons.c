#include "buttons.h"

#include <stdlib.h>

#include "behavior.h"

#include "driver/gpio.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RESET_BUTTON_PIN          GPIO_NUM_1
#define ACTION_BUTTON_PIN         GPIO_NUM_42
#define BUTTON_POLL_INTERVAL_MS   20
#define BUTTON_DEBOUNCE_MS        50

static const char *TAG = "AIPI_BUTTONS";

static void front_button_task(void *parameter)
{
    (void)parameter;

    int reset_stable = gpio_get_level(RESET_BUTTON_PIN);
    int reset_sampled = reset_stable;
    TickType_t reset_changed_at = 0;

    int action_stable = gpio_get_level(ACTION_BUTTON_PIN);
    int action_sampled = action_stable;
    TickType_t action_changed_at = 0;

    while (true)
    {
        TickType_t now = xTaskGetTickCount();

        int reset_level = gpio_get_level(RESET_BUTTON_PIN);

        if (reset_level != reset_sampled)
        {
            reset_sampled = reset_level;
            reset_changed_at = now;
        }
        else if (
            reset_level != reset_stable &&
            (now - reset_changed_at) >=
                pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)
        )
        {
            reset_stable = reset_level;

            if (reset_stable == 0)
            {
                ESP_LOGW(
                    TAG,
                    "Reset button pressed; restarting"
                );

                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }
        }

        int action_level = gpio_get_level(ACTION_BUTTON_PIN);

        if (action_level != action_sampled)
        {
            action_sampled = action_level;
            action_changed_at = now;
        }
        else if (
            action_level != action_stable &&
            (now - action_changed_at) >=
                pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)
        )
        {
            action_stable = action_level;

            if (action_stable == 0)
            {
                ESP_LOGI(TAG, "Action button pressed");

                if (!face_send_command(
                        FACE_COMMAND_LISTENING,
                        100
                    ))
                {
                    ESP_LOGW(
                        TAG,
                        "Could not queue action-button command"
                    );
                }
            }
        }

        vTaskDelay(
            pdMS_TO_TICKS(BUTTON_POLL_INTERVAL_MS)
        );
    }
}

void buttons_initialize(void)
{
    const gpio_config_t config = {
        .pin_bit_mask =
            (1ULL << RESET_BUTTON_PIN) |
            (1ULL << ACTION_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&config));

    ESP_LOGI(
        TAG,
        "Front buttons ready: reset=GPIO%d action=GPIO%d",
        (int)RESET_BUTTON_PIN,
        (int)ACTION_BUTTON_PIN
    );
}

void buttons_start(void)
{
    BaseType_t task_created = xTaskCreate(
        front_button_task,
        "front_buttons",
        3072,
        NULL,
        4,
        NULL
    );

    if (task_created != pdPASS)
    {
        ESP_LOGE(
            TAG,
            "Front-button task creation failed"
        );
        abort();
    }
}
