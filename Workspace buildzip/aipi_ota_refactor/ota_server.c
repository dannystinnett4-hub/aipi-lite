#include "ota_server.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "behavior.h"

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OTA_ACCESS_KEY      "724799"
#define OTA_HTTP_PORT       8080
#define OTA_BUFFER_SIZE     4096

static const char *TAG = "AIPI_OTA";

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

static httpd_handle_t start_ota_server_internal(void)
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


void ota_server_start(void)
{
    (void)start_ota_server_internal();
}
