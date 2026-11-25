#!/bin/bash
# Boots the LineageOS for PCs ISO in a QEMU/KVM window, like a real PC: UEFI firmware, GRUB menu,
# live system. Click in the window to use the mouse; Ctrl+Alt+G gives it back.
#
#   run_vm.sh                 the final ISO (out/lineage-pc.iso), live, nothing kept
#   run_vm.sh --disk          also attaches a 64 GB virtual disk (out/vm/disk.qcow2) to test the installer;
#                             after installing, run it again with --disk to boot the installed system
#   run_vm.sh --iso FILE      another ISO
#   run_vm.sh --res WxH       screen size (default 1920x1080)
#   run_vm.sh --bios          legacy BIOS instead of UEFI
#   run_vm.sh --no-iso        boot only from the virtual disk (needs --disk)
set -euo pipefail

TOP=$(cd "$(dirname "$0")/../../../.." && pwd)
ISO=$TOP/out/lineage-pc.iso
DIR=$TOP/out/vm
RES=1920x1080
DISK=0
BIOS=0
USE_ISO=1
while [ $# -gt 0 ]; do
	case "$1" in
		--disk) DISK=1 ;;
		--iso) ISO=$2; shift ;;
		--res) RES=$2; shift ;;
		--bios) BIOS=1 ;;
		--no-iso) USE_ISO=0 ;;
		*) echo "unknown option: $1"; exit 1 ;;
	esac
	shift
done

mkdir -p "$DIR"
# No S3/S4: QEMU's virtio-gpu (virgl) does not come back from suspend and the screen would freeze
# when Android sleeps after the screen times out.
ARGS=(-enable-kvm -cpu host -smp 4 -m 6G -machine q35,vmport=off -name "LineageOS for PCs"
	-global ICH9-LPC.disable_s3=1 -global ICH9-LPC.disable_s4=1
	-device "virtio-vga-gl,xres=${RES%x*},yres=${RES#*x}" -display sdl,gl=on
	-device qemu-xhci -device usb-mouse -device usb-kbd
	-netdev user,id=n -device virtio-net-pci,netdev=n
	-audiodev pipewire,id=snd -device intel-hda -device hda-duplex,audiodev=snd)

if [ "$BIOS" = 0 ]; then
	[ -f "$DIR/OVMF_VARS.fd" ] || cp /usr/share/edk2/ovmf/OVMF_VARS.fd "$DIR/OVMF_VARS.fd"
	ARGS+=(-drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/ovmf/OVMF_CODE.fd
		-drive if=pflash,format=raw,file="$DIR/OVMF_VARS.fd")
fi
if [ "$USE_ISO" = 1 ]; then
	ARGS+=(-drive file="$ISO",media=cdrom,readonly=on)
fi
if [ "$DISK" = 1 ]; then
	[ -f "$DIR/disk.qcow2" ] || qemu-img create -q -f qcow2 "$DIR/disk.qcow2" 64G
	ARGS+=(-drive file="$DIR/disk.qcow2",if=none,id=d0 -device nvme,drive=d0,serial=lineagepc)
fi

exec qemu-system-x86_64 "${ARGS[@]}"
