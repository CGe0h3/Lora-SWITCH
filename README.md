# LoRa-SWITCH - Heltec V4 Multi-Boot

> **Download:** get the ready-to-install package from the
> [latest release](https://github.com/CGe0h3/Lora-SWITCH/releases/latest) (`LoRa-SWITCH-v5.zip`).
> The full flash image used by `INSTALLER.bat` is only in the release zip, not in this repository.
> Website: https://cge0h3.github.io/Lora-SWITCH/

Boot menu for the **Heltec WiFi LoRa 32 V4** (ESP32-S3, 16 MB flash).
Meshtastic, MeshCore and Reticulum live side by side in flash. You pick one
on the OLED at power-on, and each firmware **keeps its own settings**
(separate NVS and filesystem).

```
┌────────────────────────────┐
│ MULTI-BOOT  Heltec V4      │
│> Meshtastic                │  PRG short press  : next line
│  MeshCore                  │  PRG long press   : start
│  Reticulum            vide │  nothing          : last firmware after 5 s
│────────────────────────────│
│Auto dans 5s                │
└────────────────────────────┘
```

## How it works

1. **Custom bootloader** (stock ESP-IDF bootloader plus a hook, `bootloader_components/`).
   Before reading the partition table, it decides what to run:
   - **power-on or RST button** → menu
   - **any other reset** (firmware reboot, deep-sleep wake, watchdog, USB) → **the same firmware as before**.
     Meshtastic and MeshCore can reboot or sleep without ever going through the menu.
2. **Partition table swap.** Each firmware expects its own `nvs` and its own
   `spiffs` partition. The bootloader copies the right table to `0x8000` before
   booting, so each firmware sees a standard table that points to *its own* areas.
   The table is only rewritten when switching to another firmware (one 4 KB sector,
   about 50 ms).
3. **Menu** (`main/`): SSD1306 OLED, PRG button. It writes the choice to a
   `bootsel` sector, then reboots.

### Flash map (16 MB)

| Area | Offset | Size |
|---|---|---|
| bootloader + table + menu | 0x000000 | 512 KB |
| bootsel + table store | 0x080000 | 20 KB |
| NVS + otadata per firmware | 0x090000 / 0x0A0000 / 0x0B0000 | 56 KB each |
| App slot 0 / 1 / 2 | 0x0C0000 / 0x3C0000 / 0x6C0000 | 3 MB each |
| Filesystem 0 / 1 / 2 | 0x9C0000 / 0xBD0000 / 0xDE0000 | 2.06 MB each |

Names and labels are in `tools/layout.py` (single source; rerun `./build.sh` after editing).

## v5: LoRa-SWITCH
- Renamed LoRa-SWITCH; splash 2.5 s; menu: 3 firmwares + **Hibernation** + **Language** (FR/EN/ES,
  saved in the menu NVS, cycles on long press). All boot-screen texts follow the language.

## v4: splash, Power Off, Reticulum

- Power-on / RST shows the splash, then the menu.
- **Hibernation**: OLED off, SX1262 reset + sleep, FEM LDO / GNSS / Vext / LED off, pins held,
  ESP32-S3 deep sleep. **PRG wakes it up into the menu** (record `wake_menu`, handled by the
  bootloader hook). No power latch on the V4: this is deep sleep, not a hard power cut.
- **Reticulum slot = RNode firmware 1.86** (official Heltec V4 build), Bluetooth address +3,
  pre-provisioned by `tools/rnode_provision.py` (EEPROM blob in NVS: product C3, model C8,
  firmware hash), web-console SPIFFS image in its 1920 KB filesystem.
- `MISE_A_JOUR_LORA-SWITCH.bat`: new menu/bootloader + RNode without erasing Meshtastic/MeshCore.

## Quick install (Windows 11)

Double-click **`INSTALLER.bat`**: it finds the COM port, erases the board, then writes
`firmware/heltec-v4-multiboot-complet.bin` (menu + Meshtastic 2.7.26 + MeshCore 1.18.0).
No Python required (`tools/windows/esptool.exe` is included).
Step-by-step guide: `Guide_Installation_LoRa-SWITCH.pdf`.

## Included firmwares (`firmware/`)

| File | Version | Source |
|---|---|---|
| `meshtastic-2.7.26-heltec-v4.bin` | Meshtastic 2.7.26.54e0d8d (stable channel) | github.com/meshtastic/firmware |
| `meshcore-1.18.0-companion-ble-heltec-v4.bin` | MeshCore companion BLE v1.18.0-81dbae5 | github.com/meshcore-dev/MeshCore |
| `heltec-v4-multiboot-complet.bin` | full image to write at 0x0 | built here |

**Already installed the first version?** `MISE_A_JOUR_MESHCORE.bat` replaces only slot 1
(MeshCore with its own Bluetooth address); Meshtastic and its settings are kept.

## Bluetooth: one address per firmware

Every firmware derives its BLE address from the chip MAC, so by default Meshtastic and MeshCore
advertise with the **same address**: the phone mixes up bonds/GATT cache, or the Meshtastic app
reconnects to the MeshCore firmware, and MeshCore pairing does nothing.
`tools/patch_bt_mac.py` (applied automatically by `flash.py` to slots 1 and 2) patches one instruction
in ESP-IDF 4.4's `esp_read_mac()`, so slot 1 uses MAC+2 and slot 2 uses MAC+3 (both in the chip's own
Espressif block). Disable with `--no-bt-patch`. Works with Arduino-ESP32 2.0.x builds; other
firmwares get a warning and are left unchanged.

**MeshCore first pairing (iOS):** right after entering the PIN the app may show
"Failed to fetch device info - timeout" (the serial log shows the board answered). Tap
**Reconnect**: it works, and later connections are direct.

Windows helpers: `MONITEUR_SERIE.bat` (serial log to `journal_serie.txt`),
`REINIT_BLUETOOTH_MESHCORE.bat` (erase MeshCore's Bluetooth bonds only).

## Manual install (any OS)

Requires Python 3 and `pip install esptool`. The `dist/` binaries are already built.

1. Download the firmwares **for Heltec V4** (not V3, not V4-R8):
   - Meshtastic: the `firmware-heltec-v4-X.Y.Z.bin` file from the release
     (the `.factory.bin` also works, the app is extracted automatically)
   - MeshCore: the Heltec V4 `.bin` (companion or repeater, as you prefer)
   - Reticulum: an ESP32-S3 / Heltec V4 build (RNode firmware or RTNode-HeltecV4)
2. Put the board in download mode if needed (hold PRG, press RST), then:

```bash
python3 tools/flash.py install -p /dev/ttyACM0 \
    0=firmware-heltec-v4-2.7.x.bin  1=meshcore-heltec_v4.bin  2=rnode-heltec_v4.bin
```

`install` erases the whole chip once. On Windows the port looks like `COM5`.

## Updating ONE firmware without touching the others

```bash
python3 tools/flash.py slot -p /dev/ttyACM0 0 firmware-heltec-v4-2.7.y.bin          # data kept
python3 tools/flash.py slot -p /dev/ttyACM0 1 meshcore-new.bin --wipe              # data reset
```

## ⚠️ What to know (read this)

- **Never use the web flashers** (flasher.meshtastic.org, MeshCore flasher, `rnodeconf --autoinstall`)
  on this board. They write to `0x0`/`0x10000` and will overwrite the menu.
  Always update with `flash.py slot`. If it happens: `flash.py install --no-erase ...`
  restores the multi-boot (only the overwritten firmware is lost).
- **Built-in OTA updates** (Meshtastic BLE/WiFi OTA, MeshCore WiFi OTA) will not work:
  each firmware only sees its own app partition. This is deliberate, so one firmware can
  never overwrite another.
- **Power loss during a switch** (the ~50 ms of the table copy) can leave the board
  unable to boot. Repair: `flash.py install --no-erase` over USB (data is kept).
- **Firmware compatibility.** Any ESP32-S3 Arduino/ESP-IDF firmware that mounts
  its filesystem on the `spiffs` label works without modification. A firmware that uses
  another label: change it in `tools/layout.py`. A firmware that hard-codes flash
  addresses (rare) will not work.
- **Heltec V3** (8 MB): this layout does not fit. **V4-R8**: select the board with
  `idf.py menuconfig` → *Heltec multi-boot menu* (OLED power pin is different).

## Rebuilding

ESP-IDF **v4.4.7**, the same version Meshtastic 2.7 and MeshCore 1.18 are built with
(Arduino-ESP32 2.0.x), so the bootloader matches what these firmwares expect:

```bash
. ~/esp/esp-idf-v4.4.7/export.sh
./build.sh          # -> dist/
```

Or push to GitHub: the `.github/workflows/build.yml` workflow builds `dist/` by itself.

| File | Role |
|---|---|
| `bootloader_components/multiboot_hook/hooks.c` | boot decision + table swap |
| `main/main.c` | OLED menu, PRG button, auto-boot |
| `main/oled.c` | minimal SSD1306 driver |
| `tools/layout.py` | flash map → CSVs + shared header |
| `tools/make_ptables.py` | table store (`ptables.bin`) |
| `tools/flash.py` | install / per-slot update / full image (`merge`) / app extraction |
| `tools/patch_bt_mac.py` | distinct Bluetooth address per slot |
