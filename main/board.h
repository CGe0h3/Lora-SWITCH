#pragma once
// Heltec WiFi LoRa 32 V4 pins (from the Meshtastic / MeshCore V4 variants)
#if CONFIG_MB_BOARD_V4_R8
#define PIN_VEXT    40   // OLED / peripherals power, active LOW
#define PIN_VGNSS   42   // GNSS power, active LOW
#define PIN_LED     46
#else
#define PIN_VEXT    36
#define PIN_VGNSS   34
#define PIN_LED     35
#endif
#define PIN_BTN      0   // PRG / USER button, active LOW
#define PIN_ADC_CTRL 37  // battery divider enable (HIGH = on)
// SX1262
#define PIN_LORA_NSS   8
#define PIN_LORA_SCK   9
#define PIN_LORA_MOSI 10
#define PIN_LORA_MISO 11
#define PIN_LORA_RST  12
#define PIN_LORA_BUSY 13
// LoRa front-end (GC1109 on V4.2, KCT8103L on V4.3)
#define PIN_FEM_LDO    7   // VFEM_Ctrl, HIGH = FEM powered
#define PIN_FEM_CSD    2
#define PIN_FEM_CTX    5
#define PIN_FEM_CPS   46   // V4 (non-R8) only

void board_release_holds(void);          // call first thing after wake-up
void board_power_off(void) __attribute__((noreturn));
