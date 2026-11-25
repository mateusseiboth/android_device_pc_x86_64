#!/bin/bash
# A VM disk laid out like a PC's: GPT, a 512 MiB EFI system partition (FAT32) and an ext4 partition
# with the rest, for testing the installer (it installs into a folder on an existing partition).
#   make_test_disk.sh DISK.qcow2 [SIZE_GIB]   (default 64)
set -euo pipefail
OUT=$1
SIZE_GIB=${2:-64}
RAW=$(mktemp --suffix=.raw -p "$(dirname "$OUT")")
trap 'rm -f "$RAW"' EXIT

truncate -s "${SIZE_GIB}G" "$RAW"
sgdisk -o -n 1:2048:+512M -t 1:ef00 -c 1:EFI -n 2:0:0 -t 2:8300 -c 2:LINEAGE "$RAW" >/dev/null 2>&1
read -r esp_start esp_end < <(sgdisk -i 1 "$RAW" | awk '/First sector/{f=$3} /Last sector/{l=$3} END{print f, l}')
read -r data_start data_end < <(sgdisk -i 2 "$RAW" | awk '/First sector/{f=$3} /Last sector/{l=$3} END{print f, l}')

mkfs.fat -F 32 -n EFI --offset="$esp_start" "$RAW" $(( (esp_end - esp_start + 1) / 2 )) >/dev/null
mke2fs -q -t ext4 -L LINEAGE -E offset=$(( data_start * 512 )) "$RAW" $(( (data_end - data_start + 1) / 2 ))K

qemu-img convert -O qcow2 "$RAW" "$OUT"
qemu-img info "$OUT" | grep -E 'virtual size|disk size'
