#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_WIDTH          128
#define LCD_HEIGHT         128

#define LCD_PIN_SCLK       GPIO_NUM_16
#define LCD_PIN_MOSI       GPIO_NUM_17
#define LCD_PIN_CS         GPIO_NUM_15
#define LCD_PIN_DC         GPIO_NUM_7
#define LCD_PIN_RESET      GPIO_NUM_18
#define LCD_PIN_BACKLIGHT  GPIO_NUM_3

#define POWER_CONTROL_PIN  GPIO_NUM_10

#define LCD_HOST           SPI3_HOST
#define LCD_CLOCK_HZ       (20 * 1000 * 1000)

typedef enum
{
    FACE_NEUTRAL,
    FACE_HAPPY,
    FACE_SLEEPY,
    FACE_CURIOUS
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
    const uint16_t background = rgb565(0, 5, 12);

    const int eye_width = 45;
    const int maximum_height = 42;

    int eye_height = (maximum_height * openness) / 100;

    if (eye_height < 3)
    {
        eye_height = 3;
    }

    int eye_y = center_y - (eye_height / 2);

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

    if (openness > 25)
    {
        int pupil_center_x = center_x + gaze_x;
        int pupil_center_y = center_y + gaze_y;

        fill_circle(
            pupil_center_x,
            pupil_center_y,
            12,
            iris_color
        );

        fill_circle(
            pupil_center_x,
            pupil_center_y,
            6,
            pupil_color
        );

        fill_circle(
            pupil_center_x - 4,
            pupil_center_y - 4,
            2,
            highlight
        );
    }

    if (expression == FACE_HAPPY)
    {
        for (int row = 0; row < 11; ++row)
        {
            int inset = row / 2;

            fill_rectangle(
                center_x - 23 + inset,
                center_y + 8 + row,
                46 - (inset * 2),
                1,
                background
            );
        }
    }
    else if (expression == FACE_SLEEPY)
    {
        fill_rectangle(
            center_x - 24,
            center_y - 13,
            48,
            13,
            background
        );
    }
    else if (expression == FACE_CURIOUS)
    {
        fill_rectangle(
            center_x - 23,
            center_y - 20,
            46,
            5,
            background
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
    for (int frame = 0; frame <= frames; ++frame)
    {
        int gaze_x =
            start_x +
            ((end_x - start_x) * frame) / frames;

        int gaze_y =
            start_y +
            ((end_y - start_y) * frame) / frames;

        draw_face(
            gaze_x,
            gaze_y,
            100,
            expression
        );

        vTaskDelay(pdMS_TO_TICKS(35));
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

void app_main(void)
{
    initialize_power();
    initialize_backlight();
    initialize_display();

    int gaze_x = 0;
    int gaze_y = 0;

    draw_face(
        gaze_x,
        gaze_y,
        100,
        FACE_NEUTRAL
    );

    while (true)
    {
        uint32_t random_value = esp_random();

        int next_x =
            (int)(random_value % 17U) - 8;

        int next_y =
            (int)((random_value >> 8) % 9U) - 4;

        animate_gaze(
            gaze_x,
            gaze_y,
            next_x,
            next_y,
            8,
            FACE_NEUTRAL
        );

        gaze_x = next_x;
        gaze_y = next_y;

        vTaskDelay(
            pdMS_TO_TICKS(
                500 + (esp_random() % 1200U)
            )
        );

        if ((esp_random() % 3U) == 0U)
        {
            animate_blink(
                gaze_x,
                gaze_y,
                FACE_NEUTRAL
            );
        }

        if ((esp_random() % 10U) == 0U)
        {
            draw_face(
                gaze_x,
                gaze_y,
                100,
                FACE_HAPPY
            );

            vTaskDelay(pdMS_TO_TICKS(900));
        }

        if ((esp_random() % 14U) == 0U)
        {
            draw_face(
                gaze_x,
                gaze_y,
                55,
                FACE_SLEEPY
            );

            vTaskDelay(pdMS_TO_TICKS(1100));

            animate_blink(
                gaze_x,
                gaze_y,
                FACE_NEUTRAL
            );
        }
    }
}