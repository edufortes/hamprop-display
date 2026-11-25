#!/usr/bin/env bash
# Build the firmware and merge bootloader, partitions and app into one image
# that can be flashed at offset 0x0 (esptool-js, esptool write-flash).
# usage: scripts/merge-firmware.sh [output.bin]   (set NO_BUILD=1 to skip the build)
set -euo pipefail
cd "$(dirname "$0")/.."

OUT="${1:-release/hamprop-merged.bin}"
ENV_NAME=supermini
BUILD=".pio/build/$ENV_NAME"
BOOT_APP0="$(ls "$HOME"/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin)"

[ -n "${NO_BUILD:-}" ] || pio run -e "$ENV_NAME"

mkdir -p "$(dirname "$OUT")"
python -m esptool --chip esp32c3 merge_bin -o "$OUT" \
  --flash-mode dio --flash-size 4MB \
  0x0 "$BUILD/bootloader.bin" \
  0x8000 "$BUILD/partitions.bin" \
  0xe000 "$BOOT_APP0" \
  0x10000 "$BUILD/firmware.bin"
echo "wrote $OUT"
