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

#include "driver/gpio.h"
#include "driver/uart.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define POWER_CONTROL_PIN  GPIO_NUM_10

/*
 * External command UART.
 * Change these two pins if GPIO 4/5 are used elsewhere on your board.
 */
#define COMMAND_UART_PORT   UART_NUM_1
#define COMMAND_UART_TX_PIN GPIO_NUM_4
#define COMMAND_UART_RX_PIN GPIO_NUM_5
#define COMMAND_UART_BAUD   115200

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






static bool command_from_text(
    const char *text,
    face_command_t *command
)
{
    if (strcmp(text, "AUTO") == 0)
    {
        *command = FACE_COMMAND_AUTONOMOUS;
    }
    else if (strcmp(text, "IDLE") == 0)
    {
        *command = FACE_COMMAND_IDLE;
    }
    else if (strcmp(text, "CURIOUS") == 0)
    {
        *command = FACE_COMMAND_CURIOUS;
    }
    else if (
        strcmp(text, "THINK") == 0 ||
        strcmp(text, "THINKING") == 0
    )
    {
        *command = FACE_COMMAND_THINKING;
    }
    else if (
        strcmp(text, "LISTEN") == 0 ||
        strcmp(text, "LISTENING") == 0
    )
    {
        *command = FACE_COMMAND_LISTENING;
    }
    else if (strcmp(text, "HAPPY") == 0)
    {
        *command = FACE_COMMAND_HAPPY;
    }
    else if (
        strcmp(text, "SLEEP") == 0 ||
        strcmp(text, "SLEEPY") == 0
    )
    {
        *command = FACE_COMMAND_SLEEPY;
    }
    else if (
        strcmp(text, "SPEAK") == 0 ||
        strcmp(text, "SPEAKING") == 0
    )
    {
        *command = FACE_COMMAND_SPEAKING;
    }
    else if (strcmp(text, "ALERT") == 0)
    {
        *command = FACE_COMMAND_ALERT;
    }
    else
    {
        return false;
    }

    return true;
}

static void normalize_command(char *text)
{
    size_t write_index = 0;

    for (size_t read_index = 0;
         text[read_index] != '\0';
         ++read_index)
    {
        char character = text[read_index];

        if (
            character == '\r' ||
            character == '\n' ||
            character == ' ' ||
            character == '\t'
        )
        {
            continue;
        }

        if (character >= 'a' && character <= 'z')
        {
            character =
                (char)(character - 'a' + 'A');
        }

        text[write_index++] = character;
    }

    text[write_index] = '\0';
}

static void command_uart_write(const char *message)
{
    uart_write_bytes(
        COMMAND_UART_PORT,
        message,
        strlen(message)
    );
}

static void command_uart_task(void *parameter)
{
    (void)parameter;

    uint8_t received_byte;
    char command_buffer[32];
    size_t command_length = 0;

    command_uart_write(
        "\r\nAIPI face UART ready\r\n"
        "Commands: AUTO IDLE CURIOUS THINK LISTEN "
        "HAPPY SLEEP SPEAK ALERT\r\n> "
    );

    while (true)
    {
        int bytes_read = uart_read_bytes(
            COMMAND_UART_PORT,
            &received_byte,
            1,
            pdMS_TO_TICKS(100)
        );

        if (bytes_read <= 0)
        {
            continue;
        }

        if (
            received_byte == '\r' ||
            received_byte == '\n'
        )
        {
            if (command_length == 0)
            {
                continue;
            }

            command_buffer[command_length] = '\0';
            normalize_command(command_buffer);

            face_command_t command;

            if (
                command_from_text(
                    command_buffer,
                    &command
                )
            )
            {
                if (face_send_command(command, 100))
                {
                    command_uart_write("OK\r\n> ");
                }
                else
                {
                    command_uart_write(
                        "ERROR queue full\r\n> "
                    );
                }
            }
            else
            {
                command_uart_write(
                    "ERROR unknown command\r\n> "
                );
            }

            command_length = 0;
            continue;
        }

        if (
            received_byte == 8U ||
            received_byte == 127U
        )
        {
            if (command_length > 0)
            {
                --command_length;
            }

            continue;
        }

        if (
            command_length <
            (sizeof(command_buffer) - 1U)
        )
        {
            command_buffer[command_length++] =
                (char)received_byte;
        }
        else
        {
            command_length = 0;
            command_uart_write(
                "ERROR command too long\r\n> "
            );
        }
    }
}

static void initialize_command_uart(void)
{
    const uart_config_t uart_config = {
        .baud_rate = COMMAND_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    ESP_ERROR_CHECK(
        uart_driver_install(
            COMMAND_UART_PORT,
            256,
            256,
            0,
            NULL,
            0
        )
    );

    ESP_ERROR_CHECK(
        uart_param_config(
            COMMAND_UART_PORT,
            &uart_config
        )
    );

    ESP_ERROR_CHECK(
        uart_set_pin(
            COMMAND_UART_PORT,
            COMMAND_UART_TX_PIN,
            COMMAND_UART_RX_PIN,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE
        )
    );

    BaseType_t task_created = xTaskCreate(
        command_uart_task,
        "command_uart",
        4096,
        NULL,
        4,
        NULL
    );

    if (task_created != pdPASS)
    {
        ESP_LOGE(TAG, "UART command task creation failed");
        abort();
    }
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

    initialize_command_uart();
    wifi_manager_initialize(start_ota_after_wifi);

    behavior_start();
    buttons_initialize();
    buttons_start();

    ESP_LOGI(
        TAG,
        "Face task, UART, home Wi-Fi, OTA, and front buttons ready"
    );
}
