#!/system/bin/sh
# Keeps the logs of the last three boots in $1 (default /metadata/pc-logs, the install folder's
# metadata/pc-logs on the PC's own partition): boot-1 is this boot, boot-3 the oldest.
# pc-init normally made this boot's folder already, with the kernel's log from the start
# (kernel.txt) and the boot's id; the folders move down here only when it could not.
dir=${1:-/metadata/pc-logs}
mkdir -p "$dir"
read -r this_boot < /proc/sys/kernel/random/boot_id
read -r logged_boot < "$dir/boot-1/boot_id" 2>/dev/null
[ "$this_boot" = "$logged_boot" ] && exit 0
rm -rf "$dir/boot-3"
[ -d "$dir/boot-2" ] && mv "$dir/boot-2" "$dir/boot-3"
[ -d "$dir/boot-1" ] && mv "$dir/boot-1" "$dir/boot-2"
mkdir -p "$dir/boot-1"
