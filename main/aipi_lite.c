#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "freertos/event_groups.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"


#define LCD_WIDTH          128
#define LCD_HEIGHT         128

#define LCD_PIN_SCLK       GPIO_NUM_16
#define LCD_PIN_MOSI       GPIO_NUM_17
#define LCD_PIN_CS         GPIO_NUM_15
#define LCD_PIN_DC         GPIO_NUM_7
#define LCD_PIN_RESET      GPIO_NUM_18
#define LCD_PIN_BACKLIGHT  GPIO_NUM_3

#define POWER_CONTROL_PIN  GPIO_NUM_10

/*
 * External command UART.
 * Change these two pins if GPIO 4/5 are used elsewhere on your board.
 */
#define COMMAND_UART_PORT   UART_NUM_1
#define COMMAND_UART_TX_PIN GPIO_NUM_4
#define COMMAND_UART_RX_PIN GPIO_NUM_5
#define COMMAND_UART_BAUD   115200

#define WIFI_STA_SSID       "The Promise LAN"
#define WIFI_STA_PASSWORD   "Cheese211!?"

#define OTA_ACCESS_KEY      "724799"
#define OTA_HTTP_PORT       8080
#define OTA_BUFFER_SIZE     4096

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_MAXIMUM_RETRY  10

#define LCD_HOST           SPI3_HOST
#define LCD_CLOCK_HZ       (20 * 1000 * 1000)

#define RESET_BUTTON_PIN          GPIO_NUM_1
#define ACTION_BUTTON_PIN         GPIO_NUM_42
#define BUTTON_POLL_INTERVAL_MS   20
#define BUTTON_DEBOUNCE_MS        50

typedef enum
{
    FACE_NEUTRAL,
    FACE_HAPPY,
    FACE_SLEEPY,
    FACE_CURIOUS,
    FACE_THINKING,
    FACE_LISTENING,
    FACE_SPEAKING,
    FACE_ALERT
} face_expression_t;

static const char *TAG = "AIPI_FACE";

static esp_lcd_panel_handle_t panel_handle;
static uint16_t *framebuffer;

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)(
        ((red & 0xF8U) << 8) |
        ((green & 0xFCU) << 3) |
        (blue >> 3)
    );
}

static void display_frame(void)
{
    ESP_ERROR_CHECK(
        esp_lcd_panel_draw_bitmap(
            panel_handle,
            0,
            0,
            LCD_WIDTH,
            LCD_HEIGHT,
            framebuffer
        )
    );
}

static void clear_frame(uint16_t color)
{
    for (size_t i = 0; i < LCD_WIDTH * LCD_HEIGHT; ++i)
    {
        framebuffer[i] = color;
    }
}

static void set_pixel(int x, int y, uint16_t color)
{
    if (
        x >= 0 &&
        x < LCD_WIDTH &&
        y >= 0 &&
        y < LCD_HEIGHT
    )
    {
        framebuffer[(y * LCD_WIDTH) + x] = color;
    }
}

static void fill_rectangle(
    int x,
    int y,
    int width,
    int height,
    uint16_t color
)
{
    if (x < 0)
    {
        width += x;
        x = 0;
    }

    if (y < 0)
    {
        height += y;
        y = 0;
    }

    if (x + width > LCD_WIDTH)
    {
        width = LCD_WIDTH - x;
    }

    if (y + height > LCD_HEIGHT)
    {
        height = LCD_HEIGHT - y;
    }

    if (width <= 0 || height <= 0)
    {
        return;
    }

    for (int row = y; row < y + height; ++row)
    {
        for (int column = x; column < x + width; ++column)
        {
            set_pixel(column, row, color);
        }
    }
}

static void fill_circle(
    int center_x,
    int center_y,
    int radius,
    uint16_t color
)
{
    const int radius_squared = radius * radius;

    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            if ((x * x) + (y * y) <= radius_squared)
            {
                set_pixel(
                    center_x + x,
                    center_y + y,
                    color
                );
            }
        }
    }
}

static void fill_rounded_rectangle(
    int x,
    int y,
    int width,
    int height,
    int radius,
    uint16_t color
)
{
    if (radius < 1)
    {
        fill_rectangle(x, y, width, height, color);
        return;
    }

    if (radius * 2 > width)
    {
        radius = width / 2;
    }

    if (radius * 2 > height)
    {
        radius = height / 2;
    }

    fill_rectangle(
        x + radius,
        y,
        width - (radius * 2),
        height,
        color
    );

    fill_rectangle(
        x,
        y + radius,
        width,
        height - (radius * 2),
        color
    );

    fill_circle(
        x + radius,
        y + radius,
        radius,
        color
    );

    fill_circle(
        x + width - radius - 1,
        y + radius,
        radius,
        color
    );

    fill_circle(
        x + radius,
        y + height - radius - 1,
        radius,
        color
    );

    fill_circle(
        x + width - radius - 1,
        y + height - radius - 1,
        radius,
        color
    );
}

static void draw_eye(
    int center_x,
    int center_y,
    int gaze_x,
    int gaze_y,
    int openness,
    face_expression_t expression
)
{
    const uint16_t eye_white = rgb565(235, 245, 255);
    const uint16_t iris_color = rgb565(0, 190, 255);
    const uint16_t pupil_color = rgb565(0, 8, 15);
    const uint16_t highlight = rgb565(255, 255, 255);

    const bool is_left_eye = center_x < (LCD_WIDTH / 2);

    int eye_width = 45;
    int maximum_height = 42;
    int local_openness = openness;
    int local_gaze_x = gaze_x;
    int local_gaze_y = gaze_y;
    int iris_radius = 12;
    int pupil_radius = 6;
    int vertical_offset = 0;

    /*
     * Expressions are created by changing the actual eye shape,
     * pupil size and gaze. No black eyebrow bars are drawn.
     */
    switch (expression)
    {
        case FACE_HAPPY:
            local_openness = (local_openness * 52) / 100;
            vertical_offset = 4;
            local_gaze_y += 2;
            break;

        case FACE_SLEEPY:
            local_openness = (local_openness * 43) / 100;
            vertical_offset = 3;
            local_gaze_y += 2;
            break;

        case FACE_CURIOUS:
            if (is_left_eye)
            {
                maximum_height = 46;
                eye_width = 46;
            }
            else
            {
                local_openness = (local_openness * 72) / 100;
                eye_width = 43;
            }
            local_gaze_x += is_left_eye ? -1 : 1;
            break;

        case FACE_THINKING:
            local_openness = (local_openness * 70) / 100;
            local_gaze_x -= 3;
            local_gaze_y -= 4;
            break;

        case FACE_LISTENING:
            maximum_height = 46;
            eye_width = 47;
            local_gaze_x = 0;
            local_gaze_y = 0;
            iris_radius = 13;
            pupil_radius = 6;
            break;

        case FACE_SPEAKING:
            maximum_height = 44;
            vertical_offset = is_left_eye ? -1 : 1;
            break;

        case FACE_ALERT:
            maximum_height = 49;
            eye_width = 48;
            local_gaze_x = 0;
            local_gaze_y = 0;
            iris_radius = 10;
            pupil_radius = 4;
            break;

        case FACE_NEUTRAL:
        default:
            break;
    }

    if (local_openness > 100)
    {
        local_openness = 100;
    }

    if (local_openness < 0)
    {
        local_openness = 0;
    }

    int eye_height = (maximum_height * local_openness) / 100;

    if (eye_height < 3)
    {
        eye_height = 3;
    }

    int eye_center_y = center_y + vertical_offset;
    int eye_y = eye_center_y - (eye_height / 2);
    int corner_radius = eye_height / 2;

    if (corner_radius > 14)
    {
        corner_radius = 14;
    }

    fill_rounded_rectangle(
        center_x - (eye_width / 2),
        eye_y,
        eye_width,
        eye_height,
        corner_radius,
        eye_white
    );

    if (local_openness > 25)
    {
        int horizontal_limit = (eye_width / 2) - iris_radius - 2;
        int vertical_limit = (eye_height / 2) - iris_radius - 1;

        if (horizontal_limit < 0)
        {
            horizontal_limit = 0;
        }

        if (vertical_limit < 0)
        {
            vertical_limit = 0;
        }

        if (local_gaze_x > horizontal_limit)
        {
            local_gaze_x = horizontal_limit;
        }
        else if (local_gaze_x < -horizontal_limit)
        {
            local_gaze_x = -horizontal_limit;
        }

        if (local_gaze_y > vertical_limit)
        {
            local_gaze_y = vertical_limit;
        }
        else if (local_gaze_y < -vertical_limit)
        {
            local_gaze_y = -vertical_limit;
        }

        int pupil_center_x = center_x + local_gaze_x;
        int pupil_center_y = eye_center_y + local_gaze_y;

        fill_circle(
            pupil_center_x,
            pupil_center_y,
            iris_radius,
            iris_color
        );

        fill_circle(
            pupil_center_x,
            pupil_center_y,
            pupil_radius,
            pupil_color
        );

        fill_circle(
            pupil_center_x - 4,
            pupil_center_y - 4,
            2,
            highlight
        );
    }
}

static void draw_face(
    int gaze_x,
    int gaze_y,
    int openness,
    face_expression_t expression
)
{
    const uint16_t background = rgb565(0, 5, 12);

    clear_frame(background);

    draw_eye(
        36,
        64,
        gaze_x,
        gaze_y,
        openness,
        expression
    );

    draw_eye(
        92,
        64,
        gaze_x,
        gaze_y,
        openness,
        expression
    );

    display_frame();
}

static void animate_gaze(
    int start_x,
    int start_y,
    int end_x,
    int end_y,
    int frames,
    face_expression_t expression
)
{
    const int saccade_frames = 3;

    for (int frame = 0; frame <= saccade_frames; ++frame)
    {
        int gaze_x = start_x + ((end_x - start_x) * frame) / saccade_frames;
        int gaze_y = start_y + ((end_y - start_y) * frame) / saccade_frames;

        draw_face(gaze_x, gaze_y, 100, expression);
        vTaskDelay(pdMS_TO_TICKS(18));
    }

    if ((esp_random() % 4U) == 0U)
    {
        int adjust_x = end_x + ((int)(esp_random() % 3U) - 1);
        int adjust_y = end_y + ((int)(esp_random() % 3U) - 1);

        draw_face(adjust_x, adjust_y, 100, expression);
        vTaskDelay(pdMS_TO_TICKS(25));
        draw_face(end_x, end_y, 100, expression);
    }
}

static void animate_blink(
    int gaze_x,
    int gaze_y,
    face_expression_t expression
)
{
    for (int openness = 100; openness >= 0; openness -= 20)
    {
        draw_face(
            gaze_x,
            gaze_y,
            openness,
            expression
        );

        vTaskDelay(pdMS_TO_TICKS(28));
    }

    vTaskDelay(pdMS_TO_TICKS(45));

    for (int openness = 0; openness <= 100; openness += 20)
    {
        draw_face(
            gaze_x,
            gaze_y,
            openness,
            expression
        );

        vTaskDelay(pdMS_TO_TICKS(28));
    }
}

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

static void initialize_backlight(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << LCD_PIN_BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_set_level(LCD_PIN_BACKLIGHT, 1));
}

static void initialize_display(void)
{
    const spi_bus_config_t bus_config = {
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = LCD_PIN_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz =
            LCD_WIDTH *
            LCD_HEIGHT *
            sizeof(uint16_t)
    };

    ESP_ERROR_CHECK(
        spi_bus_initialize(
            LCD_HOST,
            &bus_config,
            SPI_DMA_CH_AUTO
        )
    );

    esp_lcd_panel_io_handle_t panel_io = NULL;

    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_PIN_CS,
        .dc_gpio_num = LCD_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_spi(
            LCD_HOST,
            &io_config,
            &panel_io
        )
    );

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RESET,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_st7789(
            panel_io,
            &panel_config,
            &panel_handle
        )
    );

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(
        esp_lcd_panel_invert_color(panel_handle, false)
    );
    ESP_ERROR_CHECK(
        esp_lcd_panel_swap_xy(panel_handle, true)
    );
    ESP_ERROR_CHECK(
        esp_lcd_panel_mirror(panel_handle, true, false)
    );
    ESP_ERROR_CHECK(
        esp_lcd_panel_disp_on_off(panel_handle, true)
    );

    framebuffer = heap_caps_malloc(
        LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
    );

    if (framebuffer == NULL)
    {
        ESP_LOGE(TAG, "Framebuffer allocation failed");
        abort();
    }

    memset(
        framebuffer,
        0,
        LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t)
    );

    ESP_LOGI(TAG, "Animated face initialized");
}


typedef enum
{
    BEHAVIOR_IDLE,
    BEHAVIOR_CURIOUS,
    BEHAVIOR_THINKING,
    BEHAVIOR_LISTENING,
    BEHAVIOR_HAPPY,
    BEHAVIOR_SLEEPY,
    BEHAVIOR_ALERT
} behavior_state_t;

static behavior_state_t choose_next_behavior(
    behavior_state_t current_behavior
)
{
    uint32_t roll = esp_random() % 100U;
    behavior_state_t next_behavior;

    if (roll < 38U)
    {
        next_behavior = BEHAVIOR_IDLE;
    }
    else if (roll < 54U)
    {
        next_behavior = BEHAVIOR_CURIOUS;
    }
    else if (roll < 68U)
    {
        next_behavior = BEHAVIOR_THINKING;
    }
    else if (roll < 79U)
    {
        next_behavior = BEHAVIOR_LISTENING;
    }
    else if (roll < 89U)
    {
        next_behavior = BEHAVIOR_HAPPY;
    }
    else if (roll < 96U)
    {
        next_behavior = BEHAVIOR_SLEEPY;
    }
    else
    {
        next_behavior = BEHAVIOR_ALERT;
    }

    if (
        next_behavior == current_behavior &&
        current_behavior != BEHAVIOR_IDLE
    )
    {
        next_behavior = BEHAVIOR_IDLE;
    }

    return next_behavior;
}

static face_expression_t behavior_expression(
    behavior_state_t behavior
)
{
    switch (behavior)
    {
        case BEHAVIOR_CURIOUS:
            return FACE_CURIOUS;

        case BEHAVIOR_THINKING:
            return FACE_THINKING;

        case BEHAVIOR_LISTENING:
            return FACE_LISTENING;

        case BEHAVIOR_HAPPY:
            return FACE_HAPPY;

        case BEHAVIOR_SLEEPY:
            return FACE_SLEEPY;

        case BEHAVIOR_ALERT:
            return FACE_ALERT;

        case BEHAVIOR_IDLE:
        default:
            return FACE_NEUTRAL;
    }
}

static uint32_t behavior_duration_ms(
    behavior_state_t behavior
)
{
    switch (behavior)
    {
        case BEHAVIOR_CURIOUS:
            return 4500U + (esp_random() % 3500U);

        case BEHAVIOR_THINKING:
            return 5000U + (esp_random() % 4000U);

        case BEHAVIOR_LISTENING:
            return 3500U + (esp_random() % 3000U);

        case BEHAVIOR_HAPPY:
            return 3000U + (esp_random() % 3000U);

        case BEHAVIOR_SLEEPY:
            return 6000U + (esp_random() % 5000U);

        case BEHAVIOR_ALERT:
            return 1800U + (esp_random() % 1600U);

        case BEHAVIOR_IDLE:
        default:
            return 5000U + (esp_random() % 5000U);
    }
}

static void choose_behavior_gaze(
    behavior_state_t behavior,
    int *target_x,
    int *target_y
)
{
    uint32_t random_value = esp_random();

    switch (behavior)
    {
        case BEHAVIOR_CURIOUS:
            *target_x = ((random_value & 1U) != 0U) ? 8 : -8;
            *target_y = (int)((random_value >> 8) % 7U) - 3;
            break;

        case BEHAVIOR_THINKING:
            *target_x = -5 + (int)(random_value % 4U);
            *target_y = -4 + (int)((random_value >> 8) % 3U);
            break;

        case BEHAVIOR_LISTENING:
            *target_x = (int)(random_value % 5U) - 2;
            *target_y = (int)((random_value >> 8) % 3U) - 1;
            break;

        case BEHAVIOR_HAPPY:
            *target_x = (int)(random_value % 17U) - 8;
            *target_y = (int)((random_value >> 8) % 7U) - 3;
            break;

        case BEHAVIOR_SLEEPY:
            *target_x = (int)(random_value % 9U) - 4;
            *target_y = 2 + (int)((random_value >> 8) % 3U);
            break;

        case BEHAVIOR_ALERT:
            *target_x = (int)(random_value % 17U) - 8;
            *target_y = (int)((random_value >> 8) % 9U) - 4;
            break;

        case BEHAVIOR_IDLE:
        default:
            *target_x = (int)(random_value % 13U) - 6;
            *target_y = (int)((random_value >> 8) % 7U) - 3;
            break;
    }
}

static uint32_t behavior_pause_ms(
    behavior_state_t behavior
)
{
    switch (behavior)
    {
        case BEHAVIOR_ALERT:
            return 220U + (esp_random() % 380U);

        case BEHAVIOR_HAPPY:
            return 500U + (esp_random() % 800U);

        case BEHAVIOR_CURIOUS:
            return 800U + (esp_random() % 1300U);

        case BEHAVIOR_THINKING:
            return 1300U + (esp_random() % 1700U);

        case BEHAVIOR_LISTENING:
            return 1600U + (esp_random() % 1800U);

        case BEHAVIOR_SLEEPY:
            return 1800U + (esp_random() % 2400U);

        case BEHAVIOR_IDLE:
        default:
            return 900U + (esp_random() % 1800U);
    }
}

static uint32_t behavior_blink_divisor(
    behavior_state_t behavior
)
{
    switch (behavior)
    {
        case BEHAVIOR_SLEEPY:
            return 2U;

        case BEHAVIOR_HAPPY:
            return 3U;

        case BEHAVIOR_ALERT:
            return 6U;

        case BEHAVIOR_THINKING:
            return 6U;

        case BEHAVIOR_LISTENING:
            return 7U;

        case BEHAVIOR_CURIOUS:
            return 5U;

        case BEHAVIOR_IDLE:
        default:
            return 4U;
    }
}


typedef enum
{
    FACE_COMMAND_AUTONOMOUS,
    FACE_COMMAND_IDLE,
    FACE_COMMAND_CURIOUS,
    FACE_COMMAND_THINKING,
    FACE_COMMAND_LISTENING,
    FACE_COMMAND_HAPPY,
    FACE_COMMAND_SLEEPY,
    FACE_COMMAND_SPEAKING,
    FACE_COMMAND_ALERT
} face_command_t;

static QueueHandle_t face_command_queue;

static behavior_state_t command_to_behavior(
    face_command_t command
)
{
    switch (command)
    {
        case FACE_COMMAND_CURIOUS:
            return BEHAVIOR_CURIOUS;

        case FACE_COMMAND_THINKING:
            return BEHAVIOR_THINKING;

        case FACE_COMMAND_LISTENING:
            return BEHAVIOR_LISTENING;

        case FACE_COMMAND_HAPPY:
            return BEHAVIOR_HAPPY;

        case FACE_COMMAND_SLEEPY:
            return BEHAVIOR_SLEEPY;

        case FACE_COMMAND_ALERT:
            return BEHAVIOR_ALERT;

        case FACE_COMMAND_IDLE:
        default:
            return BEHAVIOR_IDLE;
    }
}

static face_expression_t command_expression(
    face_command_t command
)
{
    if (command == FACE_COMMAND_SPEAKING)
    {
        return FACE_SPEAKING;
    }

    return behavior_expression(
        command_to_behavior(command)
    );
}

/*
 * Thread-safe entry point for future button, microphone, UART and Wi-Fi code.
 * timeout_ms == 0 performs a non-blocking request.
 */
bool face_send_command(
    face_command_t command,
    uint32_t timeout_ms
)
{
    if (face_command_queue == NULL)
    {
        return false;
    }

    return xQueueSend(
        face_command_queue,
        &command,
        pdMS_TO_TICKS(timeout_ms)
    ) == pdTRUE;
}

static void run_commanded_face(
    face_command_t command,
    int *gaze_x,
    int *gaze_y
)
{
    face_expression_t expression =
        command_expression(command);

    behavior_state_t behavior =
        command_to_behavior(command);

    int openness =
        (command == FACE_COMMAND_SLEEPY) ? 55 : 100;

    if (
        command == FACE_COMMAND_LISTENING ||
        command == FACE_COMMAND_SPEAKING
    )
    {
        animate_gaze(
            *gaze_x,
            *gaze_y,
            0,
            0,
            3,
            expression
        );

        *gaze_x = 0;
        *gaze_y = 0;
    }

    draw_face(
        *gaze_x,
        *gaze_y,
        openness,
        expression
    );

    /*
     * Hold external states until another command arrives.
     * Small animation keeps the face alive while commanded.
     */
    while (true)
    {
        face_command_t next_command;

        if (
            xQueueReceive(
                face_command_queue,
                &next_command,
                pdMS_TO_TICKS(350)
            ) == pdTRUE
        )
        {
            if (next_command == FACE_COMMAND_AUTONOMOUS)
            {
                return;
            }

            command = next_command;
            expression = command_expression(command);
            behavior = command_to_behavior(command);
            openness =
                (command == FACE_COMMAND_SLEEPY) ? 55 : 100;

            if (
                command == FACE_COMMAND_LISTENING ||
                command == FACE_COMMAND_SPEAKING
            )
            {
                *gaze_x = 0;
                *gaze_y = 0;
            }

            draw_face(
                *gaze_x,
                *gaze_y,
                openness,
                expression
            );
        }
        else
        {
            if (command == FACE_COMMAND_SPEAKING)
            {
                int speaking_y =
                    ((esp_random() & 1U) != 0U) ? -1 : 1;

                draw_face(
                    *gaze_x,
                    speaking_y,
                    100,
                    FACE_SPEAKING
                );
            }
            else if (
                (esp_random() %
                 behavior_blink_divisor(behavior)) == 0U
            )
            {
                animate_blink(
                    *gaze_x,
                    *gaze_y,
                    expression
                );

                draw_face(
                    *gaze_x,
                    *gaze_y,
                    openness,
                    expression
                );
            }
        }
    }
}

static void face_behavior_task(void *parameter)
{
    (void)parameter;

    int gaze_x = 0;
    int gaze_y = 0;

    behavior_state_t behavior = BEHAVIOR_IDLE;
    face_expression_t expression = FACE_NEUTRAL;

    draw_face(gaze_x, gaze_y, 100, expression);

    while (true)
    {
        face_command_t command;

        if (
            xQueueReceive(
                face_command_queue,
                &command,
                0
            ) == pdTRUE
        )
        {
            if (command != FACE_COMMAND_AUTONOMOUS)
            {
                run_commanded_face(
                    command,
                    &gaze_x,
                    &gaze_y
                );
            }

            continue;
        }

        behavior = choose_next_behavior(behavior);
        expression = behavior_expression(behavior);

        uint32_t behavior_time_ms =
            behavior_duration_ms(behavior);

        TickType_t behavior_start = xTaskGetTickCount();

        while (
            ((xTaskGetTickCount() - behavior_start) *
             portTICK_PERIOD_MS) < behavior_time_ms
        )
        {
            if (
                xQueueReceive(
                    face_command_queue,
                    &command,
                    0
                ) == pdTRUE
            )
            {
                if (command != FACE_COMMAND_AUTONOMOUS)
                {
                    run_commanded_face(
                        command,
                        &gaze_x,
                        &gaze_y
                    );
                }

                break;
            }

            int target_x;
            int target_y;

            choose_behavior_gaze(
                behavior,
                &target_x,
                &target_y
            );

            animate_gaze(
                gaze_x,
                gaze_y,
                target_x,
                target_y,
                3,
                expression
            );

            gaze_x = target_x;
            gaze_y = target_y;

            int openness =
                (behavior == BEHAVIOR_SLEEPY) ? 55 : 100;

            draw_face(
                gaze_x,
                gaze_y,
                openness,
                expression
            );

            if (
                (esp_random() %
                 behavior_blink_divisor(behavior)) == 0U
            )
            {
                animate_blink(
                    gaze_x,
                    gaze_y,
                    expression
                );

                draw_face(
                    gaze_x,
                    gaze_y,
                    openness,
                    expression
                );
            }

            uint32_t pause_ms =
                behavior_pause_ms(behavior);

            /*
             * Split the pause so external commands are handled quickly.
             */
            while (pause_ms > 0U)
            {
                uint32_t slice_ms =
                    (pause_ms > 50U) ? 50U : pause_ms;

                if (
                    xQueueReceive(
                        face_command_queue,
                        &command,
                        pdMS_TO_TICKS(slice_ms)
                    ) == pdTRUE
                )
                {
                    if (command != FACE_COMMAND_AUTONOMOUS)
                    {
                        run_commanded_face(
                            command,
                            &gaze_x,
                            &gaze_y
                        );
                    }

                    pause_ms = 0U;
                    break;
                }

                pause_ms -= slice_ms;
            }
        }
    }
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

static void initialize_wifi_station(void)
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
        abort();
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

    if ((event_bits & WIFI_CONNECTED_BIT) != 0U)
    {
        start_ota_server();
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Could not connect to Wi-Fi: %s",
            WIFI_STA_SSID
        );
    }
}


static void initialize_front_buttons(void)
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
                ESP_LOGW(TAG, "Reset button pressed; restarting");
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

        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_INTERVAL_MS));
    }
}

static void start_front_button_task(void)
{
    initialize_front_buttons();

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
        ESP_LOGE(TAG, "Front-button task creation failed");
        abort();
    }
}

void app_main(void)
{
    initialize_power();
    initialize_backlight();
    initialize_display();

    face_command_queue = xQueueCreate(
        8,
        sizeof(face_command_t)
    );

    if (face_command_queue == NULL)
    {
        ESP_LOGE(TAG, "Face command queue creation failed");
        abort();
    }

    initialize_command_uart();
    initialize_wifi_station();

    BaseType_t task_created = xTaskCreate(
        face_behavior_task,
        "face_behavior",
        6144,
        NULL,
        5,
        NULL
    );

    if (task_created != pdPASS)
    {
        ESP_LOGE(TAG, "Face behavior task creation failed");
        abort();
    }

    start_front_button_task();

    ESP_LOGI(
        TAG,
        "Face task, UART, home Wi-Fi, OTA, and front buttons ready"
    );
}
