#!/vendor/bin/sh
# The kernel's helper (CONFIG_STATIC_USERMODEHELPER_PATH, CONFIG_MODPROBE_PATH). Android has no
# /sbin/modprobe and loads drivers through ueventd; only modules that drivers or the kernel load by
# name, which no uevent asks for, come here: Intel Wi-Fi's op modes (iwlmvm makes wlan0), Broadcom
# Wi-Fi's vendor parts, HD Audio's codec drivers, and the names pc-drivers.sh lists in
# /dev/pc-modules.byname that this kernel has as modules. Every other call (hundreds at boot) fails at once, without running modprobe, as
# running it for each of them held the boot up for a minute. The kernel runs this without /system/bin
# in PATH.
[ "$1" = "-q" ] && [ "$2" = "--" ] || exit 1
load() { exec /system/bin/modprobe -d /vendor/lib/modules -q -- "$1"; }
case "$3" in
    iwlmvm | iwldvm | iwlmld | brcmfmac-*) load "$3" ;;
    # HD Audio asks for its codec's driver while the controller probes, at boot, and gives up on the
    # sound card when none binds: by the codec's id, then the generic or HDMI parser by module name.
    hdaudio:* | snd-hda-codec-*) load "$3" ;;
esac
/system/bin/grep -qxF -- "$3" /dev/pc-modules.byname 2>/dev/null && load "$3"
exit 1
