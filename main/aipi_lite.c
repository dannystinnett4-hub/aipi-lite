#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "face.h"
#include "behavior.h"
#include "buttons.h"
#include "wifi_manager.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"


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

#define OTA_ACCESS_KEY      "724799"
#define OTA_HTTP_PORT       8080
#define OTA_BUFFER_SIZE     4096

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


static bool ota_request_authorized(httpd_req_t *request)
{
    char supplied_key[96];

    size_t header_length = httpd_req_get_hdr_value_len(
        request,
        "X-OTA-Key"
    );

    if (
        header_length == 0U ||
        header_length >= sizeof(supplied_key)
    )
    {
        return false;
    }

    if (
        httpd_req_get_hdr_value_str(
            request,
            "X-OTA-Key",
            supplied_key,
            sizeof(supplied_key)
        ) != ESP_OK
    )
    {
        return false;
    }

    return strcmp(supplied_key, OTA_ACCESS_KEY) == 0;
}

static esp_err_t ota_status_handler(httpd_req_t *request)
{
    const esp_partition_t *running =
        esp_ota_get_running_partition();

    char response[192];

    snprintf(
        response,
        sizeof(response),
        "{\"ready\":true,\"partition\":\"%s\","
        "\"update_path\":\"/update\"}",
        running != NULL ? running->label : "unknown"
    );

    httpd_resp_set_type(
        request,
        "application/json"
    );

    return httpd_resp_sendstr(
        request,
        response
    );
}

static esp_err_t ota_update_handler(httpd_req_t *request)
{
    if (!ota_request_authorized(request))
    {
        httpd_resp_set_status(
            request,
            "401 Unauthorized"
        );

        return httpd_resp_sendstr(
            request,
            "Missing or invalid X-OTA-Key"
        );
    }

    if (request->content_len <= 0)
    {
        httpd_resp_set_status(
            request,
            "400 Bad Request"
        );

        return httpd_resp_sendstr(
            request,
            "Firmware body is empty"
        );
    }

    const esp_partition_t *update_partition =
        esp_ota_get_next_update_partition(NULL);

    if (update_partition == NULL)
    {
        httpd_resp_set_status(
            request,
            "500 Internal Server Error"
        );

        return httpd_resp_sendstr(
            request,
            "No OTA partition available"
        );
    }

    ESP_LOGI(
        TAG,
        "OTA starting: %d bytes -> %s",
        request->content_len,
        update_partition->label
    );

    face_send_command(
        FACE_COMMAND_ALERT,
        100
    );

    esp_ota_handle_t update_handle;

    esp_err_t result = esp_ota_begin(
        update_partition,
        (size_t)request->content_len,
        &update_handle
    );

    if (result != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "esp_ota_begin failed: %s",
            esp_err_to_name(result)
        );

        httpd_resp_set_status(
            request,
            "500 Internal Server Error"
        );

        return httpd_resp_sendstr(
            request,
            "Could not begin OTA"
        );
    }

    uint8_t *buffer = malloc(OTA_BUFFER_SIZE);

    if (buffer == NULL)
    {
        esp_ota_abort(update_handle);

        httpd_resp_set_status(
            request,
            "500 Internal Server Error"
        );

        return httpd_resp_sendstr(
            request,
            "OTA buffer allocation failed"
        );
    }

    int remaining = request->content_len;
    bool failed = false;

    while (remaining > 0)
    {
        int receive_size =
            remaining > OTA_BUFFER_SIZE
                ? OTA_BUFFER_SIZE
                : remaining;

        int received = httpd_req_recv(
            request,
            (char *)buffer,
            receive_size
        );

        if (received == HTTPD_SOCK_ERR_TIMEOUT)
        {
            continue;
        }

        if (received <= 0)
        {
            failed = true;
            ESP_LOGE(TAG, "OTA receive failed");
            break;
        }

        result = esp_ota_write(
            update_handle,
            buffer,
            (size_t)received
        );

        if (result != ESP_OK)
        {
            failed = true;
            ESP_LOGE(
                TAG,
                "esp_ota_write failed: %s",
                esp_err_to_name(result)
            );
            break;
        }

        remaining -= received;
    }

    free(buffer);

    if (failed)
    {
        esp_ota_abort(update_handle);

        httpd_resp_set_status(
            request,
            "500 Internal Server Error"
        );

        return httpd_resp_sendstr(
            request,
            "OTA transfer failed"
        );
    }

    result = esp_ota_end(update_handle);

    if (result != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "esp_ota_end failed: %s",
            esp_err_to_name(result)
        );

        httpd_resp_set_status(
            request,
            "400 Bad Request"
        );

        return httpd_resp_sendstr(
            request,
            "Firmware image validation failed"
        );
    }

    result = esp_ota_set_boot_partition(
        update_partition
    );

    if (result != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Could not select OTA partition: %s",
            esp_err_to_name(result)
        );

        httpd_resp_set_status(
            request,
            "500 Internal Server Error"
        );

        return httpd_resp_sendstr(
            request,
            "Could not select new firmware"
        );
    }

    httpd_resp_set_type(
        request,
        "application/json"
    );

    httpd_resp_sendstr(
        request,
        "{\"ok\":true,\"rebooting\":true}"
    );

    ESP_LOGI(TAG, "OTA complete; rebooting");
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();

    return ESP_OK;
}

static httpd_handle_t start_ota_server(void)
{
    httpd_config_t config =
        HTTPD_DEFAULT_CONFIG();

    config.server_port = OTA_HTTP_PORT;
    config.stack_size = 8192;
    config.max_uri_handlers = 4;

    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not start OTA server");
        return NULL;
    }

    const httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = ota_status_handler,
        .user_ctx = NULL
    };

    const httpd_uri_t update_uri = {
        .uri = "/update",
        .method = HTTP_POST,
        .handler = ota_update_handler,
        .user_ctx = NULL
    };

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &status_uri
        )
    );

    ESP_ERROR_CHECK(
        httpd_register_uri_handler(
            server,
            &update_uri
        )
    );

    ESP_LOGI(
        TAG,
        "OTA server ready on port %d",
        OTA_HTTP_PORT
    );

    return server;
}

static void start_ota_after_wifi(void)
{
    (void)start_ota_server();
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
