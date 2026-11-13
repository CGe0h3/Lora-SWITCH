#!/usr/bin/env bash
# Build menu + custom bootloader + partition-table store (requires ESP-IDF v4.4.7 sourced).
set -euo pipefail
cd "$(dirname "$0")"
: "${IDF_PATH:?source esp-idf/export.sh first (ESP-IDF v4.4.7)}"
python3 tools/layout.py
python "$IDF_PATH/tools/idf.py" set-target esp32s3 >/dev/null 2>&1 || true
python "$IDF_PATH/tools/idf.py" build
python3 tools/make_ptables.py
mkdir -p dist
cp build/bootloader/bootloader.bin build/partition_table/partition-table.bin \
   build/heltec_multiboot.bin build/ptables.bin dist/
echo "OK -> dist/"
