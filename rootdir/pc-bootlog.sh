#!/system/bin/sh
# Keeps the logs of the last three boots in $1 (default /metadata/pc-logs, the install folder's
# metadata/pc-logs on the PC's own partition): boot-1 is this boot, boot-3 the oldest.
dir=${1:-/metadata/pc-logs}
mkdir -p "$dir"
rm -rf "$dir/boot-3"
[ -d "$dir/boot-2" ] && mv "$dir/boot-2" "$dir/boot-3"
[ -d "$dir/boot-1" ] && mv "$dir/boot-1" "$dir/boot-2"
mkdir -p "$dir/boot-1"
