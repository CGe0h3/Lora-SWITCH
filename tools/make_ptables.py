#!/usr/bin/env python3
"""Build build/ptables.bin: sector 0 = menu table, sector i+1 = slot i table (4 KB each)."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IDF = os.environ.get("IDF_PATH")
if not IDF:
    sys.exit("IDF_PATH not set (source esp-idf/export.sh first)")
GEN = os.path.join(IDF, "components", "partition_table", "gen_esp32part.py")


def env():
    vals = {}
    with open(os.path.join(ROOT, "partitions", "layout.env")) as f:
        for line in f:
            k, v = line.strip().split("=")
            vals[k] = v
    return vals


def gen(csv):
    out = os.path.join(ROOT, "build", os.path.basename(csv).replace(".csv", ".bin"))
    subprocess.run([sys.executable, GEN, "-q", "--offset", "0x8000", "--flash-size", "16MB", csv, out],
                   check=True)
    data = open(out, "rb").read()
    assert len(data) <= 0x1000
    return data + b"\xff" * (0x1000 - len(data))


def main():
    n = int(env()["SLOTS"])
    csvs = [os.path.join(ROOT, "partitions", "menu.csv")] + \
           [os.path.join(ROOT, "partitions", f"slot{i}.csv") for i in range(n)]
    blob = b"".join(gen(c) for c in csvs)
    out = os.path.join(ROOT, "build", "ptables.bin")
    open(out, "wb").write(blob)

    built = os.path.join(ROOT, "build", "partition_table", "partition-table.bin")
    if os.path.exists(built):
        ref = open(built, "rb").read()
        assert blob[:len(ref)] == ref, "menu table in ptables.bin differs from the build's table"
    print(f"wrote build/ptables.bin ({len(csvs)} tables)")


if __name__ == "__main__":
    main()
