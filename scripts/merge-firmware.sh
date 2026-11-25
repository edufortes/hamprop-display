#!/usr/bin/env bash
# Build the firmware and merge bootloader, partitions and app into one image
# that can be flashed at offset 0x0 (esptool-js, esptool write-flash).
# usage: scripts/merge-firmware.sh [output.bin]   (set NO_BUILD=1 to skip the build)
set -euo pipefail
cd "$(dirname "$0")/.."

OUT="${1:-release/hamprop-merged.bin}"
ENV_NAME=supermini
BUILD=".pio/build/$ENV_NAME"
CORE_DIR="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"

[ -n "${NO_BUILD:-}" ] || pio run -e "$ENV_NAME"

# The framework package location varies between PlatformIO setups, so search for it.
BOOT_APP0="$(find "$CORE_DIR" -name boot_app0.bin -path '*partitions*' 2>/dev/null | head -n1)"
if [ -z "$BOOT_APP0" ]; then
  echo "boot_app0.bin not found under $CORE_DIR" >&2
  ls -la "$CORE_DIR/packages" >&2 || true
  exit 1
fi

mkdir -p "$(dirname "$OUT")"
python -m esptool --chip esp32c3 merge_bin -o "$OUT" \
  --flash-mode dio --flash-size 4MB \
  0x0 "$BUILD/bootloader.bin" \
  0x8000 "$BUILD/partitions.bin" \
  0xe000 "$BOOT_APP0" \
  0x10000 "$BUILD/firmware.bin"
echo "wrote $OUT"
