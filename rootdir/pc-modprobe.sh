#!/vendor/bin/sh
# The kernel's helper (CONFIG_STATIC_USERMODEHELPER_PATH, CONFIG_MODPROBE_PATH). Android has no
# /sbin/modprobe and loads drivers through ueventd; only modules that drivers load by name, which no
# uevent asks for, come here: Intel Wi-Fi's op modes (iwlmvm makes wlan0) and Broadcom Wi-Fi's vendor
# parts. Every other call (crypto, protocols, filesystems: hundreds at boot) fails at once, as it did
# before, because running modprobe for each of them held the boot up for a minute.
[ "$1" = "-q" ] && [ "$2" = "--" ] || exit 1
case "$3" in
    iwlmvm | iwldvm | iwlmld | brcmfmac-*) exec /system/bin/modprobe -d /vendor/lib/modules -q -- "$3" ;;
esac
exit 1
