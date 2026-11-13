#pragma once
#include <stdbool.h>
#include <stdint.h>

#define OLED_W 128
#define OLED_H 64

bool oled_init(void);
void oled_clear(void);
void oled_text(int x, int y, const char *s, bool inverted);
void oled_fill_rect(int x, int y, int w, int h, bool on);
void oled_hline(int x, int y, int w);
void oled_text_scaled(int x, int y, const char *s, int scale);
int oled_text_scaled_width(const char *s, int scale);
void oled_line(int x0, int y0, int x1, int y1);
void oled_circle(int cx, int cy, int r, bool fill);
void oled_flush(void);
void oled_power_off(void);   // display off + charge pump off (before deep sleep)
