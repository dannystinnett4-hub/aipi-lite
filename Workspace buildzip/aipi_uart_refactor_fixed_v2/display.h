#pragma once

#include <stdint.h>

#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 128

void display_initialize(void);
uint16_t display_rgb565(uint8_t red, uint8_t green, uint8_t blue);
void display_present(void);
void display_clear(uint16_t color);
void display_set_pixel(int x, int y, uint16_t color);
void display_fill_rectangle(int x, int y, int width, int height, uint16_t color);
void display_fill_circle(int center_x, int center_y, int radius, uint16_t color);
void display_fill_rounded_rectangle(int x, int y, int width, int height, int radius, uint16_t color);
