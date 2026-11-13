/*
 * LoRa-SWITCH - Heltec WiFi LoRa 32 V4 boot menu.
 *
 *   power-on / RST  : LoRa-SWITCH splash (2.5 s), then the menu
 *   PRG short press : next line
 *   PRG long press  : start the selected firmware / hibernate / change language
 *   no key          : the last firmware starts after CONFIG_MB_AUTOBOOT_SECONDS
 *
 * Choosing a firmware writes a one-shot record in the "bootsel" sector and reboots;
 * the bootloader hook installs that firmware's partition table and boots it.
 * "Hibernation" puts the board in deep sleep; PRG wakes it up into this menu.
 * "Langue / Language / Idioma" cycles FR -> EN -> ES (saved in the menu's NVS).
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "oled.h"
#include "board.h"
#include "multiboot_layout.h"

#define N_ITEMS (MB_SLOT_COUNT + 2)          // firmwares + hibernate + language
#define ITEM_POWER_OFF MB_SLOT_COUNT
#define ITEM_LANGUAGE  (MB_SLOT_COUNT + 1)
#define LINE_Y0 11
#define LINE_H  10
#define SPLASH_MS 2500
#define APP_NAME "LoRa-SWITCH"

// ---- texts (ASCII only: the OLED font has no accents) ----
enum { LANG_FR, LANG_EN, LANG_ES, LANG_COUNT };
enum { T_SUBTITLE, T_EMPTY, T_HIBERNATE, T_LANGUAGE, T_STARTING, T_HIBERNATING, T_WAKE_HINT,
       T_EMPTY_SLOT, T_LONG_OK, T_COUNT };
static const char *const TXT[LANG_COUNT][T_COUNT] = {
    [LANG_FR] = {"multi-boot LoRa", "vide", "Hibernation", "Langue : FR", "Demarrage",
                 "Hibernation...", "PRG pour reveiller", "Emplacement vide", "long=OK"},
    [LANG_EN] = {"LoRa multi-boot", "empty", "Hibernate", "Language: EN", "Starting",
                 "Hibernating...", "Press PRG to wake", "Empty slot", "long=OK"},
    [LANG_ES] = {"multiarranque LoRa", "vacio", "Hibernar", "Idioma: ES", "Iniciando",
                 "Hibernando...", "PRG para despertar", "Ranura vacia", "largo=OK"},
};
static int s_lang = LANG_FR;
#define T(id) TXT[s_lang][id]

static void lang_load(void)
{
    nvs_handle_t h;
    uint8_t v;
    if (nvs_open("lswitch", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, "lang", &v) == ESP_OK && v < LANG_COUNT) s_lang = v;
        nvs_close(h);
    }
}

static void lang_save(void)
{
    nvs_handle_t h;
    if (nvs_open("lswitch", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "lang", (uint8_t)s_lang);
        nvs_commit(h);
        nvs_close(h);
    }
}

static const char *TAG = "menu";

static bool s_present[MB_SLOT_COUNT];
static const esp_partition_t *s_bootsel;

static bool image_present(const esp_partition_t *p)
{
    uint8_t hdr[24];
    if (!p || esp_partition_read(p, 0, hdr, sizeof(hdr)) != ESP_OK) return false;
    uint16_t chip_id = hdr[12] | (hdr[13] << 8);
    return hdr[0] == 0xE9 && chip_id == 9;   // ESP image magic + ESP32-S3
}

static bool read_record(mb_record_t *r)
{
    if (esp_partition_read(s_bootsel, 0, r, sizeof(*r)) != ESP_OK) return false;
    return r->magic == MB_MAGIC && r->slot < MB_SLOT_COUNT && r->check == ~(r->magic ^ r->slot);
}

static void write_record(uint32_t slot, uint32_t pending, uint32_t wake_menu)
{
    mb_record_t r = {.magic = MB_MAGIC, .slot = slot, .pending = pending, .wake_menu = wake_menu};
    r.check = ~(r.magic ^ r.slot);
    ESP_ERROR_CHECK(esp_partition_erase_range(s_bootsel, 0, 0x1000));
    ESP_ERROR_CHECK(esp_partition_write(s_bootsel, 0, &r, sizeof(r)));
}

static void center_text(int y, const char *s, int scale)
{
    if (scale == 1) oled_text((OLED_W - (int)strlen(s) * 6) / 2, y, s, false);
    else oled_text_scaled((OLED_W - oled_text_scaled_width(s, scale)) / 2, y, s, scale);
}

static void splash(void)
{
    oled_clear();
    center_text(0, APP_NAME, 2);
    center_text(24, T(T_SUBTITLE), 1);
    // a small mesh: 5 nodes, 6 links
    static const int nx[] = {8, 34, 64, 94, 120}, ny[] = {54, 42, 58, 42, 54};
    static const int links[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {1, 3}, {0, 2}};
    for (size_t i = 0; i < sizeof(links) / sizeof(links[0]); i++)
        oled_line(nx[links[i][0]], ny[links[i][0]], nx[links[i][1]], ny[links[i][1]]);
    for (int i = 0; i < 5; i++) oled_circle(nx[i], ny[i], 3, true);
    oled_flush();
    vTaskDelay(pdMS_TO_TICKS(SPLASH_MS));
}

static void message(const char *l1, const char *l2)
{
    oled_clear();
    center_text(0, APP_NAME, 2);
    center_text(30, l1, 1);
    if (l2) center_text(44, l2, 1);
    oled_flush();
}

static void boot_slot(int i)
{
    message(T(T_STARTING), MB_SLOT_NAMES[i]);
    write_record((uint32_t)i, MB_PENDING, 0xFFFFFFFF);
    ESP_LOGI(TAG, "boot request -> %s", MB_SLOT_NAMES[i]);
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

static void power_off(int last_slot)
{
    message(T(T_HIBERNATING), T(T_WAKE_HINT));
    // keep the last firmware for auto-boot, and wake up into this menu
    write_record((uint32_t)last_slot, 0, MB_WAKE_MENU);
    vTaskDelay(pdMS_TO_TICKS(1500));
    oled_clear();
    oled_flush();
    board_power_off();
}

static void draw(int sel, int countdown)
{
    oled_clear();
    oled_fill_rect(0, 0, OLED_W, 10, true);
    oled_text(1, -1, APP_NAME, true);
    char right[24];
    if (countdown > 0) snprintf(right, sizeof(right), "auto %ds", countdown);
    else snprintf(right, sizeof(right), "%s", T(T_LONG_OK));
    oled_text(OLED_W - 1 - (int)strlen(right) * 6, -1, right, true);

    for (int i = 0; i < N_ITEMS; i++) {
        char line[40];
        char mark = i == sel ? '>' : ' ';
        if (i == ITEM_POWER_OFF)
            snprintf(line, sizeof(line), "%c %s", mark, T(T_HIBERNATE));
        else if (i == ITEM_LANGUAGE)
            snprintf(line, sizeof(line), "%c %s", mark, T(T_LANGUAGE));
        else
            snprintf(line, sizeof(line), "%c %-11s%s%s", mark, MB_SLOT_NAMES[i],
                     s_present[i] ? "" : " ", s_present[i] ? "" : T(T_EMPTY));
        int y = LINE_Y0 + i * LINE_H;
        if (i == sel) oled_fill_rect(0, y, OLED_W, LINE_H, true);
        oled_text(0, y - 2, line, i == sel);
    }
    oled_flush();
}

void app_main(void)
{
    board_release_holds();     // pins may still be held after a "Hibernation" wake-up

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    lang_load();

    s_bootsel = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "bootsel");
    if (!s_bootsel) {
        ESP_LOGE(TAG, "no 'bootsel' partition: wrong partition table");
        return;
    }
    int n_present = 0;
    for (int i = 0; i < MB_SLOT_COUNT; i++) {
        char label[8];
        snprintf(label, sizeof(label), "img%d", i);
        s_present[i] = image_present(
            esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label));
        n_present += s_present[i];
    }

    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << PIN_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn);

    bool have_oled = oled_init();

    mb_record_t rec;
    int sel = 0, last = 0;
    bool autoboot = false;
    if (read_record(&rec)) last = (int)rec.slot;
    if (read_record(&rec) && s_present[rec.slot]) {
        sel = (int)rec.slot;
        autoboot = true;
    } else {
        for (int i = 0; i < MB_SLOT_COUNT; i++)
            if (s_present[i]) { sel = i; last = i; break; }
    }
    // No screen: nothing to choose with, just run the last (or first present) firmware.
    if (!have_oled && n_present) boot_slot(sel);

    splash();
    if (autoboot && CONFIG_MB_AUTOBOOT_SECONDS == 0) boot_slot(sel);

    const int64_t deadline = esp_timer_get_time() + (int64_t)CONFIG_MB_AUTOBOOT_SECONDS * 1000000;
    int shown = -1, shown_sel = -1;
    int64_t pressed_at = 0;
    bool armed = gpio_get_level(PIN_BTN) != 0;   // ignore a press still held from the wake-up

    for (;;) {
        int remaining = 0;
        if (autoboot) {
            int64_t left = deadline - esp_timer_get_time();
            if (left <= 0) boot_slot(sel);
            remaining = (int)((left + 999999) / 1000000);
        }
        if (remaining != shown || sel != shown_sel) {
            draw(sel, remaining);
            shown = remaining;
            shown_sel = sel;
        }

        bool down = gpio_get_level(PIN_BTN) == 0;
        int64_t now = esp_timer_get_time();
        if (!armed) {
            if (!down) armed = true;
        } else if (down && !pressed_at) {
            pressed_at = now;
            autoboot = false;                       // any key cancels the countdown
        } else if (down && pressed_at && now - pressed_at >= CONFIG_MB_LONG_PRESS_MS * 1000LL) {
            if (sel == ITEM_POWER_OFF) power_off(last);
            if (sel == ITEM_LANGUAGE) {
                s_lang = (s_lang + 1) % LANG_COUNT;
                lang_save();
                draw(sel, 0);
                while (gpio_get_level(PIN_BTN) == 0) vTaskDelay(pdMS_TO_TICKS(20));
                pressed_at = 0;
                shown = -2;
                continue;
            }
            if (s_present[sel]) boot_slot(sel);
            message(T(T_EMPTY_SLOT), NULL);
            while (gpio_get_level(PIN_BTN) == 0) vTaskDelay(pdMS_TO_TICKS(20));
            vTaskDelay(pdMS_TO_TICKS(600));
            pressed_at = 0;
            shown = -2;
        } else if (!down && pressed_at) {
            if (now - pressed_at > 30000) sel = (sel + 1) % N_ITEMS;   // debounced short press
            pressed_at = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
