#!/usr/bin/env python3
"""
Build the NVS image that "provisions" the RNode firmware (Reticulum) in a multi-boot slot.

The RNode firmware refuses to run until its EEPROM holds a device description
(product / model / hw revision / checksum) and the SHA-256 of the installed firmware.
Normally `rnodeconf --autoinstall` writes these, but it also reflashes the whole chip,
which would destroy the multi-boot. Arduino-ESP32 keeps the "EEPROM" as one NVS blob
(namespace "eeprom", key "eeprom"), so we generate that NVS partition directly.

  python3 tools/rnode_provision.py rnode_slot2.bin rnode_nvs.bin

Values used (Heltec WiFi LoRa 32 V4, from the RNode sources Boards.h / ROM.h):
  product 0xC3 (PRODUCT_H32_V4), model 0xC8 (850-950 MHz, 28 dBm), hw rev 1,
  Bluetooth enabled. Radio parameters are left to the host (Reticulum config).
The image MUST be the exact one flashed in the slot (after the Bluetooth patch):
its appended SHA-256 is the "firmware hash" the RNode checks at boot.
"""
import argparse
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
import time

EEPROM_SIZE = 4096
EEPROM_RESERVED = 200
OFF = EEPROM_SIZE - EEPROM_RESERVED            # eeprom_addr(0)
DEV_SIG_LEN, DEV_HASH_LEN = 64, 32
FWHASH_OFF = EEPROM_SIZE - EEPROM_RESERVED - DEV_SIG_LEN - DEV_HASH_LEN

ADDR_PRODUCT, ADDR_MODEL, ADDR_HW_REV, ADDR_SERIAL, ADDR_MADE = 0x00, 0x01, 0x02, 0x03, 0x07
ADDR_CHKSUM, ADDR_INFO_LOCK, ADDR_CONF_BT = 0x0B, 0x9B, 0xB0
INFO_LOCK_BYTE = BT_ENABLE_BYTE = 0x73
PRODUCT_H32_V4, MODEL_C8 = 0xC3, 0xC8


def image_digest(img):
    """Appended SHA-256 of an ESP app image (what esp_partition_get_sha256 returns)."""
    if img[0] != 0xE9 or img[23] != 1:
        raise ValueError("not an ESP app image with appended SHA-256")
    pos = 24
    for _ in range(img[1]):
        _, n = struct.unpack_from("<II", img, pos)
        pos += 8 + n
    pos = (pos + 16) & ~15
    digest = img[pos:pos + 32]
    if hashlib.sha256(img[:pos]).digest() != digest:
        raise ValueError("image SHA-256 does not match its content")
    return digest


def eeprom_blob(fw_hash, serial=None, bluetooth=True):
    e = bytearray(b"\xff" * EEPROM_SIZE)
    serial = serial if serial is not None else int(time.time()) & 0xFFFFFFFF
    info = bytes([PRODUCT_H32_V4, MODEL_C8, 0x01]) + struct.pack(">I", serial) + struct.pack(">I", int(time.time()))
    e[OFF:OFF + len(info)] = info
    e[OFF + ADDR_CHKSUM:OFF + ADDR_CHKSUM + 16] = hashlib.md5(info).digest()
    e[OFF + ADDR_INFO_LOCK] = INFO_LOCK_BYTE
    if bluetooth:
        e[OFF + ADDR_CONF_BT] = BT_ENABLE_BYTE
    e[FWHASH_OFF:FWHASH_OFF + DEV_HASH_LEN] = fw_hash
    return bytes(e)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("app", help="RNode app image exactly as written in the slot")
    ap.add_argument("out", help="NVS partition image to write at the slot's nvs offset")
    ap.add_argument("--size", default="0xC000", help="NVS partition size (default 0xC000)")
    ap.add_argument("--no-bluetooth", action="store_true")
    a = ap.parse_args()

    idf = os.environ.get("IDF_PATH")
    gen = os.path.join(idf or "", "components", "nvs_flash", "nvs_partition_generator", "nvs_partition_gen.py")
    if not os.path.exists(gen):
        sys.exit("needs ESP-IDF (IDF_PATH) for nvs_partition_gen.py")

    blob = eeprom_blob(image_digest(open(a.app, "rb").read()), bluetooth=not a.no_bluetooth)
    with tempfile.TemporaryDirectory() as tmp:
        bpath = os.path.join(tmp, "eeprom.bin")
        open(bpath, "wb").write(blob)
        csv = os.path.join(tmp, "nvs.csv")
        open(csv, "w").write(f"key,type,encoding,value\neeprom,namespace,,\neeprom,file,binary,{bpath}\n")
        subprocess.run([sys.executable, gen, "generate", csv, a.out, a.size], check=True,
                       stdout=subprocess.DEVNULL)
    print(f"wrote {a.out}: RNode provisioned (Heltec V4, model C8), firmware hash {blob[FWHASH_OFF:FWHASH_OFF+4].hex()}...")


if __name__ == "__main__":
    main()
