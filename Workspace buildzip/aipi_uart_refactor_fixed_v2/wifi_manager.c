#include "wifi_manager.h"

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_STA_SSID       "The Promise LAN"
#define WIFI_STA_PASSWORD   "Cheese211!?"

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_MAXIMUM_RETRY  10

static const char *TAG = "AIPI_WIFI";

static EventGroupHandle_t wifi_event_group;
static int wifi_retry_count = 0;

static void wifi_event_handler(
    void *argument,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    (void)argument;

    if (
        event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START
    )
    {
        esp_wifi_connect();
    }
    else if (
        event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_DISCONNECTED
    )
    {
        if (wifi_retry_count < WIFI_MAXIMUM_RETRY)
        {
            ++wifi_retry_count;

            ESP_LOGW(
                TAG,
                "Wi-Fi reconnect %d/%d",
                wifi_retry_count,
                WIFI_MAXIMUM_RETRY
            );

            esp_wifi_connect();
        }
        else
        {
            xEventGroupSetBits(
                wifi_event_group,
                WIFI_FAIL_BIT
            );
        }
    }
    else if (
        event_base == IP_EVENT &&
        event_id == IP_EVENT_STA_GOT_IP
    )
    {
        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        ESP_LOGI(
            TAG,
            "Wi-Fi connected. IP=" IPSTR,
            IP2STR(&event->ip_info.ip)
        );

        wifi_retry_count = 0;

        xEventGroupSetBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );
    }
}

bool wifi_manager_initialize(
    wifi_connected_callback_t connected_callback
)
{
    esp_err_t nvs_result = nvs_flash_init();

    if (
        nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND
    )
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    else
    {
        ESP_ERROR_CHECK(nvs_result);
    }

    wifi_event_group = xEventGroupCreate();

    if (wifi_event_group == NULL)
    {
        ESP_LOGE(
            TAG,
            "Wi-Fi event group creation failed"
        );

        return false;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_config =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(
        esp_wifi_init(&wifi_init_config)
    );

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL,
            NULL
        )
    );

    wifi_config_t wifi_config = {0};

    strncpy(
        (char *)wifi_config.sta.ssid,
        WIFI_STA_SSID,
        sizeof(wifi_config.sta.ssid) - 1U
    );

    strncpy(
        (char *)wifi_config.sta.password,
        WIFI_STA_PASSWORD,
        sizeof(wifi_config.sta.password) - 1U
    );

    wifi_config.sta.threshold.authmode =
        WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA)
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(
        TAG,
        "Connecting to home Wi-Fi: %s",
        WIFI_STA_SSID
    );

    EventBits_t event_bits =
        xEventGroupWaitBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY
        );

    if ((event_bits & WIFI_CONNECTED_BIT) == 0U)
    {
        ESP_LOGE(
            TAG,
            "Could not connect to Wi-Fi: %s",
            WIFI_STA_SSID
        );

        return false;
    }

    if (connected_callback != NULL)
    {
        connected_callback();
    }

    return true;
}
