#!/bin/bash
# Copy a linux-firmware tree into the source tree (Soong refuses PRODUCT_COPY_FILES from outside it).
#   device/pc/pc_x86_64/kernel/stage_firmware.sh [SRC]   (default SRC: the build host's /lib/firmware)
# Output: vendor/pc/firmware (local, not in git), the default PC_FIRMWARE_DIR of device.mk.
set -euo pipefail
TOP=$(cd "$(dirname "$0")/../../../.." && pwd)
SRC=${1:-/lib/firmware}
mkdir -p "$TOP/vendor/pc/firmware"
rsync -a --delete "$SRC"/ "$TOP/vendor/pc/firmware"/
echo "firmware: $(find "$TOP/vendor/pc/firmware" -type f | wc -l) files from $SRC"
