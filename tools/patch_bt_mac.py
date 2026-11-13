#!/usr/bin/env python3
"""
Give each multi-boot slot its own Bluetooth address.

Why: every firmware derives its BLE address from the chip's factory MAC
(ESP-IDF esp_read_mac(ESP_MAC_BT) = base MAC + 1). With Meshtastic and MeshCore on the
same board, both advertise with the SAME address, so the phone (iOS especially) mixes
them up: it reuses the bond / GATT cache of the other firmware, or the other app
(e.g. Meshtastic) silently reconnects to it -> pairing does nothing.

How: in the ESP-IDF 4.4 esp_read_mac() code, the ESP_MAC_BT branch is
    l8ui   a3, a2, 5
    addi.n a3, a3, 1      <- patched to +2 (slot 1) or +3 (slot 2)
    s8i    a3, a2, 5
base+2 and base+3 belong to the chip's own Espressif MAC block (unused in the
"two universal MAC" mode), so no clash with other devices.
The image checksum and SHA-256 are then recomputed.

  python3 tools/patch_bt_mac.py in.bin out.bin --offset 2
"""
import argparse
import hashlib
import re
import struct
import sys

# memcpy(mac, base, 6); mac[5] += 1   (Arduino-ESP32 2.0.x / ESP-IDF 4.4, ESP32-S3)
PATTERN = re.compile(rb"\x0c\x6c\xab\xb1\xad\x02\x81..\xe0\x08\x00"
                     rb"\x32\x02\x05([\x1b-\xfb])\x33\x32\x42\x05", re.S)
IMM_OFFSET = 15   # position of the addi.n low byte inside the match


def _segments_end(img):
    pos = 24
    for _ in range(img[1]):
        _, n = struct.unpack_from("<II", img, pos)
        pos += 8 + n
    return pos


def _checksum_pos(img):
    end = _segments_end(img)
    return ((end + 16) & ~15) - 1          # last byte of the 16-byte aligned block


def _segment_ranges(img):
    pos = 24
    for _ in range(img[1]):
        _, n = struct.unpack_from("<II", img, pos)
        yield pos + 8, pos + 8 + n
        pos += 8 + n


def current_offset(img):
    m = list(PATTERN.finditer(img))
    if len(m) != 1:
        return None, m
    return (img[m[0].start() + IMM_OFFSET] >> 4), m


def patch(img, offset):
    """Return a new image where BT MAC = base + offset (1..15)."""
    if not 1 <= offset <= 15:
        raise ValueError("offset must be 1..15")
    img = bytearray(img)
    cur, m = current_offset(img)
    if cur is None:
        raise ValueError(f"BT MAC code not found exactly once ({len(m)} matches): "
                         "unsupported firmware build (needs Arduino-ESP32 2.0.x / IDF 4.4)")
    if cur == offset:
        return bytes(img), False
    pos = m[0].start() + IMM_OFFSET
    if not any(a <= pos < b for a, b in _segment_ranges(img)):
        raise ValueError("match outside image segments")
    old = img[pos]
    new = (offset << 4) | (old & 0x0F)
    img[pos] = new
    csum_pos = _checksum_pos(img)
    img[csum_pos] ^= old ^ new                 # XOR checksum over segment data
    if img[23] == 1:                           # SHA-256 appended after checksum
        img[csum_pos + 1:csum_pos + 33] = hashlib.sha256(img[:csum_pos + 1]).digest()
    return bytes(img), True


def verify(img):
    """Check checksum and appended hash like the bootloader does."""
    csum = 0xEF
    for a, b in _segment_ranges(img):
        for x in img[a:b]:
            csum ^= x
    cp = _checksum_pos(img)
    ok = img[cp] == csum
    if img[23] == 1:
        ok &= img[cp + 1:cp + 33] == hashlib.sha256(img[:cp + 1]).digest()
    return ok


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("--offset", type=int, required=True, help="BT MAC = base + OFFSET (default firmware: 1)")
    a = ap.parse_args()
    data = open(a.inp, "rb").read()
    if not verify(data):
        sys.exit("input image checksum/hash invalid")
    out, changed = patch(data, a.offset)
    assert verify(out)
    open(a.out, "wb").write(out)
    print(f"{a.out}: BT MAC = base+{a.offset}" + ("" if changed else " (already)"))
