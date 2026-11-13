#!/usr/bin/env python3
"""
Flash helper for the Heltec V4 multi-boot.

  # first install (erases the whole chip), firmwares optional
  python3 tools/flash.py install -p /dev/ttyACM0 0=meshtastic.bin 1=meshcore.bin 2=rnode.bin

  # update / replace ONE firmware, the others keep their data
  python3 tools/flash.py slot -p /dev/ttyACM0 1 meshcore-new.bin
  python3 tools/flash.py slot -p /dev/ttyACM0 1 meshcore.bin --wipe   # also reset its settings

  # just extract the app from a "factory"/"merged" image
  python3 tools/flash.py extract firmware.factory.bin app.bin

Accepted inputs: app-only images (*.bin as used for OTA/update) or full merged images
(*.factory.bin, *-merged.bin): the app is extracted automatically.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import patch_bt_mac  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ESP32S3_CHIP_ID = 9


def layout():
    vals = {}
    with open(os.path.join(ROOT, "partitions", "layout.env")) as f:
        for line in f:
            k, v = line.strip().split("=")
            vals[k] = int(v, 0)
    return vals


def image_length(data, off=0):
    """Exact length of an ESP app image starting at `off`."""
    if data[off] != 0xE9:
        raise ValueError("not an ESP image (magic 0xE9 missing)")
    seg_count = data[off + 1]
    chip_id = struct.unpack_from("<H", data, off + 12)[0]
    hash_appended = data[off + 23]
    if chip_id != ESP32S3_CHIP_ID:
        raise ValueError(f"image is for chip id {chip_id}, not ESP32-S3")
    pos = off + 24
    for _ in range(seg_count):
        _, seg_len = struct.unpack_from("<II", data, pos)
        pos += 8 + seg_len
    pos = (pos + 16) & ~15            # padding + checksum byte
    if hash_appended == 1:
        pos += 32
    return pos - off


def extract_app(data):
    """Return the app image from an app-only or merged image."""
    if len(data) > 0x9000 and data[0x8000:0x8002] == b"\xaa\x50":
        apps = []
        for e in range(0x8000, 0x8C00, 32):
            magic, ptype, sub, offset, size = struct.unpack_from("<HBBII", data, e)
            if magic != 0x50AA:
                break
            if ptype == 0 and offset < len(data) and data[offset] == 0xE9:
                apps.append(offset)
        if not apps:
            raise ValueError("merged image without app partition content")
        off = apps[0]
        print(f"  merged image detected, app found at 0x{off:X}")
        return data[off:off + image_length(data, off)]
    return data[:image_length(data)]


def esptool(port, baud, *args):
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port, "-b", str(baud),
           "--before", "default_reset", "--after", "hard_reset", *args]
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)


def prepare(slot, path, tmp, L, bt_patch=True):
    data = open(path, "rb").read()
    app = extract_app(data)
    if bt_patch and slot > 0:
        # distinct Bluetooth address per slot (slot 0 keeps base+1, slot 1 -> +2, slot 2 -> +3)
        try:
            app, _ = patch_bt_mac.patch(app, slot + 1)
            print(f"  slot {slot}: Bluetooth address set to base MAC + {slot + 1}")
        except ValueError as e:
            print(f"  WARNING slot {slot}: Bluetooth address NOT changed ({e})")
            print("           the phone may confuse this firmware with slot 0")
    if len(app) > L["APP_SIZE"]:
        sys.exit(f"{path}: {len(app)} bytes > slot size {L['APP_SIZE']}")
    out = os.path.join(tmp, f"slot{slot}.bin")
    open(out, "wb").write(app)
    print(f"  slot {slot}: {os.path.basename(path)} -> {len(app) // 1024} KB")
    return out


def parse_pairs(pairs, n):
    res = {}
    for p in pairs:
        k, _, v = p.partition("=")
        if not v or not k.isdigit() or int(k) >= n:
            sys.exit(f"bad argument '{p}', expected N=file.bin with N in 0..{n - 1}")
        res[int(k)] = v
    return res


def main():
    L = layout()
    n = L["SLOTS"]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    i = sub.add_parser("install")
    i.add_argument("-p", "--port", required=True)
    i.add_argument("-b", "--baud", default=921600, type=int)
    i.add_argument("--no-erase", action="store_true", help="do not erase the whole chip first")
    i.add_argument("images", nargs="*", help="N=file.bin")
    i.add_argument("--no-bt-patch", action="store_true", help="keep the factory Bluetooth address in every slot")
    s = sub.add_parser("slot")
    s.add_argument("-p", "--port", required=True)
    s.add_argument("-b", "--baud", default=921600, type=int)
    s.add_argument("n", type=int)
    s.add_argument("image")
    s.add_argument("--wipe", action="store_true", help="erase this firmware's settings/filesystem")
    s.add_argument("--no-bt-patch", action="store_true", help="keep the factory Bluetooth address")
    m = sub.add_parser("merge", help="build the full image (bootloader+menu+firmwares) for offset 0x0")
    m.add_argument("out")
    m.add_argument("images", nargs="*", help="N=file.bin")
    m.add_argument("--no-bt-patch", action="store_true")
    m.add_argument("--raw", action="append", default=[], metavar="OFFSET=file",
                   help="extra data written as-is (e.g. RNode NVS / SPIFFS images)")
    e = sub.add_parser("extract")
    e.add_argument("image")
    e.add_argument("out")
    a = ap.parse_args()

    if a.cmd == "extract":
        app = extract_app(open(a.image, "rb").read())
        open(a.out, "wb").write(app)
        print(f"wrote {a.out} ({len(app)} bytes)")
        return

    if a.cmd == "merge":
        dist = os.path.join(ROOT, "dist")
        parts = [(0x0, "bootloader.bin"), (0x8000, "partition-table.bin"),
                 (0x10000, "heltec_multiboot.bin"), (L["PTABLES_OFF"], "ptables.bin")]
        blobs = [(off, open(os.path.join(dist, f), "rb").read()) for off, f in parts]
        with tempfile.TemporaryDirectory() as tmp:
            for slot, path in sorted(parse_pairs(a.images, n).items()):
                f = prepare(slot, path, tmp, L, not a.no_bt_patch)
                blobs.append((L[f"SLOT{slot}_APP"], open(f, "rb").read()))
        for r in a.raw:
            off, _, path = r.partition("=")
            blobs.append((int(off, 0), open(path, "rb").read()))
        blobs.sort()
        for (o1, b1), (o2, _) in zip(blobs, blobs[1:]):
            if o1 + len(b1) > o2:
                sys.exit(f"overlap at 0x{o2:X}")
        size = max(off + len(b) for off, b in blobs)
        img = bytearray(b"\xff" * size)
        for off, b in blobs:
            img[off:off + len(b)] = b
        open(a.out, "wb").write(img)
        print(f"wrote {a.out} ({size // 1024} KB) - write it at 0x0 after a full erase")
        return

    with tempfile.TemporaryDirectory() as tmp:
        if a.cmd == "install":
            dist = os.path.join(ROOT, "dist")
            names = ["bootloader.bin", "partition-table.bin", "heltec_multiboot.bin", "ptables.bin"]
            needed = [os.path.join(dist, x) for x in names]
            for f in needed:
                if not os.path.exists(f):
                    sys.exit(f"missing {f}: run ./build.sh first (or use the prebuilt dist/)")
            writes = ["0x0", needed[0], "0x8000", needed[1], "0x10000", needed[2],
                      hex(L["PTABLES_OFF"]), needed[3]]
            for slot, path in sorted(parse_pairs(a.images, n).items()):
                writes += [hex(L[f"SLOT{slot}_APP"]), prepare(slot, path, tmp, L, not a.no_bt_patch)]
            if not a.no_erase:
                esptool(a.port, a.baud, "--after", "no_reset", "erase_flash")
            else:
                esptool(a.port, a.baud, "--after", "no_reset", "erase_region", hex(L["BOOTSEL_OFF"]), "0x1000")
            esptool(a.port, a.baud, "write_flash", *writes)
        else:
            if not 0 <= a.n < n:
                sys.exit(f"slot must be 0..{n - 1}")
            img = prepare(a.n, a.image, tmp, L, not a.no_bt_patch)
            if a.wipe:
                esptool(a.port, a.baud, "--after", "no_reset", "erase_region",
                        hex(L[f"SLOT{a.n}_META"]), hex(L["META_SIZE"]))
                esptool(a.port, a.baud, "--after", "no_reset", "erase_region",
                        hex(L[f"SLOT{a.n}_FS"]), hex(L["FS_SIZE"]))
            esptool(a.port, a.baud, "write_flash", hex(L[f"SLOT{a.n}_APP"]), img)


if __name__ == "__main__":
    main()
