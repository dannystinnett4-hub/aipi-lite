#include "display.h"

#include <stddef.h>
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

#define LCD_PIN_SCLK       GPIO_NUM_16
#define LCD_PIN_MOSI       GPIO_NUM_17
#define LCD_PIN_CS         GPIO_NUM_15
#define LCD_PIN_DC         GPIO_NUM_7
#define LCD_PIN_RESET      GPIO_NUM_18
#define LCD_PIN_BACKLIGHT  GPIO_NUM_3

#define LCD_HOST           SPI3_HOST
#define LCD_CLOCK_HZ       (20 * 1000 * 1000)

#define LCD_WIDTH          DISPLAY_WIDTH
#define LCD_HEIGHT         DISPLAY_HEIGHT

static const char *TAG = "AIPI_DISPLAY";
static esp_lcd_panel_handle_t panel_handle;
static uint16_t *framebuffer;

uint16_t display_rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)(
        ((red & 0xF8U) << 8) |
        ((green & 0xFCU) << 3) |
        (blue >> 3)
    );
}

void display_present(void)
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

void display_clear(uint16_t color)
{
    for (size_t i = 0; i < LCD_WIDTH * LCD_HEIGHT; ++i)
    {
        framebuffer[i] = color;
    }
}

void display_set_pixel(int x, int y, uint16_t color)
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

void display_fill_rectangle(
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
            display_set_pixel(column, row, color);
        }
    }
}

void display_fill_circle(
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
                display_set_pixel(
                    center_x + x,
                    center_y + y,
                    color
                );
            }
        }
    }
}

void display_fill_rounded_rectangle(
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
        display_fill_rectangle(x, y, width, height, color);
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

    display_fill_rectangle(
        x + radius,
        y,
        width - (radius * 2),
        height,
        color
    );

    display_fill_rectangle(
        x,
        y + radius,
        width,
        height - (radius * 2),
        color
    );

    display_fill_circle(
        x + radius,
        y + radius,
        radius,
        color
    );

    display_fill_circle(
        x + width - radius - 1,
        y + radius,
        radius,
        color
    );

    display_fill_circle(
        x + radius,
        y + height - radius - 1,
        radius,
        color
    );

    display_fill_circle(
        x + width - radius - 1,
        y + height - radius - 1,
        radius,
        color
    );
}

static void display_initialize_backlight(void)
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

void display_initialize(void)
{
    display_initialize_backlight();

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
