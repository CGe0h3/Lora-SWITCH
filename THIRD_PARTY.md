# Third-party software included

| Component | License | Source code |
|---|---|---|
| Meshtastic firmware 2.7.26.54e0d8d (`firmware/meshtastic-*.bin`) | GPL-3.0 | https://github.com/meshtastic/firmware/tree/v2.7.26.54e0d8d |
| MeshCore companion 1.18.0 (`firmware/meshcore-*.bin`) | MIT | https://github.com/meshcore-dev/MeshCore/tree/companion-v1.18.0 |
| RNode firmware 1.86 (`firmware/rnode-*.bin`, console image) | GPL-3.0 | https://github.com/markqvist/RNode_Firmware/tree/1.86 |
| esptool 4.8.1 (`tools/windows/esptool.exe`) | GPL-2.0 | https://github.com/espressif/esptool/tree/v4.8.1 |
| ESP-IDF 4.4.7 (bootloader, menu runtime) | Apache-2.0 | https://github.com/espressif/esp-idf/tree/v4.4.7 |

The firmware binaries are official release builds. The only change: in slots 1 and 2,
`tools/patch_bt_mac.py` modifies one instruction (Bluetooth address = factory MAC + 2 / + 3)
so that each firmware has its own Bluetooth identity (`firmware/rnode-1.86-slot2.bin` too) (`firmware/meshcore-1.18.0-slot1.bin`
and the copy inside `heltec-v4-multiboot-complet.bin`). The checksum and SHA-256 are recomputed.
If you distribute boards with this image pre-installed, the GPL-3.0 requires you to
provide (or offer) the Meshtastic source code: the link above is enough.

The RNode slot also gets a generated NVS image (`firmware/rnode-1.86-slot2-nvs.bin`, made by
`tools/rnode_provision.py`) holding the device description and firmware hash that
`rnodeconf` would normally write.
