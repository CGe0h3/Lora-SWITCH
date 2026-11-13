// "Power Off" for the Heltec V4: everything that can be switched off is switched off,
// then the ESP32-S3 goes to deep sleep. The PRG button wakes it up (into the menu).
// The V4 has no power latch: this is the lowest-consumption state the board can reach
// without a hardware switch (Heltec specifies < 20 uA in this mode).
#include "board.h"
#include "oled.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power";

static const gpio_num_t HELD[] = {
    PIN_VEXT, PIN_VGNSS, PIN_LED, PIN_ADC_CTRL,
    PIN_LORA_NSS, PIN_LORA_SCK, PIN_LORA_MOSI, PIN_LORA_RST,
    PIN_FEM_LDO, PIN_FEM_CSD, PIN_FEM_CTX,
#if !CONFIG_MB_BOARD_V4_R8
    PIN_FEM_CPS,
#endif
};

void board_release_holds(void)
{
    gpio_deep_sleep_hold_dis();
    for (size_t i = 0; i < sizeof(HELD) / sizeof(HELD[0]); i++) gpio_hold_dis(HELD[i]);
    rtc_gpio_deinit(GPIO_NUM_0);
}

static void out(gpio_num_t pin, int level)
{
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, level);
}

static void spi_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(PIN_LORA_MOSI, (b >> i) & 1);
        esp_rom_delay_us(2);
        gpio_set_level(PIN_LORA_SCK, 1);
        esp_rom_delay_us(2);
        gpio_set_level(PIN_LORA_SCK, 0);
    }
}

// Reset the SX1262 (whatever the previous firmware left it doing) and put it in sleep.
static void lora_sleep(void)
{
    out(PIN_LORA_NSS, 1);
    out(PIN_LORA_SCK, 0);
    out(PIN_LORA_MOSI, 0);
    gpio_reset_pin(PIN_LORA_BUSY);
    gpio_set_direction(PIN_LORA_BUSY, GPIO_MODE_INPUT);
    out(PIN_LORA_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(PIN_LORA_RST, 1);
    for (int i = 0; i < 50 && gpio_get_level(PIN_LORA_BUSY); i++) vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(PIN_LORA_NSS, 0);
    spi_byte(0x84);   // SetSleep
    spi_byte(0x00);   // cold start, RTC off
    gpio_set_level(PIN_LORA_NSS, 1);   // stays HIGH: a falling NSS edge would wake it
    vTaskDelay(pdMS_TO_TICKS(2));
}

void board_power_off(void)
{
    ESP_LOGI(TAG, "power off");
    oled_power_off();
    lora_sleep();

    out(PIN_FEM_LDO, 0);
    out(PIN_FEM_CSD, 0);
    out(PIN_FEM_CTX, 0);
#if !CONFIG_MB_BOARD_V4_R8
    out(PIN_FEM_CPS, 0);
#endif
    out(PIN_LED, 0);
    out(PIN_ADC_CTRL, 0);
    out(PIN_VGNSS, 1);    // active LOW -> off
    out(PIN_VEXT, 1);     // active LOW -> OLED & peripherals off

    for (size_t i = 0; i < sizeof(HELD) / sizeof(HELD[0]); i++) gpio_hold_en(HELD[i]);
    gpio_deep_sleep_hold_en();

    // do not wake up immediately because the button is still pressed
    while (gpio_get_level(PIN_BTN) == 0) vTaskDelay(pdMS_TO_TICKS(20));
    vTaskDelay(pdMS_TO_TICKS(200));

    rtc_gpio_init(GPIO_NUM_0);
    rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(GPIO_NUM_0);
    rtc_gpio_pulldown_dis(GPIO_NUM_0);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    esp_deep_sleep_start();
}
