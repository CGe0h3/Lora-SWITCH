/*
 * Multi-boot hook for the ESP-IDF 2nd-stage bootloader.
 *
 * Runs right after bootloader_init(), BEFORE the partition table is read.
 * It decides which image must run and, if needed, copies the matching
 * partition table from the "ptables" store into the active slot at 0x8000.
 * The stock bootloader then does the rest:
 *   - menu table  -> only one app (factory = menu)  -> boots the menu
 *   - slot table  -> only one app (ota_0 = firmware) -> boots that firmware
 *
 * Decision:
 *   1. one-shot request written by the menu (record.pending still erased) -> that slot
 *   2. wake-up after the menu's "Hibernation" (record.wake_menu set)        -> menu
 *   3. power-on / RST button                                             -> menu
 *   4. any other reset (software reboot, deep-sleep wake, watchdog,
 *      USB reset, brown-out...)                                          -> last slot
 * Rule 4 lets each firmware reboot / sleep / OTA-reboot without ever seeing the menu.
 */
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "bootloader_flash_priv.h"
#include "multiboot_layout.h"

static const char *TAG = "multiboot";

#define SECTOR 0x1000
#define CHUNK  256

static uint8_t s_a[CHUNK] __attribute__((aligned(4)));
static uint8_t s_b[CHUNK] __attribute__((aligned(4)));

void bootloader_hooks_include(void) {}

static bool table_differs(uint32_t src)
{
    for (uint32_t off = 0; off < SECTOR; off += CHUNK) {
        if (bootloader_flash_read(src + off, s_a, CHUNK, false) != ESP_OK ||
            bootloader_flash_read(MB_PTABLE_ACTIVE + off, s_b, CHUNK, false) != ESP_OK) {
            return true;
        }
        if (memcmp(s_a, s_b, CHUNK) != 0) {
            return true;
        }
    }
    return false;
}

static void install_table(int target)
{
    const uint32_t src = MB_PTABLES_OFFSET + (uint32_t)(target + 1) * SECTOR;

    // Refuse to copy anything that does not look like a partition table (store not flashed?)
    if (bootloader_flash_read(src, s_a, 4, false) != ESP_OK || s_a[0] != 0xAA || s_a[1] != 0x50) {
        ESP_LOGE(TAG, "ptables store empty at 0x%x - keeping current table", (unsigned)src);
        return;
    }
    if (!table_differs(src)) {
        return;
    }
    ESP_LOGI(TAG, "installing partition table for %s",
             target == MB_MENU_INDEX ? "menu" : MB_SLOT_NAMES[target]);
    if (bootloader_flash_erase_sector(MB_PTABLE_ACTIVE / SECTOR) != ESP_OK) {
        ESP_LOGE(TAG, "erase failed");
        return;
    }
    for (uint32_t off = 0; off < SECTOR; off += CHUNK) {
        bootloader_flash_read(src + off, s_a, CHUNK, false);
        if (bootloader_flash_write(MB_PTABLE_ACTIVE + off, s_a, CHUNK, false) != ESP_OK) {
            ESP_LOGE(TAG, "write failed at +0x%x", (unsigned)off);
            return;
        }
    }
}

void bootloader_after_init(void)
{
    mb_record_t rec;
    if (bootloader_flash_read(MB_BOOTSEL_OFFSET, &rec, sizeof(rec), false) != ESP_OK) {
        memset(&rec, 0xFF, sizeof(rec));
    }
    const bool valid = rec.magic == MB_MAGIC && rec.slot < MB_SLOT_COUNT &&
                       rec.check == ~(rec.magic ^ rec.slot);
    const soc_reset_reason_t reason = esp_rom_get_reset_reason(0);

    int target;
    if (valid && rec.pending == MB_PENDING) {
        target = (int)rec.slot;
        uint32_t zero = 0;   // 1 -> 0 bit flips only: no erase, no wear
        bootloader_flash_write(MB_BOOTSEL_OFFSET + offsetof(mb_record_t, pending), &zero, 4, false);
    } else if (valid && rec.wake_menu == MB_WAKE_MENU) {
        target = MB_MENU_INDEX;
        uint32_t zero = 0;
        bootloader_flash_write(MB_BOOTSEL_OFFSET + offsetof(mb_record_t, wake_menu), &zero, 4, false);
    } else if (!valid || reason == RESET_REASON_CHIP_POWER_ON) {
        target = MB_MENU_INDEX;
    } else {
        target = (int)rec.slot;
    }
    ESP_LOGI(TAG, "reset reason %d -> %s", (int)reason,
             target == MB_MENU_INDEX ? "menu" : MB_SLOT_NAMES[target]);
    install_table(target);
}
