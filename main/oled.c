// Minimal SSD1306 128x64 I2C driver for the Heltec WiFi LoRa 32 V3/V4 on-board OLED.
#include <string.h>
#include "oled.h"
#include "font6x11.h"
#include "driver/i2c.h"   // legacy driver: builds on ESP-IDF 4.4 and 5.x
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define PIN_SDA 17
#define PIN_SCL 18
#define PIN_RST 21
#if CONFIG_MB_BOARD_V4_R8
#define PIN_VEXT 40
#else
#define PIN_VEXT 36   // Vext (OLED power), active LOW
#endif
#define OLED_ADDR 0x3C

static const char *TAG = "oled";
#define I2C_PORT I2C_NUM_0
static uint8_t s_fb[OLED_W * OLED_H / 8];

static esp_err_t cmd(const uint8_t *c, size_t n)
{
    uint8_t buf[32] = {0x00};
    memcpy(buf + 1, c, n);
    return i2c_master_write_to_device(I2C_PORT, OLED_ADDR, buf, n + 1, pdMS_TO_TICKS(100));
}

bool oled_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_VEXT) | (1ULL << PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(PIN_VEXT, 0);          // power the display
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    if (i2c_param_config(I2C_PORT, &conf) != ESP_OK) return false;
    if (i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) return false;

    static const uint8_t init[] = {
        0xAE,             // display off
        0xD5, 0x80,       // clock
        0xA8, 0x3F,       // multiplex 64
        0xD3, 0x00,       // offset
        0x40,             // start line 0
        0x8D, 0x14,       // charge pump on
        0x20, 0x00,       // horizontal addressing
        0xA1, 0xC8,       // segment remap + COM scan dec (rotated like Meshtastic)
        0xDA, 0x12,       // COM pins
        0x81, 0xCF,       // contrast
        0xD9, 0xF1,       // precharge
        0xDB, 0x40,       // VCOM detect
        0xA4, 0xA6,       // resume RAM, normal
        0xAF,             // display on
    };
    if (cmd(init, sizeof(init)) != ESP_OK) {
        ESP_LOGE(TAG, "SSD1306 not answering at 0x%02X", OLED_ADDR);
        return false;
    }
    oled_clear();
    oled_flush();
    return true;
}

void oled_clear(void) { memset(s_fb, 0, sizeof(s_fb)); }

static inline void px(int x, int y, bool on)
{
    if (x < 0 || y < 0 || x >= OLED_W || y >= OLED_H) return;
    uint8_t *b = &s_fb[(y / 8) * OLED_W + x];
    if (on) *b |= 1 << (y & 7); else *b &= ~(1 << (y & 7));
}

void oled_fill_rect(int x, int y, int w, int h, bool on)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) px(i, j, on);
}

void oled_hline(int x, int y, int w) { oled_fill_rect(x, y, w, 1, true); }

void oled_text(int x, int y, const char *s, bool inverted)
{
    for (; *s; s++, x += FONT_W) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7E) c = '?';
        const uint16_t *g = font6x11[c - 0x20];
        for (int i = 0; i < FONT_W; i++)
            for (int j = 0; j < FONT_H; j++) {
                // inverted = dark glyph on an already-filled bar: only glyph pixels are touched
                if ((g[i] >> j) & 1) px(x + i, y + j, !inverted);
            }
    }
}

static int glyph_cols(unsigned char c)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    if (c == ' ') return 3;
    const uint16_t *g = font6x11[c - 0x20];
    int w = 0;
    for (int i = 0; i < FONT_W; i++) if (g[i]) w = i + 1;
    return w;
}

// Proportional big text: each glyph keeps its real width, 1 pixel between glyphs,
// so "LoRa-SWITCH" fits the 128 px screen at scale 2.
int oled_text_scaled_width(const char *s, int scale)
{
    int w = 0;
    for (const char *p = s; *p; p++) w += glyph_cols((unsigned char)*p) * scale + 1;
    return w ? w - 1 : 0;
}

void oled_text_scaled(int x, int y, const char *s, int scale)
{
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7E) c = '?';
        const uint16_t *g = font6x11[c - 0x20];
        int w = glyph_cols(c);
        for (int i = 0; i < w; i++)
            for (int j = 0; j < FONT_H; j++)
                if ((g[i] >> j) & 1) oled_fill_rect(x + i * scale, y + j * scale, scale, scale, true);
        x += w * scale + 1;
    }
}

void oled_line(int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        px(x0, y0, true);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void oled_circle(int cx, int cy, int r, bool fill)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++) {
            int d = x * x + y * y;
            if (fill ? d <= r * r : (d <= r * r && d > (r - 1) * (r - 1))) px(cx + x, cy + y, true);
        }
}

void oled_power_off(void)
{
    static const uint8_t off[] = {0xAE, 0x8D, 0x10};   // display off, charge pump off
    cmd(off, sizeof(off));
}

void oled_flush(void)
{
    static const uint8_t win[] = {0x21, 0, OLED_W - 1, 0x22, 0, OLED_H / 8 - 1};
    cmd(win, sizeof(win));
    uint8_t buf[1 + 128];
    buf[0] = 0x40;
    for (size_t off = 0; off < sizeof(s_fb); off += 128) {
        memcpy(buf + 1, s_fb + off, 128);
        i2c_master_write_to_device(I2C_PORT, OLED_ADDR, buf, sizeof(buf), pdMS_TO_TICKS(100));
    }
}
